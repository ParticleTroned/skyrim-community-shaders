#include "Profiler.h"

#include <algorithm>
#include <unordered_map>

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
	frame.inFlight = false;
	frame.cpuTimers.clear();
}

bool Profiler::HasPendingFrameData(const FrameQueries& frame)
{
	return frame.inFlight || !frame.cpuTimers.empty();
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
	initialized = true;
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
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
	frameActive = false;
	initialized = false;
	context = nullptr;
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
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
}

bool Profiler::BeginPass(std::string_view name)
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
	if (frame.activeCount >= kMaxTimers)
		return false;

	auto& timer = frame.timers[frame.activeCount];
	timer.name.assign(name);
	context->End(timer.begin.get());
	QueryPerformanceCounter(&timer.cpuBegin);

	if (beginPerfEvent)
		beginPerfEvent(name);

	return true;
}

void Profiler::EndPass()
{
	if (!initialized || !context || !frameActive)
		return;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);
	timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);

	context->End(timer.end.get());
	frame.activeCount++;

	if (endPerfEvent)
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

void Profiler::EndFrame()
{
	if (!initialized || !context) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
		return;
	}

	auto& frame = frames[writeFrame];
	const bool hasCpuTimers = !completedCpuTimers.empty();
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

	if (frame.inFlight) {
		HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr != S_OK)
			return false;
		frame.inFlight = false;
	}

	if (!disjointData.Disjoint && frame.activeCount > 0) {
		double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);

		for (uint32_t i = 0; i < frame.activeCount; i++) {
			auto& timer = frame.timers[i];
			if (timer.name.empty())
				continue;

			UINT64 tsBegin = 0, tsEnd = 0;

			if (context->GetData(timer.begin.get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			if (context->GetData(timer.end.get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;

			float ms = static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs);
			auto& entry = activeTimers[timer.name];
			entry.gpuMs += ms;
			entry.cpuMs += timer.cpuMs;
			entry.hasGpu = true;
			entry.hasCpu = true;
			activeTotalMs += ms;
			activeCpuTotalMs += timer.cpuMs;

			auto& known = GetOrCreateTimer(timer.name);
			known.hasGpu = true;
			known.hasCpu = true;
			known.gpu.PushSample(ms);
			known.cpu.PushSample(timer.cpuMs);
		}
	}

	for (const auto& timer : frame.cpuTimers) {
		if (timer.name.empty())
			continue;

		auto& entry = activeTimers[timer.name];
		entry.cpuMs += timer.cpuMs;
		entry.hasCpu = true;
		activeCpuTotalMs += timer.cpuMs;

		auto& known = GetOrCreateTimer(timer.name);
		known.hasCpu = true;
		known.cpu.PushSample(timer.cpuMs);
	}

	frame.cpuTimers.clear();
	frame.activeCount = 0;

	totalTimeMs = activeTotalMs;
	cpuTotalTimeMs = activeCpuTotalMs;

	RebuildResults(&activeTimers);
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
