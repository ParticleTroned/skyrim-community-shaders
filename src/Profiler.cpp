#include "Profiler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

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

	bool IsValidPresentInterval(float ms)
	{
		// Keep genuine ordinary hitches, but do not let a pause, loading stall, or
		// alt-tab gap become an in-scene frame-time sample.
		return std::isfinite(ms) &&
		       ms > 0.0f &&
		       ms <= Profiler::kMaxPresentIntervalMs;
	}

	void IncrementSaturating(uint64_t& value)
	{
		if (value != std::numeric_limits<uint64_t>::max())
			++value;
	}

	Profiler::CaptureMode MinCaptureMode(
		Profiler::CaptureMode lhs,
		Profiler::CaptureMode rhs)
	{
		return static_cast<uint8_t>(lhs) <= static_cast<uint8_t>(rhs) ?
		           lhs :
		           rhs;
	}

	Profiler::CaptureMode MaxCaptureMode(
		Profiler::CaptureMode lhs,
		Profiler::CaptureMode rhs)
	{
		return static_cast<uint8_t>(lhs) >= static_cast<uint8_t>(rhs) ?
		           lhs :
		           rhs;
	}

	void ClampCaptureMode(
		std::atomic<Profiler::CaptureMode>& target,
		Profiler::CaptureMode limit)
	{
		auto current = target.load(std::memory_order_acquire);
		while (static_cast<uint8_t>(current) >
			       static_cast<uint8_t>(limit) &&
		       !target.compare_exchange_weak(
			       current,
			       limit,
			       std::memory_order_acq_rel,
			       std::memory_order_acquire)) {
		}
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
	frame.captureMode = CaptureMode::None;
	frame.wholeFrameCpuBegin = {};
	frame.wholeFrameCpuMs = 0.0f;
	frame.hasWholeFrameCpuBegin = false;
	frame.hasWholeFrameCpuTime = false;
	frame.acceptedPresent = false;
	frame.presentIntervalSampleId = 0;
	frame.inFlight = false;
	frame.cpuTimers.clear();
}

void Profiler::ResetWholeFrameTimings()
{
	RecordPresentDiscontinuity();
	wholeFrameGpuHistory = {};
	wholeFrameCpuHistory = {};
	presentIntervalHistory = {};
	wholeFrameSampleId = 0;
	wholeFrameGpuSampleId = 0;
	wholeFrameCpuSampleId = 0;
	wholeFramePresentIntervalSampleId = 0;
	presentIntervalSampleId = 0;
	lastAcceptedPresentStartCounter = 0;
	hasLastAcceptedPresentStart = false;
	lastAcceptedPresentWasSynced = false;
	latestPresentIntervalWasSynced = false;
	pendingPresentStart = {};
	pendingPresentSyncInterval = 0;
	pendingPresentFrame = 0;
	presentPending = false;
	pendingPresentIsReal = false;
	pendingPresentHasTimestamp = false;
	pendingPresentHasProfileFrame = false;
}

void Profiler::RecordPresentDiscontinuity()
{
	IncrementSaturating(presentDiscontinuityEpoch);
}

