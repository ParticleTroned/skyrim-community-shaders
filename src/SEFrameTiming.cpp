#include "SEFrameTiming.h"
#include "Utils/ResourceName.h"

#include <cmath>
#include <limits>
#include <spdlog/spdlog.h>

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
		       ms <= SEFrameTiming::kMaxPresentIntervalMs;
	}

	void IncrementSaturating(uint64_t& value)
	{
		if (value != std::numeric_limits<uint64_t>::max())
			++value;
	}

}

void SEFrameTiming::ResetWholeFrameTimings()
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

void SEFrameTiming::RecordPresentDiscontinuity()
{
	IncrementSaturating(presentDiscontinuityEpoch);
}

void SEFrameTiming::ResetFrameState(FrameQueries& frame)
{
	frame.wholeFrameCpuBegin = {};
	frame.wholeFrameCpuMs = 0.0f;
	frame.hasWholeFrameCpuBegin = false;
	frame.hasWholeFrameCpuTime = false;
	frame.acceptedPresent = false;
	frame.presentIntervalSampleId = 0;
	frame.hasGpuTiming = false;
}

void SEFrameTiming::Initialize(ID3D11Device* device, ID3D11DeviceContext* a_context, double ticksToMs)
{
	Release();
	if (!device || !a_context || !std::isfinite(ticksToMs) || ticksToMs <= 0.0)
		return;
	context = a_context;
	cpuTicksToMs = ticksToMs;
	wholeFrameGpuTimingAvailable = true;
	const D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP, 0 };
	for (uint32_t i = 0; i < kFrameLatency; ++i) {
		auto& frame = frames[i];
		const auto beginResult = device->CreateQuery(&desc, frame.wholeFrameBegin.put());
		const auto endResult = device->CreateQuery(&desc, frame.wholeFrameEnd.put());
		wholeFrameGpuTimingAvailable = wholeFrameGpuTimingAvailable &&
			SUCCEEDED(beginResult) && SUCCEEDED(endResult) && frame.wholeFrameBegin && frame.wholeFrameEnd;
		if (frame.wholeFrameBegin)
			Util::SetResourceName(frame.wholeFrameBegin.get(), "Profiler::WholeFrame Begin[%u]", i);
		if (frame.wholeFrameEnd)
			Util::SetResourceName(frame.wholeFrameEnd.get(), "Profiler::WholeFrame End[%u]", i);
	}
	if (!wholeFrameGpuTimingAvailable) {
		for (auto& frame : frames) {
			frame.wholeFrameBegin = nullptr;
			frame.wholeFrameEnd = nullptr;
		}
		spdlog::warn("Whole-frame GPU timing is unavailable because timestamp queries could not be created; CPU and accepted-Present timing remain active");
	}
	initialized = true;
}

void SEFrameTiming::Release()
{
	Clear();
	for (auto& frame : frames) {
		frame.wholeFrameBegin = nullptr;
		frame.wholeFrameEnd = nullptr;
	}
	wholeFrameGpuTimingAvailable = false;
	initialized = false;
	context = nullptr;
	cpuTicksToMs = 0.0;
	captureRequested.store(false, std::memory_order_release);
	captureActive.store(false, std::memory_order_release);
}

void SEFrameTiming::Clear()
{
	ResetWholeFrameTimings();
	for (auto& frame : frames)
		ResetFrameState(frame);
	frameActive = false;
}

void SEFrameTiming::SetUserEnabled(bool enabled)
{
	userEnabled.store(enabled, std::memory_order_release);
	if (!enabled) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
	}
}

void SEFrameTiming::SetCaptureAllowed(bool allowed)
{
	captureAllowed.store(allowed, std::memory_order_release);
	if (!allowed) {
		captureRequested.store(false, std::memory_order_release);
		captureActive.store(false, std::memory_order_release);
	}
}

void SEFrameTiming::RequestCapture()
{
	if (userEnabled.load(std::memory_order_acquire) && captureAllowed.load(std::memory_order_acquire))
		captureRequested.store(true, std::memory_order_release);
	if (!userEnabled.load(std::memory_order_acquire) || !captureAllowed.load(std::memory_order_acquire))
		captureRequested.store(false, std::memory_order_release);
}

