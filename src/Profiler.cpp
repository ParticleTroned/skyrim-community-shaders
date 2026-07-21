#include "Profiler.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "Utils/D3D.h"

namespace
{
	constexpr float kMaxSaneProfilerSampleMs = 1000.0f;

	bool IsValidProfilerSample(float ms)
	{
		return std::isfinite(ms) && ms >= 0.0f && ms <= kMaxSaneProfilerSampleMs;
	}

	bool IsValidWholeFrameSample(float ms)
	{
		// Discard loading, pause, and alt-tab intervals: they are not representative scene timing.
		return ms > 0.0f && IsValidProfilerSample(ms);
	}
}

float Profiler::RollingHistory::GetAverage() const
{
	if (count == 0)
		return lastMs;
	float sum = 0.0f;
	for (uint32_t i = 0; i < count; i++)
		sum += history[i];
	return sum / static_cast<float>(count);
}

float Profiler::RollingHistory::GetAverage(uint32_t maxSamples) const
{
	if (count == 0 || maxSamples == 0)
		return lastMs;

	const uint32_t sampleCount = std::min(count, maxSamples);
	float sum = 0.0f;
	const uint32_t firstSample = count - sampleCount;
	for (uint32_t i = 0; i < sampleCount; i++)
		sum += GetSample(firstSample + i);
	return sum / static_cast<float>(sampleCount);
}

float Profiler::RollingHistory::GetPercentile(float p) const
{
	if (count == 0)
		return lastMs;

	thread_local std::vector<float> sorted;
	sorted.resize(count);
	for (uint32_t i = 0; i < count; i++)
		sorted[i] = history[i];
	std::sort(sorted.begin(), sorted.end());

	float idx = (p / 100.0f) * static_cast<float>(count - 1);
	uint32_t lo = static_cast<uint32_t>(idx);
	uint32_t hi = std::min(lo + 1, count - 1);
	float frac = idx - static_cast<float>(lo);
	return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

void Profiler::ResetFrameState(FrameQueries& frame)
{
	frame.activeCount = 0;
	frame.activeTimerStack.clear();
	frame.wholeFrameCpuBegin = {};
	frame.wholeFrameCpuMs = 0.0f;
	frame.frameTimeMs = 0.0f;
	frame.hasWholeFrameCpuTime = false;
	frame.hasFrameTime = false;
	frame.presentSynced = false;
	frame.inFlight = false;
	frame.cpuTimers.clear();
}

void Profiler::ResetWholeFrameTimings()
{
	wholeFrameGpuHistory = {};
	wholeFrameCpuHistory = {};
	frameTimeHistory = {};
	wholeFrameSampleId = 0;
	wholeFrameGpuSampleId = 0;
	wholeFrameCpuSampleId = 0;
	frameTimeSampleId = 0;
	wholeFrameLastPresentStartCounter = 0;
	wholeFrameHasLastPresentStart = false;
	wholeFrameLastPresentSynced = false;
	latestFrameWasPresentSynced = false;
}

bool Profiler::HasPendingFrameData(const FrameQueries& frame)
{
	return frame.inFlight || !frame.cpuTimers.empty();
}

void Profiler::Initialize(ID3D11Device* device, ID3D11DeviceContext* a_context)
{
	Release();
	if (!device || !a_context) {
		logger::error("Profiler initialization requires a D3D11 device and immediate context");
		return;
	}

	context = a_context;

	LARGE_INTEGER freq{};
	if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
		logger::error("Profiler initialization failed to obtain the performance-counter frequency");
		Release();
		return;
	}
	cpuTicksToMs = 1000.0 / static_cast<double>(freq.QuadPart);

	wholeFrameGpuTimingAvailable = true;
	for (uint32_t frameIndex = 0; frameIndex < kFrameLatency; ++frameIndex) {
		auto& frame = frames[frameIndex];
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(device->CreateQuery(&disjointDesc, frame.disjoint.put())) || !frame.disjoint) {
			logger::error("Profiler initialization failed to create the D3D11 disjoint query for frame {}", frameIndex);
			Release();
			return;
		}

		D3D11_QUERY_DESC timestampDesc{};
		timestampDesc.Query = D3D11_QUERY_TIMESTAMP;
		const HRESULT wholeFrameBeginResult = device->CreateQuery(&timestampDesc, frame.wholeFrameBegin.put());
		const HRESULT wholeFrameEndResult = device->CreateQuery(&timestampDesc, frame.wholeFrameEnd.put());
		wholeFrameGpuTimingAvailable = wholeFrameGpuTimingAvailable &&
		                               SUCCEEDED(wholeFrameBeginResult) && SUCCEEDED(wholeFrameEndResult) &&
		                               frame.wholeFrameBegin && frame.wholeFrameEnd;
		if (frame.disjoint)
			Util::SetResourceName(frame.disjoint.get(), "Profiler::Frame Disjoint[%u]", frameIndex);
		if (frame.wholeFrameBegin)
			Util::SetResourceName(frame.wholeFrameBegin.get(), "Profiler::WholeFrame Begin[%u]", frameIndex);
		if (frame.wholeFrameEnd)
			Util::SetResourceName(frame.wholeFrameEnd.get(), "Profiler::WholeFrame End[%u]", frameIndex);

		frame.timers.resize(kMaxTimers);
		frame.activeTimerStack.reserve(kMaxTimers);
		for (uint32_t timerIndex = 0; timerIndex < frame.timers.size(); ++timerIndex) {
			auto& timer = frame.timers[timerIndex];
			D3D11_QUERY_DESC tsDesc{};
			tsDesc.Query = D3D11_QUERY_TIMESTAMP;
			if (FAILED(device->CreateQuery(&tsDesc, timer.begin.put())) ||
				FAILED(device->CreateQuery(&tsDesc, timer.end.put())) ||
				!timer.begin || !timer.end) {
				logger::error("Profiler initialization failed to create the D3D11 timestamp query for frame {}, timer {}", frameIndex, timerIndex);
				Release();
				return;
			}
		}
		frame.cpuTimers.reserve(kMaxTimers);
		ResetFrameState(frame);
	}
	if (!wholeFrameGpuTimingAvailable) {
		for (auto& frame : frames) {
			frame.wholeFrameBegin = nullptr;
			frame.wholeFrameEnd = nullptr;
		}
		logger::warn("Whole-frame GPU timing is unavailable because D3D11 timestamp queries could not be created");
	}

	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	frameActive = false;
	initialized = true;
	userEnabled.store(false, std::memory_order_release);
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
}