bool Profiler::HasPendingFrameData(const FrameQueries& frame)
{
	return frame.inFlight ||
	       frame.hasWholeFrameCpuTime ||
	       !frame.cpuTimers.empty();
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

	bool disjointTimingAvailable = true;
	wholeFrameGpuTimingAvailable = true;
	detailedPassGpuTimingAvailable = true;
	for (uint32_t frameIndex = 0; frameIndex < kFrameLatency; ++frameIndex) {
		auto& frame = frames[frameIndex];
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(device->CreateQuery(&disjointDesc, frame.disjoint.put())) || !frame.disjoint) {
			disjointTimingAvailable = false;
			logger::warn(
				"GPU profiler timing is unavailable because the D3D11 disjoint query for frame {} could not be created",
				frameIndex);
		}

		D3D11_QUERY_DESC timestampDesc{};
		timestampDesc.Query = D3D11_QUERY_TIMESTAMP;
		const HRESULT wholeFrameBeginResult = device->CreateQuery(&timestampDesc, frame.wholeFrameBegin.put());
		const HRESULT wholeFrameEndResult = device->CreateQuery(&timestampDesc, frame.wholeFrameEnd.put());
		wholeFrameGpuTimingAvailable = wholeFrameGpuTimingAvailable &&
		                               frame.disjoint &&
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
				detailedPassGpuTimingAvailable = false;
			}
		}
		frame.cpuTimers.reserve(kMaxTimers);
		ResetFrameState(frame);
	}
	if (!disjointTimingAvailable) {
		wholeFrameGpuTimingAvailable = false;
		detailedPassGpuTimingAvailable = false;
		for (auto& frame : frames) {
			frame.disjoint = nullptr;
			frame.wholeFrameBegin = nullptr;
			frame.wholeFrameEnd = nullptr;
			for (auto& timer : frame.timers) {
				timer.begin = nullptr;
				timer.end = nullptr;
			}
		}
	} else if (!wholeFrameGpuTimingAvailable) {
		for (auto& frame : frames) {
			frame.wholeFrameBegin = nullptr;
			frame.wholeFrameEnd = nullptr;
		}
		logger::warn("Whole-frame GPU timing is unavailable because D3D11 timestamp queries could not be created");
	}
	if (!detailedPassGpuTimingAvailable) {
		for (auto& frame : frames) {
			for (auto& timer : frame.timers) {
				timer.begin = nullptr;
				timer.end = nullptr;
			}
		}
		logger::warn("Detailed GPU pass timing is unavailable; direct Present and supported whole-frame timing remain active");
	}

	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	frameActive = false;
	initialized = true;
	userEnabled.store(false, std::memory_order_release);
	captureModeLimit.store(CaptureMode::DetailedPasses, std::memory_order_release);
	captureRequested.store(CaptureMode::None, std::memory_order_release);
	captureActive.store(CaptureMode::None, std::memory_order_release);
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
	collectedDetailedCycles = 0;
	activeCpuTimers.clear();
	completedCpuTimers.clear();
	profiledPassGpuTotalHistory = {};
	profiledPassCpuTotalHistory = {};
	profiledPassGpuTotalMs = 0.0f;
	profiledPassCpuTotalMs = 0.0f;
	ResetWholeFrameTimings();
	wholeFrameGpuTimingAvailable = false;
	detailedPassGpuTimingAvailable = false;
	frameActive = false;
	initialized = false;
	context = nullptr;
	userEnabled.store(false, std::memory_order_release);
	captureModeLimit.store(CaptureMode::DetailedPasses, std::memory_order_release);
	captureRequested.store(CaptureMode::None, std::memory_order_release);
	captureActive.store(CaptureMode::None, std::memory_order_release);
}

void Profiler::SetUserEnabled(bool a_enabled)
{
	userEnabled.store(a_enabled, std::memory_order_release);
	if (!a_enabled) {
		captureRequested.store(CaptureMode::None, std::memory_order_release);
		captureActive.store(CaptureMode::None, std::memory_order_release);
	}
}

void Profiler::SetCaptureModeLimit(CaptureMode a_mode)
{
	captureModeLimit.store(a_mode, std::memory_order_release);
	ClampCaptureMode(captureRequested, a_mode);
	ClampCaptureMode(captureActive, a_mode);
}

void Profiler::RequestCapture(CaptureMode a_mode)
{
	if (!IsUserEnabled())
		return;

	a_mode = MinCaptureMode(
		a_mode,
		captureModeLimit.load(std::memory_order_acquire));
	if (a_mode == CaptureMode::None)
		return;

	auto requested = captureRequested.load(std::memory_order_acquire);
	while (true) {
		const auto limit =
			captureModeLimit.load(std::memory_order_acquire);
		const auto desired =
			MinCaptureMode(MaxCaptureMode(requested, a_mode), limit);
		if (desired == requested)
			break;
		if (captureRequested.compare_exchange_weak(
			    requested,
			    desired,
			    std::memory_order_acq_rel,
			    std::memory_order_acquire)) {
			break;
		}
	}

	ClampCaptureMode(
		captureRequested,
		captureModeLimit.load(std::memory_order_acquire));
}

void Profiler::PromoteRequestedCapture()
{
	const CaptureMode requested =
		captureRequested.exchange(
			CaptureMode::None,
			std::memory_order_acq_rel);
	const CaptureMode promoted =
		IsUserEnabled() ?
			MinCaptureMode(
				requested,
				captureModeLimit.load(std::memory_order_acquire)) :
			CaptureMode::None;
	captureActive.store(promoted, std::memory_order_release);
	ClampCaptureMode(
		captureActive,
		captureModeLimit.load(std::memory_order_acquire));
}

