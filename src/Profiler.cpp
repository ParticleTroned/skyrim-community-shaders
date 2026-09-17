#include "Profiler.h"
#include "Utils/ProfilerTiming.h"
#include "Utils/ResourceName.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <unordered_map>

namespace
{
	bool IsValidProfilerSample(float ms)
	{
		return Util::ProfilerTiming::IsValidSample(ms);
	}

	bool HasSameProfilerRoot(std::string_view left, std::string_view right)
	{
		return Profiler::GetTimerRootName(left) == Profiler::GetTimerRootName(right);
	}

	void PushAlignedProfilerSamples(
		Profiler::RollingHistory& fullHistory,
		Profiler::RollingHistory& outermostHistory,
		float fullMs,
		float outermostMs)
	{
		fullHistory.PushSample(fullMs);
		outermostHistory.PushSample(outermostMs);
	}

	void IncrementSaturating(uint64_t& value)
	{
		if (value != std::numeric_limits<uint64_t>::max())
			++value;
	}
}

std::string_view Profiler::GetTimerRootName(std::string_view name)
{
	const auto separator = name.find("::");
	return separator == std::string_view::npos ? name : name.substr(0, separator);
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
	CancelFrameCaptures(frame, "capture_reset");
	frame.activeCount = 0;
	frame.detailCount = 0;
	frame.activeTimerStack.clear();
	frame.activeDetailStack.clear();
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
		CancelFrameCaptures(frame, "device_released");
		frame.disjoint = nullptr;
		frame.timers.clear();
		frame.detailTimers.clear();
		frame.activeTimerStack.clear();
		frame.activeDetailStack.clear();
		frame.cpuTimers.clear();
		frame.activeCount = 0;
		frame.detailCount = 0;
		frame.inFlight = false;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	collectedDetailedCycles = 0;
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
		for (auto& frame : frames)
			CancelFrameCaptures(frame, "profiler_disabled");
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
	collectedDetailedCycles = 0;
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
				InvalidateCapture(timer.capture, "feature_timers_cleared");
				timer.capture.reset();
				timer.name.clear();
			}
		}
		for (auto& timer : frame.detailTimers) {
			if (timer.name.starts_with(prefix)) {
				InvalidateCapture(timer.capture, "feature_timers_cleared");
				timer.capture.reset();
				timer.name.clear();
			}
		}
	}

	RebuildTimerIndex();
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive || !captureActive.load(std::memory_order_acquire))
		return;

	if (!CollectResults())
		return;

	auto& frame = frames[writeFrame];
	if (!frame.disjoint)
		return;
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

bool Profiler::BeginPass(std::string_view name, bool fireCallbacks, const PassTimingHandle& capture)
{
	auto claimedCapture = ClaimCapture(capture, false);
	if (!initialized || !context || !IsUserEnabled())
		return false;
	if (!captureActive.load(std::memory_order_acquire))
		return false;

	if (!frameActive)
		BeginFrame();
	if (!frameActive) {
		InvalidateCapture(claimedCapture, frames[writeFrame].disjoint ? "query_ring_pending" : "query_creation_failed");
		return false;
	}

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers) {
		slotRefusals++;
		InvalidateCapture(claimedCapture, "query_capacity_exhausted");
		return false;
	}
	if (!frame.timers[frame.activeCount].begin || !frame.timers[frame.activeCount].end) {
		InvalidateCapture(claimedCapture, "query_creation_failed");
		return false;
	}

	const uint32_t timerIndex = frame.activeCount;
	auto& timer = frame.timers[timerIndex];
	try {
		timer.name.assign(name);
	} catch (const std::bad_alloc&) {
		InvalidateCapture(claimedCapture, "timing_allocation_failed");
		return false;
	}
	++frame.activeCount;
	++acquiredSlotsThisFrame;
	timer.capture = std::move(claimedCapture);
	timer.cpuMs = 0.0f;
	timer.cpuSelfMs = 0.0f;
	timer.nestedCpuMs = 0.0;
	timer.cpuOrdinal = ++nextCpuOrdinal;
	timer.parentSlot = frame.activeTimerStack.empty() ? -1 : static_cast<int32_t>(frame.activeTimerStack.back());
	timer.depth = static_cast<uint32_t>(frame.activeTimerStack.size());
	timer.ended = false;
	timer.outermostGpuInRoot = !HasActiveGpuAncestorWithSameRoot(name);
	timer.outermostCpuInRoot = timer.outermostGpuInRoot && !HasActiveCpuAncestorWithSameRoot(name);
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
	const auto completion = Util::ProfilerTiming::CompleteCpuScope(timer.name.empty() ? -1.0 : timer.cpuMs, timer.nestedCpuMs);
	timer.cpuSelfMs = static_cast<float>(completion.selfMs);
	AddCpuChildTime(timer.cpuOrdinal, completion.coveredMs);

	context->End(timer.end.get());
	timer.ended = true;
	CompleteCapturedCpu(timer);

	if (fireCallbacks && endPerfEvent)
		endPerfEvent({});
}

