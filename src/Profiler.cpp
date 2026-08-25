#include "Profiler.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
	constexpr float kMaxSaneProfilerSampleMs = 1000.0f;

	bool IsValidProfilerSample(float ms)
	{
		return std::isfinite(ms) && ms >= 0.0f && ms <= kMaxSaneProfilerSampleMs;
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
	frame.inFlight = false;
	frame.cpuTimers.clear();
	frame.captureSessionId = 0;
}

bool Profiler::HasPendingFrameData(const FrameQueries& frame)
{
	return frame.inFlight || !frame.cpuTimers.empty();
}

void Profiler::ResetPendingFrames()
{
	for (uint32_t i = 0; i < kFrameLatency; i++) {
		// An open GPU scope still relies on the current slot's query stack.
		if (frameActive && i == writeFrame)
			continue;
		ResetFrameState(frames[i]);
	}
}

void Profiler::Initialize(ID3D11Device* device, ID3D11DeviceContext* a_context)
{
	Release();

	context = a_context;

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	cpuTicksToMs = 1000.0 / static_cast<double>(freq.QuadPart);

	for (auto& frame : frames) {
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		device->CreateQuery(&disjointDesc, frame.disjoint.put());

		frame.timers.resize(kMaxTimers);
		frame.activeTimerStack.reserve(kMaxTimers);
		for (auto& timer : frame.timers) {
			D3D11_QUERY_DESC tsDesc{};
			tsDesc.Query = D3D11_QUERY_TIMESTAMP;
			device->CreateQuery(&tsDesc, timer.begin.put());
			device->CreateQuery(&tsDesc, timer.end.put());
		}
		frame.cpuTimers.reserve(kMaxTimers);
		ResetFrameState(frame);
	}

	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	frameActive = false;
	resolvedTotalMs = 0.0f;
	resolvedCpuTotalMs = 0.0f;
	capturedFrameCount = 0;
	acquiredSlotsThisFrame = 0;
	acquiredSlots = 0;
	peakAcquiredSlots = 0;
	slotRefusals = 0;
	initialized = true;
	// Preserve the user's preference across renderer/device reinitialization.
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
	boundedCapture = {};
	boundedCaptureTimers.clear();
	boundedCaptureTimerIndex.clear();
	boundedCaptureResults.clear();
}

void Profiler::Release()
{
	for (auto& frame : frames) {
		frame.disjoint = nullptr;
		frame.timers.clear();
		frame.cpuTimers.clear();
		frame.activeCount = 0;
		frame.inFlight = false;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	activeCpuTimers.clear();
	completedCpuTimers.clear();
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	resolvedTotalMs = 0.0f;
	resolvedCpuTotalMs = 0.0f;
	capturedFrameCount = 0;
	acquiredSlotsThisFrame = 0;
	acquiredSlots = 0;
	peakAcquiredSlots = 0;
	slotRefusals = 0;
	frameActive = false;
	initialized = false;
	context = nullptr;
	// Preserve userEnabled; Initialize() reuses it after device recreation.
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
	boundedCapture = {};
	boundedCaptureTimers.clear();
	boundedCaptureTimerIndex.clear();
	boundedCaptureResults.clear();
}

void Profiler::SetUserEnabled(bool a_enabled)
{
	userEnabled.store(a_enabled, std::memory_order_release);
	if (!a_enabled) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
		if (boundedCapture.state == CaptureSessionState::Running)
			boundedCapture.state = CaptureSessionState::Cancelled;
	}
}

void Profiler::RequestCapture()
{
	if (!IsUserEnabled())
		return;

	captureRequested.store(true, std::memory_order_release);
}

bool Profiler::StartBoundedCapture(uint32_t a_frameCount, bool a_clearHistory, uint64_t& a_sessionId)
{
	a_sessionId = 0;
	if (!initialized || !IsUserEnabled() || a_frameCount == 0 || a_frameCount > kHistorySize ||
		boundedCapture.state == CaptureSessionState::Running) {
		return false;
	}

	if (a_clearHistory)
		ClearTimers();

	boundedCapture = {
		.sessionId = nextCaptureSessionId++,
		.state = CaptureSessionState::Running,
		.requestedFrames = a_frameCount,
	};
	boundedCaptureTimers.clear();
	boundedCaptureTimerIndex.clear();
	boundedCaptureResults.clear();
	a_sessionId = boundedCapture.sessionId;
	RequestCapture();
	return true;
}