void Profiler::ClearTimers()
{
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	collectedDetailedCycles = 0;
	activeCpuTimers.clear();
	completedCpuTimers.clear();
	profiledPassGpuTotalHistory = {};
	profiledPassCpuTotalHistory = {};
	profiledPassGpuTotalMs = 0.0f;
	profiledPassCpuTotalMs = 0.0f;
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

	RebuildTimerIndex();
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive || !IsWholeFrameCaptureActive())
		return;

	if (!CollectResults()) {
		IncrementSaturating(skippedWholeFrameCaptureCount);
		return;
	}

	auto& frame = frames[writeFrame];
	ResetFrameState(frame);
	frame.captureMode = captureActive.load(std::memory_order_acquire);
	frame.inFlight = frame.disjoint != nullptr;
	frameActive = true;
	if (frame.inFlight) {
		context->Begin(frame.disjoint.get());
		if (wholeFrameGpuTimingAvailable)
			context->End(frame.wholeFrameBegin.get());
	}
	frame.hasWholeFrameCpuBegin =
		QueryPerformanceCounter(&frame.wholeFrameCpuBegin) &&
		frame.wholeFrameCpuBegin.QuadPart > 0;
}

bool Profiler::BeginPass(std::string_view name)
{
	if (!initialized || !context)
		return false;
	if (!IsDetailedCaptureActive())
		return false;

	if (!frameActive)
		return false;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return false;

	const uint32_t timerIndex = frame.activeCount++;
	auto& timer = frame.timers[timerIndex];
	timer.name.assign(name);
	timer.cpuBegin = {};
	timer.cpuMs = 0.0f;
	timer.hasCpuBegin =
		QueryPerformanceCounter(&timer.cpuBegin) &&
		timer.cpuBegin.QuadPart > 0;
	timer.hasCpuTime = false;
	timer.ended = false;
	if (detailedPassGpuTimingAvailable)
		context->End(timer.begin.get());
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

	LARGE_INTEGER cpuEnd{};
	if (timer.hasCpuBegin &&
	    QueryPerformanceCounter(&cpuEnd) &&
	    cpuEnd.QuadPart >= timer.cpuBegin.QuadPart) {
		timer.cpuMs = static_cast<float>(
			static_cast<double>(
				cpuEnd.QuadPart -
				timer.cpuBegin.QuadPart) *
			cpuTicksToMs);
		timer.hasCpuTime = true;
	}

	if (detailedPassGpuTimingAvailable)
		context->End(timer.end.get());
	timer.ended = true;

	if (endPerfEvent)
		endPerfEvent({});
}

bool Profiler::BeginCpuPass(std::string_view name)
{
	if (!initialized || !frameActive)
		return false;
	if (!IsDetailedCaptureActive())
		return false;

	if (activeCpuTimers.size() + completedCpuTimers.size() >= kMaxTimers)
		return false;

	auto& timer = activeCpuTimers.emplace_back();
	timer.name.assign(name);
	timer.cpuBegin = {};
	if (!QueryPerformanceCounter(&timer.cpuBegin) ||
	    timer.cpuBegin.QuadPart <= 0) {
		CompletedCpuTimer invalidTimer;
		invalidTimer.name = std::move(timer.name);
		completedCpuTimers.push_back(
			std::move(invalidTimer));
		activeCpuTimers.pop_back();
		return false;
	}
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

	CompletedCpuTimer completed;
	completed.name = std::move(timer.name);
	LARGE_INTEGER cpuEnd{};
	if (QueryPerformanceCounter(&cpuEnd) &&
	    cpuEnd.QuadPart >= timer.cpuBegin.QuadPart) {
		completed.cpuMs = static_cast<float>(
			static_cast<double>(
				cpuEnd.QuadPart -
				timer.cpuBegin.QuadPart) *
			cpuTicksToMs);
		completed.hasCpuTime = true;
	}
	completedCpuTimers.push_back(std::move(completed));
}