Profiler::PassTimingHandle Profiler::ClaimCapture(const PassTimingHandle& capture, bool detailOnly) noexcept
{
	if (!capture)
		return {};
	bool accepted = false;
	capture->Update([&](auto& result, bool& claimed) {
		if (claimed || !result.captureId)
			return;
		claimed = accepted = true;
		result.detailOnly = detailOnly;
		const char* unavailable = !initialized || !context                       ? "profiler_uninitialized" :
		                          !IsUserEnabled()                               ? "profiler_disabled" :
		                          !captureActive.load(std::memory_order_acquire) ? "capture_inactive" :
		                                                                           nullptr;
		result.cpuReason = result.gpuReason = unavailable ? unavailable : "scope_pending";
		result.cpuState = result.gpuState = unavailable ? Util::PassTimingState::Unavailable : Util::PassTimingState::Pending;
	});
	return accepted ? capture : PassTimingHandle{};
}

void Profiler::MarkCaptureUnavailable(const PassTimingHandle& capture, const char* reason, bool detailOnly) noexcept
{
	if (!capture)
		return;
	capture->Update([&](auto& result, bool& claimed) {
		if (claimed || !result.captureId)
			return;
		claimed = true;
		result.detailOnly = detailOnly;
		result.cpuReason = result.gpuReason = reason;
	});
}

void Profiler::InvalidateCapture(const PassTimingHandle& capture, const char* reason) noexcept
{
	if (!capture)
		return;
	capture->Update([&](auto& result, bool&) {
		if (result.cpuState == Util::PassTimingState::Pending) {
			result.cpuState = Util::PassTimingState::Unavailable;
			result.cpuReason = reason;
		}
		if (result.gpuState == Util::PassTimingState::Pending) {
			result.gpuState = Util::PassTimingState::Unavailable;
			result.gpuReason = reason;
		}
	});
}

void Profiler::CancelFrameCaptures(FrameQueries& frame, const char* reason) noexcept
{
	for (auto timers : { std::span(frame.timers).first(frame.activeCount), std::span(frame.detailTimers).first(frame.detailCount) }) {
		for (auto& timer : timers) {
			InvalidateCapture(timer.capture, reason);
			timer.capture.reset();
		}
	}
}

void Profiler::CompleteCapturedCpu(FrameQueries::TimerPair& timer) noexcept
{
	if (!timer.capture)
		return;
	timer.capture->Update([&](auto& result, bool&) {
		if (result.cpuState != Util::PassTimingState::Pending)
			return;
		const bool valid = !timer.name.empty() && IsValidProfilerSample(timer.cpuMs);
		result.cpuState = valid ? Util::PassTimingState::Ready : Util::PassTimingState::Failed;
		result.cpuReason = valid ? "sample_ready" : "invalid_duration";
		result.cpuInclusiveMs = valid ? timer.cpuMs : 0.0f;
		result.cpuSelfMs = valid ? timer.cpuSelfMs : 0.0f;
		result.gpuReason = "query_pending";
	});
}