void Profiler::Release()
{
	for (auto& frame : frames) {
		frame.disjoint = nullptr;
		frame.wholeFrameBegin = nullptr;
		frame.wholeFrameEnd = nullptr;
		frame.timers.clear();
		frame.cpuTimers.clear();
		frame.activeCount = 0;
		frame.activeTimerStack.clear();
		frame.inFlight = false;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	activeCpuTimers.clear();
	completedCpuTimers.clear();
	totalGpuHistory = {};
	totalCpuHistory = {};
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	ResetWholeFrameTimings();
	wholeFrameGpuTimingAvailable = false;
	frameActive = false;
	initialized = false;
	context = nullptr;
	userEnabled.store(false, std::memory_order_release);
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
}

void Profiler::SetUserEnabled(bool a_enabled)
{
	userEnabled.store(a_enabled, std::memory_order_release);
	if (!a_enabled) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
	}
}

void Profiler::RequestCapture()
{
	if (!IsUserEnabled())
		return;

	captureRequested.store(true, std::memory_order_release);
}

void Profiler::ClearTimers()
{
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	activeCpuTimers.clear();
	completedCpuTimers.clear();
	totalGpuHistory = {};
	totalCpuHistory = {};
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	ResetWholeFrameTimings();

	for (auto& frame : frames) {
		ResetFrameState(frame);
	}
}

void Profiler::ClearTimersForFeature(const std::string& featureName)
{
	std::string prefix = featureName + "::";
	std::erase_if(knownTimers, [&prefix](const KnownTimer& kt) {
		return kt.name.starts_with(prefix);
	});
	std::erase_if(results, [&prefix](const TimerResult& result) {
		return result.name.starts_with(prefix);
	});
	std::erase_if(completedCpuTimers, [&prefix](const CompletedCpuTimer& timer) {
		return timer.name.starts_with(prefix);
	});
	for (auto& timer : activeCpuTimers) {
		if (timer.name.starts_with(prefix)) {
			timer.name.clear();
		}
	}
	for (auto& frame : frames) {
		std::erase_if(frame.cpuTimers, [&prefix](const CompletedCpuTimer& timer) {
			return timer.name.starts_with(prefix);
		});
		for (auto& timer : frame.timers) {
			if (timer.name.starts_with(prefix)) {
				timer.name.clear();
			}
		}
	}

	knownTimerIndex.clear();
	for (size_t i = 0; i < knownTimers.size(); i++) {
		knownTimerIndex[knownTimers[i].name] = i;
	}
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive || !captureActive.load(std::memory_order_acquire))
		return;

	if (!CollectResults())
		return;

	auto& frame = frames[writeFrame];
	ResetFrameState(frame);
	frame.inFlight = true;
	frameActive = true;
	context->Begin(frame.disjoint.get());
	if (wholeFrameGpuTimingAvailable)
		context->End(frame.wholeFrameBegin.get());
	QueryPerformanceCounter(&frame.wholeFrameCpuBegin);
}