void Profiler::BeginPresent(UINT syncInterval, UINT presentFlags)
{
	if (!initialized || !context || presentPending)
		return;

	presentPending = true;
	pendingPresentSyncInterval = syncInterval;
	pendingPresentIsReal = (presentFlags & DXGI_PRESENT_TEST) == 0;
	pendingPresentHasTimestamp = false;
	pendingPresentHasProfileFrame = false;
	pendingPresentStart = {};

	// A test Present is not a display boundary and must not close or rotate the active
	// profiling frame. CompletePresent will simply clear this pending call.
	if (!pendingPresentIsReal)
		return;

	pendingPresentHasTimestamp =
		QueryPerformanceCounter(&pendingPresentStart) &&
		pendingPresentStart.QuadPart > 0;

	auto& frame = frames[writeFrame];
	const bool hasCpuTimers = !completedCpuTimers.empty();
	if (!frameActive && !hasCpuTimers)
		return;

	if (!frameActive) {
		const bool slotAvailable = CollectResults();
		if (!slotAvailable) {
			completedCpuTimers.clear();
			return;
		}
	}

	if (frameActive) {
		frameActive = false;
		if (pendingPresentHasTimestamp &&
		    frame.hasWholeFrameCpuBegin) {
			frame.wholeFrameCpuMs = static_cast<float>(
				static_cast<double>(pendingPresentStart.QuadPart - frame.wholeFrameCpuBegin.QuadPart) * cpuTicksToMs);
			frame.hasWholeFrameCpuTime =
				IsValidWholeFrameSample(frame.wholeFrameCpuMs);
		}
		if (frame.inFlight) {
			if (wholeFrameGpuTimingAvailable)
				context->End(frame.wholeFrameEnd.get());
			context->End(frame.disjoint.get());
		}
	} else {
		ResetFrameState(frame);
	}

	StoreCompletedCpuTimers(frame);
	pendingPresentFrame = writeFrame;
	pendingPresentHasProfileFrame =
		frame.inFlight ||
		frame.hasWholeFrameCpuTime ||
		!frame.cpuTimers.empty();
}