void SEFrameTiming::PromoteRequestedCapture()
{
	const bool requested = captureRequested.exchange(false, std::memory_order_acq_rel);
	captureActive.store(requested && userEnabled.load(std::memory_order_acquire) && captureAllowed.load(std::memory_order_acquire), std::memory_order_release);
}

bool SEFrameTiming::BeginFrame(uint32_t slot, bool gpuClockAvailable)
{
	if (!initialized || !context || frameActive || slot >= kFrameLatency || !IsCaptureActive())
		return false;
	auto& frame = frames[slot];
	ResetFrameState(frame);
	activeFrame = slot;
	frameActive = true;
	frame.hasGpuTiming = gpuClockAvailable && wholeFrameGpuTimingAvailable;
	if (frame.hasGpuTiming)
		context->End(frame.wholeFrameBegin.get());
	frame.hasWholeFrameCpuBegin = QueryPerformanceCounter(&frame.wholeFrameCpuBegin) && frame.wholeFrameCpuBegin.QuadPart > 0;
	return true;
}

void SEFrameTiming::SkipFrame()
{
	IncrementSaturating(skippedWholeFrameCaptureCount);
}

void SEFrameTiming::BeginPresent(UINT syncInterval, UINT presentFlags)
{
	if (!initialized || !context || presentPending)
		return;
	presentPending = true;
	pendingPresentSyncInterval = syncInterval;
	pendingPresentIsReal = (presentFlags & DXGI_PRESENT_TEST) == 0;
	pendingPresentHasTimestamp = false;
	pendingPresentHasProfileFrame = false;
	pendingPresentStart = {};
	if (!pendingPresentIsReal)
		return;

	pendingPresentHasTimestamp = QueryPerformanceCounter(&pendingPresentStart) && pendingPresentStart.QuadPart > 0;
	if (!frameActive)
		return;
	auto& frame = frames[activeFrame];
	frameActive = false;
	if (pendingPresentHasTimestamp && frame.hasWholeFrameCpuBegin) {
		frame.wholeFrameCpuMs = static_cast<float>(static_cast<double>(pendingPresentStart.QuadPart - frame.wholeFrameCpuBegin.QuadPart) * cpuTicksToMs);
		frame.hasWholeFrameCpuTime = IsValidWholeFrameSample(frame.wholeFrameCpuMs);
	}
	if (frame.hasGpuTiming)
		context->End(frame.wholeFrameEnd.get());
	pendingPresentFrame = activeFrame;
	pendingPresentHasProfileFrame = true;
}

void SEFrameTiming::CompletePresent(HRESULT presentResult)
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

bool SEFrameTiming::ReadGpuSample(uint32_t slot, uint64_t frequency, bool clockValid, float& milliseconds) const
{
	milliseconds = 0.0f;
	const auto& frame = frames[slot];
	if (!clockValid || frequency == 0 || !frame.hasGpuTiming)
		return true;
	UINT64 begin = 0, end = 0;
	const auto beginResult = context->GetData(frame.wholeFrameBegin.get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	const auto endResult = context->GetData(frame.wholeFrameEnd.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	if (beginResult == S_FALSE || endResult == S_FALSE)
		return false;
	if (beginResult == S_OK && endResult == S_OK && end >= begin) {
		const float sample = static_cast<float>(static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency));
		if (IsValidWholeFrameSample(sample))
			milliseconds = sample;
	}
	return true;
}

void SEFrameTiming::CompleteFrame(uint32_t slot, float gpuMilliseconds)
{
	auto& frame = frames[slot];
	if (frame.acceptedPresent) {
		const auto sampleId = ++wholeFrameSampleId;
		wholeFramePresentIntervalSampleId = frame.presentIntervalSampleId;
		if (IsValidWholeFrameSample(gpuMilliseconds)) {
			wholeFrameGpuHistory.PushSample(gpuMilliseconds);
			wholeFrameGpuSampleId = sampleId;
		}
		if (frame.hasWholeFrameCpuTime && IsValidWholeFrameSample(frame.wholeFrameCpuMs)) {
			wholeFrameCpuHistory.PushSample(frame.wholeFrameCpuMs);
			wholeFrameCpuSampleId = sampleId;
		}
	}
	ResetFrameState(frame);
}