bool Profiler::CancelBoundedCapture(uint64_t a_sessionId)
{
	if (boundedCapture.state != CaptureSessionState::Running || boundedCapture.sessionId != a_sessionId)
		return false;
	boundedCapture.state = CaptureSessionState::Cancelled;
	return true;
}

Profiler::CaptureSessionProgress Profiler::GetBoundedCaptureProgress() const
{
	return boundedCapture;
}

const std::vector<Profiler::TimerResult>* Profiler::GetBoundedCaptureResults(uint64_t a_sessionId) const
{
	if (boundedCapture.sessionId == 0 || boundedCapture.sessionId != a_sessionId)
		return nullptr;
	return &boundedCaptureResults;
}

void Profiler::ClearTimers()
{
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	activeCpuTimers.clear();
	completedCpuTimers.clear();
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	resolvedTotalMs = 0.0f;
	resolvedCpuTotalMs = 0.0f;
	capturedFrameCount = 0;
	if (!frameActive)
		acquiredSlotsThisFrame = 0;
	acquiredSlots = 0;

	ResetPendingFrames();
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
	if (boundedCapture.state == CaptureSessionState::Running &&
		boundedCapture.submittedFrames < boundedCapture.requestedFrames) {
		frame.captureSessionId = boundedCapture.sessionId;
		boundedCapture.submittedFrames++;
	}
	frameActive = true;
	acquiredSlotsThisFrame = 0;
	context->Begin(frame.disjoint.get());
}

bool Profiler::BeginPass(std::string_view name, bool fireCallbacks)
{
	if (!initialized || !context)
		return false;
	if (!captureActive.load(std::memory_order_acquire))
		return false;

	if (!frameActive)
		BeginFrame();
	if (!frameActive)
		return false;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers) {
		slotRefusals++;
		return false;
	}

	const uint32_t timerIndex = frame.activeCount++;
	acquiredSlotsThisFrame++;
	auto& timer = frame.timers[timerIndex];
	timer.name.assign(name);
	timer.cpuMs = 0.0f;
	timer.depth = static_cast<uint32_t>(frame.activeTimerStack.size());
	timer.ended = false;
	context->End(timer.begin.get());
	QueryPerformanceCounter(&timer.cpuBegin);
	frame.activeTimerStack.push_back(timerIndex);

	if (fireCallbacks && beginPerfEvent)
		beginPerfEvent(name);

	return true;
}

void Profiler::EndPass(bool fireCallbacks)
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

	if (fireCallbacks && endPerfEvent)
		endPerfEvent({});
}

bool Profiler::BeginCpuPass(std::string_view name)
{
	if (!initialized)
		return false;
	if (!captureActive.load(std::memory_order_acquire))
		return false;

	if (activeCpuTimers.size() + completedCpuTimers.size() >= kMaxTimers)
		return false;

	auto& timer = activeCpuTimers.emplace_back();
	timer.name.assign(name);
	const bool insideGpuPass = frameActive && !frames[writeFrame].activeTimerStack.empty();
	timer.depth = static_cast<uint32_t>(activeCpuTimers.size() - 1) + (insideGpuPass ? 1u : 0u);
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
	completed.depth = timer.depth;
	if (!IsValidProfilerSample(completed.cpuMs))
		return;
	completedCpuTimers.push_back(std::move(completed));
}