void Profiler::CompletePresent(HRESULT presentResult)
{
	if (!presentPending)
		return;

	const bool attemptedRealPresent = pendingPresentIsReal;
	const bool acceptedPresentBoundary =
		attemptedRealPresent &&
		SUCCEEDED(presentResult) &&
		presentResult != DXGI_STATUS_OCCLUDED;
	const bool acceptedTimedPresent =
		acceptedPresentBoundary && pendingPresentHasTimestamp;

	uint64_t completedPresentIntervalSampleId = 0;
	bool presentSourceDiscontinuous = false;
	if (acceptedTimedPresent) {
		if (hasLastAcceptedPresentStart) {
			if (pendingPresentStart.QuadPart > lastAcceptedPresentStartCounter) {
				const float presentIntervalMs = static_cast<float>(
					static_cast<double>(pendingPresentStart.QuadPart - lastAcceptedPresentStartCounter) * cpuTicksToMs);
				if (IsValidPresentInterval(presentIntervalMs)) {
					presentIntervalHistory.PushSample(presentIntervalMs);
					completedPresentIntervalSampleId = ++presentIntervalSampleId;
					latestPresentIntervalWasSynced = lastAcceptedPresentWasSynced;
				} else {
					presentSourceDiscontinuous = true;
				}
			} else {
				// QueryPerformanceCounter should be monotonic. A rollback means the
				// direct timing source must be re-baselined before another sample.
				presentSourceDiscontinuous = true;
			}
		}

		lastAcceptedPresentStartCounter = pendingPresentStart.QuadPart;
		hasLastAcceptedPresentStart = true;
		lastAcceptedPresentWasSynced = pendingPresentSyncInterval != 0;
	} else if (attemptedRealPresent) {
		// Do not let the first successful Present after a failure or occlusion inherit
		// an interval that crossed the rejected display boundary.
		lastAcceptedPresentStartCounter = 0;
		hasLastAcceptedPresentStart = false;
		lastAcceptedPresentWasSynced = false;
		presentSourceDiscontinuous = true;
	}
	if (presentSourceDiscontinuous)
		RecordPresentDiscontinuity();

	if (pendingPresentHasProfileFrame) {
		auto& frame = frames[pendingPresentFrame];
		frame.acceptedPresent = acceptedPresentBoundary;
		frame.presentIntervalSampleId = completedPresentIntervalSampleId;
		writeFrame = (writeFrame + 1) % kFrameLatency;
		if (framesSinceInit < kFrameLatency)
			framesSinceInit++;
	}

	pendingPresentStart = {};
	pendingPresentSyncInterval = 0;
	presentPending = false;
	pendingPresentIsReal = false;
	pendingPresentHasTimestamp = false;
	pendingPresentHasProfileFrame = false;

	if (attemptedRealPresent)
		PromoteRequestedCapture();
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
	std::unordered_set<std::string> invalidGpuTimers;
	std::unordered_set<std::string> invalidCpuTimers;
	float activeTotalMs = 0.0f;
	float activeCpuTotalMs = 0.0f;
	float wholeFrameGpuMs = 0.0f;
	bool hasWholeFrameGpuTime = false;
	bool gpuFrameComplete = false;
	double gpuTicksToMs = 0.0;
	const bool hasCompletedCpuTimers = !frame.cpuTimers.empty();

	if (frame.inFlight) {
		const HRESULT hr = context->GetData(
			frame.disjoint.get(),
			&disjointData,
			sizeof(disjointData),
			D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr == S_FALSE)
			return false;
		if (hr == S_OK &&
		    !disjointData.Disjoint &&
		    disjointData.Frequency > 0) {
			gpuFrameComplete = true;
			gpuTicksToMs =
				1000.0 /
				static_cast<double>(disjointData.Frequency);
		} else {
			frame.inFlight = false;
		}
	}

	if (gpuFrameComplete) {
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
			if (beginResult == S_OK &&
			    endResult == S_OK &&
			    wholeFrameEnd >= wholeFrameBegin) {
				wholeFrameGpuMs = static_cast<float>(
					static_cast<double>(
						wholeFrameEnd -
						wholeFrameBegin) *
					gpuTicksToMs);
				hasWholeFrameGpuTime =
					IsValidWholeFrameSample(
						wholeFrameGpuMs);
			}
		}
	}

	for (uint32_t i = 0; i < frame.activeCount; i++) {
		auto& timer = frame.timers[i];
		if (timer.name.empty())
			continue;
		if (!timer.ended) {
			invalidGpuTimers.insert(timer.name);
			invalidCpuTimers.insert(timer.name);
			continue;
		}

		if (timer.hasCpuTime &&
		    IsValidProfilerSample(timer.cpuMs)) {
			auto& entry = activeTimers[timer.name];
			entry.cpuMs += timer.cpuMs;
			entry.hasCpu = true;
			activeCpuTotalMs += timer.cpuMs;
		} else {
			invalidCpuTimers.insert(timer.name);
		}

		if (!gpuFrameComplete) {
			invalidGpuTimers.insert(timer.name);
			continue;
		}
		if (!detailedPassGpuTimingAvailable) {
			invalidGpuTimers.insert(timer.name);
			continue;
		}

		UINT64 tsBegin = 0;
		UINT64 tsEnd = 0;
		const HRESULT beginResult =
			context->GetData(
				timer.begin.get(),
				&tsBegin,
				sizeof(tsBegin),
				D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (beginResult == S_FALSE)
			return false;
		if (FAILED(beginResult)) {
			invalidGpuTimers.insert(timer.name);
			continue;
		}

		const HRESULT endResult =
			context->GetData(
				timer.end.get(),
				&tsEnd,
				sizeof(tsEnd),
				D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (endResult == S_FALSE)
			return false;
		if (FAILED(endResult) || tsEnd < tsBegin) {
			invalidGpuTimers.insert(timer.name);
			continue;
		}

		const float gpuMs = static_cast<float>(
			static_cast<double>(tsEnd - tsBegin) *
			gpuTicksToMs);
		if (IsValidProfilerSample(gpuMs)) {
			auto& entry = activeTimers[timer.name];
			entry.gpuMs += gpuMs;
			entry.hasGpu = true;
			activeTotalMs += gpuMs;
		} else {
			invalidGpuTimers.insert(timer.name);
		}
	}
	if (frame.inFlight)
		frame.inFlight = false;

	for (const auto& timer : frame.cpuTimers) {
		if (timer.name.empty())
			continue;
		if (!timer.hasCpuTime ||
		    !IsValidProfilerSample(timer.cpuMs)) {
			invalidCpuTimers.insert(timer.name);
			continue;
		}

		auto& entry = activeTimers[timer.name];
		entry.cpuMs += timer.cpuMs;
		entry.hasCpu = true;
		activeCpuTotalMs += timer.cpuMs;
	}

	// A duplicated timer name is one logical channel sample. If any occurrence
	// failed, do not publish the sum of only its successful occurrences.
	for (const auto& name : invalidGpuTimers) {
		if (auto it = activeTimers.find(name);
			it != activeTimers.end()) {
			it->second.gpuMs = 0.0f;
			it->second.hasGpu = false;
		}
	}
	for (const auto& name : invalidCpuTimers) {
		if (auto it = activeTimers.find(name);
			it != activeTimers.end()) {
			it->second.cpuMs = 0.0f;
			it->second.hasCpu = false;
		}
	}

	if (frame.acceptedPresent) {
		if (frame.captureMode == CaptureMode::DetailedPasses) {
			IncrementSaturating(collectedDetailedCycles);

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
				if (active.hasGpu || active.hasCpu)
					known.lastSampleCycle = collectedDetailedCycles;
			}

			const bool cpuFrameComplete =
				gpuFrameComplete ||
				frame.hasWholeFrameCpuTime ||
				frame.activeCount > 0 ||
				hasCompletedCpuTimers;
			for (auto& known : knownTimers) {
				const auto activeIt =
					activeTimers.find(known.name);
				const bool activeGpu =
					activeIt != activeTimers.end() &&
					activeIt->second.hasGpu;
				const bool activeCpu =
					activeIt != activeTimers.end() &&
					activeIt->second.hasCpu;

				if (gpuFrameComplete &&
				    known.hasGpu &&
				    !activeGpu &&
				    !invalidGpuTimers.contains(
					    known.name)) {
					known.gpu.PushSample(0.0f);
				}
				if (cpuFrameComplete &&
				    known.hasCpu &&
				    !activeCpu &&
				    !invalidCpuTimers.contains(
					    known.name)) {
					known.cpu.PushSample(0.0f);
				}
			}

			const bool gpuPassTotalsValid =
				gpuFrameComplete &&
				invalidGpuTimers.empty();
			const bool cpuPassTotalsValid =
				cpuFrameComplete &&
				invalidCpuTimers.empty();
			if (gpuPassTotalsValid) {
				profiledPassGpuTotalMs = activeTotalMs;
				profiledPassGpuTotalHistory.PushSample(
					activeTotalMs);
			}
			if (cpuPassTotalsValid) {
				profiledPassCpuTotalMs = activeCpuTotalMs;
				profiledPassCpuTotalHistory.PushSample(
					activeCpuTotalMs);
			}
			RetireStaleTimers();
			RebuildResults(&activeTimers);
		}

		const bool hasWholeFrameCpuTime =
			frame.hasWholeFrameCpuTime &&
			IsValidWholeFrameSample(
				frame.wholeFrameCpuMs);
		const uint64_t sampleId = ++wholeFrameSampleId;
		wholeFramePresentIntervalSampleId =
			frame.presentIntervalSampleId;
		if (hasWholeFrameGpuTime) {
			wholeFrameGpuHistory.PushSample(
				wholeFrameGpuMs);
			wholeFrameGpuSampleId = sampleId;
		}
		if (hasWholeFrameCpuTime) {
			wholeFrameCpuHistory.PushSample(
				frame.wholeFrameCpuMs);
			wholeFrameCpuSampleId = sampleId;
		}
	}
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

void Profiler::RetireStaleTimers()
{
	const size_t previousSize = knownTimers.size();
	std::erase_if(knownTimers, [this](const KnownTimer& known) {
		return collectedDetailedCycles >= known.lastSampleCycle &&
		       collectedDetailedCycles - known.lastSampleCycle >= kTimerRetireCycles;
	});
	if (knownTimers.size() != previousSize)
		RebuildTimerIndex();
}

void Profiler::RebuildTimerIndex()
{
	knownTimerIndex.clear();
	for (size_t i = 0; i < knownTimers.size(); ++i)
		knownTimerIndex[knownTimers[i].name] = i;
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