bool Profiler::BeginPass(std::string_view name)
{
	if (!initialized || !context)
		return false;
	if (!captureActive.load(std::memory_order_acquire))
		return false;

	if (!frameActive)
		return false;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return false;

	const uint32_t timerIndex = frame.activeCount++;
	auto& timer = frame.timers[timerIndex];
	timer.name.assign(name);
	timer.cpuMs = 0.0f;
	timer.ended = false;
	context->End(timer.begin.get());
	QueryPerformanceCounter(&timer.cpuBegin);
	frame.activeTimerStack.push_back(timerIndex);

	if (beginPerfEvent)
		beginPerfEvent(name);

	return true;
}

void Profiler::EndPass()
{
	if (!initialized || !context || !frameActive)
		return;

	auto& frame = frames[writeFrame];
	if (frame.activeTimerStack.empty())
		return;

	const uint32_t timerIndex = frame.activeTimerStack.back();
	frame.activeTimerStack.pop_back();
	if (timerIndex >= frame.activeCount || timerIndex >= frame.timers.size())
		return;

	auto& timer = frame.timers[timerIndex];

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);
	timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);

	context->End(timer.end.get());
	timer.ended = true;

	if (endPerfEvent)
		endPerfEvent({});
}

bool Profiler::BeginCpuPass(std::string_view name)
{
	if (!initialized || !frameActive)
		return false;
	if (!captureActive.load(std::memory_order_acquire))
		return false;

	if (activeCpuTimers.size() + completedCpuTimers.size() >= kMaxTimers)
		return false;

	auto& timer = activeCpuTimers.emplace_back();
	timer.name.assign(name);
	QueryPerformanceCounter(&timer.cpuBegin);
	return true;
}

void Profiler::EndCpuPass()
{
	if (!initialized || activeCpuTimers.empty())
		return;

	auto timer = std::move(activeCpuTimers.back());
	activeCpuTimers.pop_back();

	if (timer.name.empty())
		return;

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);

	CompletedCpuTimer completed;
	completed.name = std::move(timer.name);
	completed.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);
	completedCpuTimers.push_back(std::move(completed));
}

void Profiler::EndFrame(UINT syncInterval)
{
	if (!initialized || !context) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
		return;
	}

	LARGE_INTEGER presentStart{};
	QueryPerformanceCounter(&presentStart);
	const bool hasFrameTime = wholeFrameHasLastPresentStart && presentStart.QuadPart > wholeFrameLastPresentStartCounter;
	const float frameTimeMs = hasFrameTime ?
	                              static_cast<float>(static_cast<double>(presentStart.QuadPart - wholeFrameLastPresentStartCounter) * cpuTicksToMs) :
	                              0.0f;
	const bool framePresentSynced = wholeFrameLastPresentSynced;
	wholeFrameLastPresentStartCounter = presentStart.QuadPart;
	wholeFrameHasLastPresentStart = true;
	wholeFrameLastPresentSynced = syncInterval != 0;

	auto& frame = frames[writeFrame];
	const bool hasCpuTimers = !completedCpuTimers.empty();
	if (!IsUserEnabled() && !frameActive && !hasCpuTimers) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
		return;
	}
	if (!frameActive) {
		const bool slotAvailable = CollectResults();
		if (!slotAvailable) {
			completedCpuTimers.clear();
			captureActive.store(captureRequested.exchange(false, std::memory_order_acq_rel), std::memory_order_release);
			return;
		}
	}
	if (!frameActive && !hasCpuTimers) {
		captureActive.store(captureRequested.exchange(false, std::memory_order_acq_rel), std::memory_order_release);
		return;
	}

	if (frameActive) {
		frameActive = false;
		frame.wholeFrameCpuMs = static_cast<float>(
			static_cast<double>(presentStart.QuadPart - frame.wholeFrameCpuBegin.QuadPart) * cpuTicksToMs);
		frame.hasWholeFrameCpuTime = IsValidWholeFrameSample(frame.wholeFrameCpuMs);
		frame.frameTimeMs = frameTimeMs;
		frame.hasFrameTime = hasFrameTime && IsValidWholeFrameSample(frameTimeMs);
		frame.presentSynced = framePresentSynced;
		if (wholeFrameGpuTimingAvailable)
			context->End(frame.wholeFrameEnd.get());
		context->End(frame.disjoint.get());
	} else {
		ResetFrameState(frame);
	}

	StoreCompletedCpuTimers(frame);

	writeFrame = (writeFrame + 1) % kFrameLatency;
	framesSinceInit++;
	captureActive.store(captureRequested.exchange(false, std::memory_order_acq_rel), std::memory_order_release);
}