void Profiler::CompleteCapturedGpu(FrameQueries::TimerPair& timer, double inclusiveMs, double selfMs, const char* failure) noexcept
{
	if (!timer.capture)
		return;
	timer.capture->Update([&](auto& result, bool&) {
		if (result.gpuState != Util::PassTimingState::Pending)
			return;
		const bool valid = Util::ProfilerTiming::IsValidSample(inclusiveMs);
		result.gpuState = valid ? Util::PassTimingState::Ready : Util::PassTimingState::Failed;
		result.gpuReason = valid ? "sample_ready" : failure;
		result.gpuInclusiveMs = valid ? static_cast<float>(inclusiveMs) : 0.0f;
		result.gpuSelfMs = valid ? static_cast<float>(selfMs) : 0.0f;
	});
}

void Profiler::BindCapturedFrame(FrameQueries::TimerPair& timer, uint32_t frame) noexcept
{
	if (!timer.capture)
		return;
	timer.capture->Update([&](auto& result, bool&) {
		result.capturedFrame = frame;
		if (!timer.ended) {
			result.cpuState = result.gpuState = Util::PassTimingState::Failed;
			result.cpuReason = result.gpuReason = "scope_not_ended";
		}
	});
}

bool Profiler::BeginDetailPass(std::string_view name, const PassTimingHandle& capture) noexcept
try {
	auto claimedCapture = ClaimCapture(capture, true);
	if (!claimedCapture || !initialized || !context || !IsEnabled())
		return false;
	if (!frameActive)
		BeginFrame();
	if (!frameActive) {
		InvalidateCapture(claimedCapture, frames[writeFrame].disjoint ? "query_ring_pending" : "query_creation_failed");
		return false;
	}
	auto& frame = frames[writeFrame];
	if (frame.detailCount >= kMaxDetailTimers) {
		InvalidateCapture(claimedCapture, "query_capacity_exhausted");
		return false;
	}
	if (frame.detailTimers.empty()) {
		frame.detailTimers.resize(kMaxDetailTimers);
	}
	frame.activeDetailStack.reserve(kMaxDetailTimers);
	auto& timer = frame.detailTimers[frame.detailCount];
	if (!timer.begin || !timer.end) {
		winrt::com_ptr<ID3D11Device> device;
		context->GetDevice(device.put());
		D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP, 0 };
		winrt::com_ptr<ID3D11Query> beginQuery, endQuery;
		if (!device || FAILED(device->CreateQuery(&desc, beginQuery.put())) ||
			FAILED(device->CreateQuery(&desc, endQuery.put()))) {
			InvalidateCapture(claimedCapture, "query_creation_failed");
			return false;
		}
		Util::SetResourceName(beginQuery.get(), "Profiler::DetailBegin[%u][%u]", writeFrame, frame.detailCount);
		Util::SetResourceName(endQuery.get(), "Profiler::DetailEnd[%u][%u]", writeFrame, frame.detailCount);
		timer.begin = std::move(beginQuery);
		timer.end = std::move(endQuery);
	}
	try {
		timer.name.assign(name);
	} catch (const std::bad_alloc&) {
		InvalidateCapture(claimedCapture, "timing_allocation_failed");
		return false;
	}
	timer.capture = std::move(claimedCapture);
	timer.cpuMs = timer.cpuSelfMs = 0.0f;
	timer.nestedCpuMs = 0.0;
	timer.parentSlot = frame.activeDetailStack.empty() ? -1 : static_cast<int32_t>(frame.activeDetailStack.back());
	timer.ended = false;
	context->End(timer.begin.get());
	QueryPerformanceCounter(&timer.cpuBegin);
	frame.activeDetailStack.push_back(frame.detailCount++);
	return true;
} catch (...) {
	InvalidateCapture(capture, "timing_allocation_failed");
	return false;
}

void Profiler::EndDetailPass() noexcept
{
	if (!initialized || !context || !frameActive)
		return;
	auto& frame = frames[writeFrame];
	if (frame.activeDetailStack.empty())
		return;
	auto& timer = frame.detailTimers[frame.activeDetailStack.back()];
	frame.activeDetailStack.pop_back();
	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);
	timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);
	const auto completion = Util::ProfilerTiming::CompleteCpuScope(timer.name.empty() ? -1.0 : timer.cpuMs, timer.nestedCpuMs);
	timer.cpuSelfMs = static_cast<float>(completion.selfMs);
	if (!frame.activeDetailStack.empty())
		frame.detailTimers[frame.activeDetailStack.back()].nestedCpuMs += completion.coveredMs;
	context->End(timer.end.get());
	timer.ended = true;
	CompleteCapturedCpu(timer);
}