void Profiler::EndFrame(uint32_t a_frameCount)
{
	if (!initialized || !context) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
		if (boundedCapture.state == CaptureSessionState::Running)
			boundedCapture.state = CaptureSessionState::Cancelled;
		activeCpuTimers.clear();
		completedCpuTimers.clear();
		return;
	}

	auto& frame = frames[writeFrame];
	const bool hasCpuTimers = !completedCpuTimers.empty();
	if (!IsUserEnabled() && !frameActive && !hasCpuTimers) {
		totalTimeMs = 0.0f;
		cpuTotalTimeMs = 0.0f;
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
		return;
	}
	if (!frameActive) {
		const bool slotAvailable = CollectResults();
		if (!slotAvailable) {
			if (boundedCapture.state == CaptureSessionState::Running &&
				boundedCapture.submittedFrames < boundedCapture.requestedFrames) {
				captureRequested.store(true, std::memory_order_release);
			}
			captureActive.store(captureRequested.exchange(false, std::memory_order_acq_rel), std::memory_order_release);
			return;
		}
	}
	if (!frameActive && !hasCpuTimers) {
		totalTimeMs = 0.0f;
		cpuTotalTimeMs = 0.0f;
		// Walk every leftover ring slot while capture is idle so an old
		// session cannot be replayed as the first samples of the next one.
		for (const auto& pendingFrame : frames) {
			if (HasPendingFrameData(pendingFrame)) {
				writeFrame = (writeFrame + 1) % kFrameLatency;
				break;
			}
		}
		if (boundedCapture.state == CaptureSessionState::Running &&
			boundedCapture.submittedFrames < boundedCapture.requestedFrames) {
			captureRequested.store(true, std::memory_order_release);
		}
		captureActive.store(captureRequested.exchange(false, std::memory_order_acq_rel), std::memory_order_release);
		return;
	}

	if (frameActive) {
		frameActive = false;
		context->End(frame.disjoint.get());
		acquiredSlots = acquiredSlotsThisFrame;
		peakAcquiredSlots = std::max(peakAcquiredSlots, acquiredSlotsThisFrame);
	} else {
		ResetFrameState(frame);
		acquiredSlots = 0;
	}

	StoreCompletedCpuTimers(frame);
	frame.capturedFrame = a_frameCount;
	// CPU-only instrumentation does not call BeginFrame(), so bind that query
	// set to the bounded session here. GPU query sets are bound at BeginFrame()
	// and can never be retroactively claimed by a session started mid-frame.
	if (!frameActive && hasCpuTimers && frame.captureSessionId == 0 &&
		boundedCapture.state == CaptureSessionState::Running &&
		boundedCapture.submittedFrames < boundedCapture.requestedFrames) {
		frame.captureSessionId = boundedCapture.sessionId;
		boundedCapture.submittedFrames++;
	}
	if (boundedCapture.state == CaptureSessionState::Running &&
		boundedCapture.submittedFrames < boundedCapture.requestedFrames)
		captureRequested.store(true, std::memory_order_release);

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
	bool gpuFrameResolved = false;
	const bool hadCpuTimers = !frame.cpuTimers.empty();

	if (frame.inFlight) {
		HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr == S_FALSE)
			return false;
		if (hr == S_OK && !disjointData.Disjoint && disjointData.Frequency > 0) {
			const double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);
			for (uint32_t i = 0; i < frame.activeCount; i++) {
				auto& timer = frame.timers[i];
				if (timer.name.empty() || !timer.ended)
					continue;

				UINT64 tsBegin = 0;
				UINT64 tsEnd = 0;
				const HRESULT beginHr = context->GetData(timer.begin.get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				const HRESULT endHr = context->GetData(timer.end.get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (beginHr == S_FALSE || endHr == S_FALSE)
					return false;

				// CPU and GPU validity are independent: a hitch on one side must
				// not discard a valid sample from the other.
				const bool gpuValid = beginHr == S_OK && endHr == S_OK && tsEnd >= tsBegin &&
				                      IsValidProfilerSample(static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs));
				const float gpuMs = gpuValid ? static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs) : 0.0f;
				const bool cpuValid = IsValidProfilerSample(timer.cpuMs);
				if (!gpuValid && !cpuValid)
					continue;

				// Repeated pass names accumulate here and enter history once below.
				auto& entry = activeTimers[timer.name];
				GetOrCreateTimer(timer.name);
				if (gpuValid) {
					entry.gpuMs += gpuMs;
					entry.hasGpu = true;
					if (timer.depth == 0) {
						activeTotalMs += gpuMs;
						entry.topLevelMs += gpuMs;
					}
				}
				if (cpuValid) {
					entry.cpuMs += timer.cpuMs;
					entry.hasCpu = true;
					if (timer.depth == 0)
						activeCpuTotalMs += timer.cpuMs;
				}
			}
			gpuFrameResolved = true;
		}
		frame.inFlight = false;
	}

	for (const auto& timer : frame.cpuTimers) {
		if (timer.name.empty())
			continue;
		if (!IsValidProfilerSample(timer.cpuMs))
			continue;

		auto& entry = activeTimers[timer.name];
		entry.cpuMs += timer.cpuMs;
		entry.hasCpu = true;
		if (timer.depth == 0)
			activeCpuTotalMs += timer.cpuMs;

		GetOrCreateTimer(timer.name);
	}

	// Exactly one history sample per named timer and resolved cycle keeps all
	// histories aligned for percentile-of-sum calculations in the UI.
	const bool cpuCycleResolved = gpuFrameResolved || hadCpuTimers;
	for (auto& known : knownTimers) {
		auto it = activeTimers.find(known.name);
		const bool freshGpu = it != activeTimers.end() && it->second.hasGpu;
		const bool freshCpu = it != activeTimers.end() && it->second.hasCpu;
		if (freshGpu) {
			known.hasGpu = true;
			known.gpu.PushSample(it->second.gpuMs);
		} else if (gpuFrameResolved && known.hasGpu) {
			known.gpu.PushSample(0.0f);
		}
		if (freshCpu) {
			known.hasCpu = true;
			known.cpu.PushSample(it->second.cpuMs);
		} else if (cpuCycleResolved && known.hasCpu) {
			known.cpu.PushSample(0.0f);
		}
	}

	frame.cpuTimers.clear();
	frame.activeCount = 0;
	frame.activeTimerStack.clear();

	totalTimeMs = activeTotalMs;
	cpuTotalTimeMs = activeCpuTotalMs;
	resolvedTotalMs = activeTotalMs;
	resolvedCpuTotalMs = activeCpuTotalMs;
	capturedFrameCount = frame.capturedFrame;
	if (boundedCapture.state == CaptureSessionState::Running &&
		frame.captureSessionId == boundedCapture.sessionId) {
		StoreBoundedCaptureResults(activeTimers, gpuFrameResolved, cpuCycleResolved);
		boundedCapture.resolvedFrames++;
		if (boundedCapture.resolvedFrames >= boundedCapture.requestedFrames)
			boundedCapture.state = CaptureSessionState::Completed;
	}

	RebuildResults(&activeTimers);
	return true;
}