bool Profiler::CollectResults()
{
	if (framesSinceInit < kFrameLatency)
		return true;

	readFrame = writeFrame;
	auto& frame = frames[readFrame];
	if (!HasPendingFrameData(frame))
		return true;

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
	std::unordered_map<std::string, ActiveTimerData> activeTimers;
	float activeTotalMs = 0.0f;
	float activeCpuTotalMs = 0.0f;
	float wholeFrameGpuMs = 0.0f;
	bool hasGpuFrameData = false;
	bool gpuFrameComplete = false;
	bool hasWholeFrameGpuTime = false;
	bool gpuTimerQueriesComplete = true;
	const bool hasCompletedCpuTimers = !frame.cpuTimers.empty();

	if (frame.inFlight) {
		HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr != S_OK)
			return false;
		hasGpuFrameData = true;
	}

	if (hasGpuFrameData && !disjointData.Disjoint && disjointData.Frequency > 0) {
		double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);

		if (wholeFrameGpuTimingAvailable) {
			UINT64 wholeFrameBegin = 0;
			UINT64 wholeFrameEnd = 0;
			const HRESULT beginResult = context->GetData(
				frame.wholeFrameBegin.get(),
				&wholeFrameBegin,
				sizeof(wholeFrameBegin),
				D3D11_ASYNC_GETDATA_DONOTFLUSH);
			const HRESULT endResult = context->GetData(
				frame.wholeFrameEnd.get(),
				&wholeFrameEnd,
				sizeof(wholeFrameEnd),
				D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (beginResult == S_FALSE || endResult == S_FALSE)
				return false;
			if (beginResult == S_OK && endResult == S_OK && wholeFrameEnd >= wholeFrameBegin) {
				wholeFrameGpuMs = static_cast<float>(static_cast<double>(wholeFrameEnd - wholeFrameBegin) * ticksToMs);
				hasWholeFrameGpuTime = IsValidWholeFrameSample(wholeFrameGpuMs);
			}
		}

		for (uint32_t i = 0; i < frame.activeCount; i++) {
			auto& timer = frame.timers[i];
			if (timer.name.empty() || !timer.ended)
				continue;

			UINT64 tsBegin = 0, tsEnd = 0;

			if (context->GetData(timer.begin.get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
				gpuTimerQueriesComplete = false;
				continue;
			}
			if (context->GetData(timer.end.get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
				gpuTimerQueriesComplete = false;
				continue;
			}
			if (tsEnd < tsBegin)
				continue;

			float ms = static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs);
			if (!IsValidProfilerSample(ms) || !IsValidProfilerSample(timer.cpuMs))
				continue;

			auto& entry = activeTimers[timer.name];
			entry.gpuMs += ms;
			entry.cpuMs += timer.cpuMs;
			entry.hasGpu = true;
			entry.hasCpu = true;
			activeTotalMs += ms;
			activeCpuTotalMs += timer.cpuMs;
		}

		if (!gpuTimerQueriesComplete)
			return false;

		gpuFrameComplete = true;
	}
	if (hasGpuFrameData)
		frame.inFlight = false;

	for (const auto& timer : frame.cpuTimers) {
		if (timer.name.empty())
			continue;
		if (!IsValidProfilerSample(timer.cpuMs))
			continue;

		auto& entry = activeTimers[timer.name];
		entry.cpuMs += timer.cpuMs;
		entry.hasCpu = true;
		activeCpuTotalMs += timer.cpuMs;
	}

	for (const auto& [name, active] : activeTimers) {
		auto& known = GetOrCreateTimer(name);
		if (active.hasGpu) {
			known.hasGpu = true;
			known.gpu.PushSample(active.gpuMs);
		}
		if (active.hasCpu) {
			known.hasCpu = true;
			known.cpu.PushSample(active.cpuMs);
		}
	}

	if (gpuFrameComplete) {
		for (auto& known : knownTimers) {
			auto it = activeTimers.find(known.name);
			const bool activeGpu = it != activeTimers.end() && it->second.hasGpu;
			const bool activeCpu = it != activeTimers.end() && it->second.hasCpu;

			if (known.hasGpu && !activeGpu)
				known.gpu.PushSample(0.0f);
			if (known.hasCpu && !activeCpu)
				known.cpu.PushSample(0.0f);
		}
	} else if (hasCompletedCpuTimers) {
		for (auto& known : knownTimers) {
			auto it = activeTimers.find(known.name);
			if (known.hasCpu && (it == activeTimers.end() || !it->second.hasCpu))
				known.cpu.PushSample(0.0f);
		}
	}

	totalTimeMs = activeTotalMs;
	cpuTotalTimeMs = activeCpuTotalMs;
	if (gpuFrameComplete)
		totalGpuHistory.PushSample(activeTotalMs);
	if (gpuFrameComplete || hasCompletedCpuTimers)
		totalCpuHistory.PushSample(activeCpuTotalMs);

	const bool hasWholeFrameCpuTime = frame.hasWholeFrameCpuTime && IsValidWholeFrameSample(frame.wholeFrameCpuMs);
	const bool hasFrameTime = frame.hasFrameTime && IsValidWholeFrameSample(frame.frameTimeMs);
	if (hasWholeFrameGpuTime || hasWholeFrameCpuTime || hasFrameTime) {
		const uint64_t sampleId = ++wholeFrameSampleId;
		if (hasWholeFrameGpuTime) {
			wholeFrameGpuHistory.PushSample(wholeFrameGpuMs);
			wholeFrameGpuSampleId = sampleId;
		}
		if (hasWholeFrameCpuTime) {
			wholeFrameCpuHistory.PushSample(frame.wholeFrameCpuMs);
			wholeFrameCpuSampleId = sampleId;
		}
		if (hasFrameTime) {
			frameTimeHistory.PushSample(frame.frameTimeMs);
			frameTimeSampleId = sampleId;
			latestFrameWasPresentSynced = frame.presentSynced;
		}
	}

	RebuildResults(&activeTimers);
	ResetFrameState(frame);
	return true;
}