bool Profiler::BeginCpuPass(std::string_view name)
{
	if (!initialized)
		return false;
	if (!captureActive.load(std::memory_order_acquire))
		return false;

	if (activeCpuTimers.size() + completedCpuTimers.size() >= kMaxTimers)
		return false;

	const bool outermostCpuInRoot =
		!HasActiveGpuAncestorWithSameRoot(name) && !HasActiveCpuAncestorWithSameRoot(name);

	auto& timer = activeCpuTimers.emplace_back();
	timer.name.assign(name);
	timer.ordinal = ++nextCpuOrdinal;
	timer.outermostCpuInRoot = outermostCpuInRoot;
	QueryPerformanceCounter(&timer.cpuBegin);
	return true;
}

void Profiler::EndCpuPass()
{
	if (!initialized || activeCpuTimers.empty())
		return;

	auto timer = std::move(activeCpuTimers.back());
	activeCpuTimers.pop_back();

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);

	CompletedCpuTimer completed;
	completed.name = std::move(timer.name);
	completed.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);
	const auto completion = Util::ProfilerTiming::CompleteCpuScope(completed.name.empty() ? -1.0 : completed.cpuMs, timer.nestedCpuMs);
	completed.cpuSelfMs = static_cast<float>(completion.selfMs);
	AddCpuChildTime(timer.ordinal, completion.coveredMs);
	completed.outermostCpuInRoot = timer.outermostCpuInRoot;
	if (completed.name.empty() || !IsValidProfilerSample(completed.cpuMs))
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
	for (auto timers : { std::span(frame.timers).first(frame.activeCount), std::span(frame.detailTimers).first(frame.detailCount) }) {
		for (auto& timer : timers)
			BindCapturedFrame(timer, a_frameCount);
	}
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
		std::vector<Util::ProfilerTiming::Interval> intervals(frame.activeCount);
		std::array<Util::ProfilerTiming::Interval, kMaxDetailTimers> detailStorage{};
		const auto detailIntervals = std::span(detailStorage).first(frame.detailCount);
		std::array<const char*, kMaxTimers> failureStorage{};
		std::array<const char*, kMaxDetailTimers> detailFailureStorage{};
		const auto failures = std::span(failureStorage).first(frame.activeCount);
		const auto detailFailures = std::span(detailFailureStorage).first(frame.detailCount);
		std::fill(failures.begin(), failures.end(), "invalid_duration");
		std::fill(detailFailures.begin(), detailFailures.end(), "invalid_duration");
		for (uint32_t i = 0; i < frame.activeCount; ++i)
			intervals[i].parent = frame.timers[i].parentSlot;
		for (uint32_t i = 0; i < frame.detailCount; ++i)
			detailIntervals[i].parent = frame.detailTimers[i].parentSlot;
		HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr == S_FALSE)
			return false;
		if (hr == S_OK && !disjointData.Disjoint && disjointData.Frequency > 0) {
			const double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);
			const auto readTimers = [&](const auto& timers, auto& timings, auto& reasons) {
				for (size_t i = 0; i < timings.size(); ++i) {
					const auto& timer = timers[i];
					if (timer.name.empty() || !timer.ended)
						continue;
					UINT64 tsBegin = 0;
					UINT64 tsEnd = 0;
					const HRESULT beginHr = context->GetData(timer.begin.get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					const HRESULT endHr = context->GetData(timer.end.get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					if (beginHr == S_FALSE || endHr == S_FALSE)
						return false;
					if (beginHr != S_OK || endHr != S_OK)
						reasons[i] = "query_failed";
					else if (tsEnd < tsBegin)
						reasons[i] = "invalid_timestamp_order";
					else
						timings[i].inclusiveMs = static_cast<double>(tsEnd - tsBegin) * ticksToMs;
				}
				return true;
			};
			if (!readTimers(frame.timers, intervals, failures) || !readTimers(frame.detailTimers, detailIntervals, detailFailures))
				return false;
			gpuFrameResolved = true;
		} else {
			const char* reason = hr != S_OK ? "query_failed" : disjointData.Disjoint ? "gpu_clock_disjoint" :
			                                                                           "invalid_gpu_frequency";
			std::fill(failures.begin(), failures.end(), reason);
			std::fill(detailFailures.begin(), detailFailures.end(), reason);
		}

		const auto selfTimes = Util::ProfilerTiming::ResolveSelfTimes(intervals);
		try {
			const auto detailSelfTimes = Util::ProfilerTiming::ResolveSelfTimes(detailIntervals);
			for (uint32_t i = 0; i < frame.detailCount; ++i)
				CompleteCapturedGpu(frame.detailTimers[i], detailIntervals[i].inclusiveMs, detailSelfTimes[i], detailFailures[i]);
		} catch (const std::bad_alloc&) {
			for (uint32_t i = 0; i < frame.detailCount; ++i)
				InvalidateCapture(frame.detailTimers[i].capture, "timing_allocation_failed");
		}
		for (uint32_t i = 0; i < frame.activeCount; ++i) {
			auto& timer = frame.timers[i];
			CompleteCapturedGpu(timer, intervals[i].inclusiveMs, selfTimes[i], failures[i]);
			if (timer.name.empty() || !timer.ended)
				continue;
			const bool gpuValid = Util::ProfilerTiming::IsValidSample(intervals[i].inclusiveMs);
			const bool cpuValid = IsValidProfilerSample(timer.cpuMs);
			if (!gpuValid && !cpuValid)
				continue;

			// Resolve nesting before aggregating repeated names into one history sample.
			auto& entry = activeTimers[timer.name];
			GetOrCreateTimer(timer.name);
			if (gpuValid) {
				const auto inclusiveMs = static_cast<float>(intervals[i].inclusiveMs);
				entry.gpuMs += static_cast<float>(selfTimes[i]);
				if (timer.outermostGpuInRoot)
					entry.outermostGpuMs += inclusiveMs;
				entry.hasGpu = true;
				if (timer.depth == 0) {
					activeTotalMs += inclusiveMs;
					entry.topLevelMs += inclusiveMs;
				}
			}
			// A disjoint GPU clock does not invalidate CPU wall-clock measurements.
			if (cpuValid) {
				entry.cpuMs += timer.cpuSelfMs;
				if (timer.outermostCpuInRoot)
					entry.outermostCpuMs += timer.cpuMs;
				entry.hasCpu = true;
				activeCpuTotalMs += timer.cpuSelfMs;
			}
		}
		frame.inFlight = false;
	}

	for (const auto& timer : frame.cpuTimers) {
		if (timer.name.empty())
			continue;
		if (!IsValidProfilerSample(timer.cpuMs))
			continue;

		auto& entry = activeTimers[timer.name];
		entry.cpuMs += timer.cpuSelfMs;
		if (timer.outermostCpuInRoot)
			entry.outermostCpuMs += timer.cpuMs;
		entry.hasCpu = true;
		activeCpuTotalMs += timer.cpuSelfMs;

		GetOrCreateTimer(timer.name);
	}

	// Exactly one history sample per named timer and resolved cycle keeps all
	// histories aligned for percentile-of-sum calculations in the UI.
	const bool cpuCycleResolved = gpuFrameResolved || hadCpuTimers ||
	                              std::any_of(activeTimers.begin(), activeTimers.end(), [](const auto& timer) { return timer.second.hasCpu; });
	if (cpuCycleResolved)
		IncrementSaturating(collectedDetailedCycles);
	for (auto& known : knownTimers) {
		auto it = activeTimers.find(known.name);
		const bool freshGpu = it != activeTimers.end() && it->second.hasGpu;
		const bool freshCpu = it != activeTimers.end() && it->second.hasCpu;
		if (freshGpu) {
			known.hasGpu = true;
			PushAlignedProfilerSamples(known.gpu, known.outermostGpu, it->second.gpuMs, it->second.outermostGpuMs);
		} else if (gpuFrameResolved && known.hasGpu) {
			PushAlignedProfilerSamples(known.gpu, known.outermostGpu, 0.0f, 0.0f);
		}
		if (freshCpu) {
			known.hasCpu = true;
			PushAlignedProfilerSamples(known.cpu, known.outermostCpu, it->second.cpuMs, it->second.outermostCpuMs);
		} else if (cpuCycleResolved && known.hasCpu) {
			PushAlignedProfilerSamples(known.cpu, known.outermostCpu, 0.0f, 0.0f);
		}
		if (freshGpu || freshCpu)
			known.lastSampleCycle = collectedDetailedCycles;
	}
	if (cpuCycleResolved)
		RetireStaleTimers();

	frame.cpuTimers.clear();
	CancelFrameCaptures(frame, "capture_reset");
	frame.activeCount = 0;
	frame.detailCount = 0;
	frame.activeTimerStack.clear();
	frame.activeDetailStack.clear();

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
			PushAlignedProfilerSamples(timer.gpu, timer.outermostGpu, found->second.gpuMs, found->second.outermostGpuMs);
		} else if (a_gpuCycleResolved && timer.hasGpu) {
			timer.topLevelMs = 0.0f;
			PushAlignedProfilerSamples(timer.gpu, timer.outermostGpu, 0.0f, 0.0f);
		}
		if (freshCpu) {
			timer.hasCpu = true;
			PushAlignedProfilerSamples(timer.cpu, timer.outermostCpu, found->second.cpuMs, found->second.outermostCpuMs);
		} else if (a_cpuCycleResolved && timer.hasCpu) {
			PushAlignedProfilerSamples(timer.cpu, timer.outermostCpu, 0.0f, 0.0f);
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
			result.outermostGpuHistoryBuffer = known.outermostGpu.history;
			result.historyHead = known.gpu.head;
			result.historyCount = known.gpu.count;
		}
		if (known.hasCpu) {
			result.cpuAvgMs = known.cpu.GetAverage();
			result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
			result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
			result.cpuHistoryBuffer = known.cpu.history;
			result.outermostCpuHistoryBuffer = known.outermostCpu.history;
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

bool Profiler::HasActiveGpuAncestorWithSameRoot(std::string_view name) const
{
	if (!frameActive)
		return false;

	const auto& frame = frames[writeFrame];
	for (const uint32_t activeTimerIndex : frame.activeTimerStack) {
		if (activeTimerIndex < frame.activeCount && HasSameProfilerRoot(frame.timers[activeTimerIndex].name, name))
			return true;
	}
	return false;
}

void Profiler::AddCpuChildTime(uint64_t childOrdinal, double coveredMs)
{
	uint64_t parentOrdinal = 0;
	double* nestedMs = nullptr;
	// GPU-backed and CPU-only scopes share one chronological CPU nesting order.
	if (frameActive) {
		auto& frame = frames[writeFrame];
		for (const auto index : frame.activeTimerStack) {
			auto& timer = frame.timers[index];
			if (timer.cpuOrdinal < childOrdinal && timer.cpuOrdinal > parentOrdinal) {
				parentOrdinal = timer.cpuOrdinal;
				nestedMs = &timer.nestedCpuMs;
			}
		}
	}
	for (auto& timer : activeCpuTimers) {
		if (timer.ordinal < childOrdinal && timer.ordinal > parentOrdinal) {
			parentOrdinal = timer.ordinal;
			nestedMs = &timer.nestedCpuMs;
		}
	}
	if (nestedMs)
		*nestedMs += coveredMs;
}

bool Profiler::HasActiveCpuAncestorWithSameRoot(std::string_view name) const
{
	for (const auto& activeTimer : activeCpuTimers) {
		if (HasSameProfilerRoot(activeTimer.name, name))
			return true;
	}
	return false;
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
			result.outermostGpuHistoryBuffer = known.outermostGpu.history;
			result.historyHead = known.gpu.head;
			result.historyCount = known.gpu.count;
		}

		if (known.hasCpu) {
			result.cpuAvgMs = known.cpu.GetAverage();
			result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
			result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
			result.cpuHistoryBuffer = known.cpu.history;
			result.outermostCpuHistoryBuffer = known.outermostCpu.history;
			result.cpuHistoryHead = known.cpu.head;
			result.cpuHistoryCount = known.cpu.count;
		}

		result.valid = result.hasGpu || result.hasCpu;
		results.push_back(std::move(result));
	}
}