void Profiler::StoreBoundedCaptureResults(
	const std::unordered_map<std::string, ActiveTimerData>& a_activeTimers,
	bool a_gpuCycleResolved,
	bool a_cpuCycleResolved)
{
	for (const auto& [name, active] : a_activeTimers) {
		auto [found, inserted] = boundedCaptureTimerIndex.try_emplace(name, boundedCaptureTimers.size());
		if (inserted) {
			CaptureKnownTimer timer;
			timer.name = name;
			boundedCaptureTimers.push_back(std::move(timer));
		}
		(void)active;
	}

	for (auto& timer : boundedCaptureTimers) {
		const auto found = a_activeTimers.find(timer.name);
		const bool freshGpu = found != a_activeTimers.end() && found->second.hasGpu;
		const bool freshCpu = found != a_activeTimers.end() && found->second.hasCpu;
		if (freshGpu) {
			timer.hasGpu = true;
			timer.topLevelMs = found->second.topLevelMs;
			timer.gpu.PushSample(found->second.gpuMs);
		} else if (a_gpuCycleResolved && timer.hasGpu) {
			timer.topLevelMs = 0.0f;
			timer.gpu.PushSample(0.0f);
		}
		if (freshCpu) {
			timer.hasCpu = true;
			timer.cpu.PushSample(found->second.cpuMs);
		} else if (a_cpuCycleResolved && timer.hasCpu) {
			timer.cpu.PushSample(0.0f);
		}
	}
	RebuildBoundedCaptureResults();
}

void Profiler::RebuildBoundedCaptureResults()
{
	boundedCaptureResults.clear();
	boundedCaptureResults.reserve(boundedCaptureTimers.size());
	for (const auto& known : boundedCaptureTimers) {
		TimerResult result;
		result.name = known.name;
		result.hasGpu = known.hasGpu;
		result.hasCpu = known.hasCpu;
		result.gpuTimeMs = known.gpu.lastMs;
		result.topLevelMs = known.topLevelMs;
		result.cpuTimeMs = known.cpu.lastMs;
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
		boundedCaptureResults.push_back(std::move(result));
	}
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

		const ActiveTimerData* activeTimer = nullptr;
		if (activeTimers) {
			auto it = activeTimers->find(known.name);
			if (it != activeTimers->end()) {
				activeTimer = &it->second;
			}
		}

		if (activeTimer && activeTimer->hasGpu) {
			result.activeGpu = true;
			result.gpuTimeMs = activeTimer->gpuMs;
			result.topLevelMs = activeTimer->topLevelMs;
		} else {
			result.gpuTimeMs = known.gpu.lastMs;
		}
		if (activeTimer && activeTimer->hasCpu) {
			result.activeCpu = true;
			result.cpuTimeMs = activeTimer->cpuMs;
		} else {
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