Profiler::KnownTimer& Profiler::GetOrCreateTimer(const std::string& name)
{
	auto [it, inserted] = knownTimerIndex.try_emplace(name, knownTimers.size());
	if (inserted) {
		KnownTimer kt;
		kt.name = name;
		knownTimers.push_back(std::move(kt));
	}
	return knownTimers[it->second];
}

void Profiler::StoreCompletedCpuTimers(FrameQueries& frame)
{
	frame.cpuTimers.clear();
	frame.cpuTimers.reserve(completedCpuTimers.size());
	for (auto& timer : completedCpuTimers) {
		frame.cpuTimers.push_back(std::move(timer));
	}
	completedCpuTimers.clear();
}

void Profiler::RebuildResults(const std::unordered_map<std::string, ActiveTimerData>* activeTimers)
{
	results.clear();
	results.reserve(knownTimers.size());
	for (const auto& known : knownTimers) {
		TimerResult result;
		result.name = known.name;
		result.hasGpu = known.hasGpu;
		result.hasCpu = known.hasCpu;

		if (activeTimers) {
			auto it = activeTimers->find(known.name);
			if (it != activeTimers->end()) {
				result.gpuTimeMs = it->second.hasGpu ? it->second.gpuMs : known.gpu.lastMs;
				result.cpuTimeMs = it->second.hasCpu ? it->second.cpuMs : known.cpu.lastMs;
			} else {
				result.gpuTimeMs = known.gpu.lastMs;
				result.cpuTimeMs = known.cpu.lastMs;
			}
		} else {
			result.gpuTimeMs = known.gpu.lastMs;
			result.cpuTimeMs = known.cpu.lastMs;
		}

		if (known.hasGpu) {
			result.avgMs = known.gpu.GetAverage();
			result.p95Ms = known.gpu.GetPercentile(95.0f);
			result.p99Ms = known.gpu.GetPercentile(99.0f);
			result.historyBuffer = known.gpu.history;
			result.historyHead = known.gpu.head;
			result.historyCount = known.gpu.count;
		}

		if (known.hasCpu) {
			result.cpuAvgMs = known.cpu.GetAverage();
			result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
			result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
			result.cpuHistoryBuffer = known.cpu.history;
			result.cpuHistoryHead = known.cpu.head;
			result.cpuHistoryCount = known.cpu.count;
		}

		result.valid = result.hasGpu || result.hasCpu;
		results.push_back(std::move(result));
	}
}
