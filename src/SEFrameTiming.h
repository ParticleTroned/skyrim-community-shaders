#pragma once

#include "Utils/ProfilerHistory.h"
#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <winrt/base.h>

/** Preserves SE accepted-Present and full-frame measurements on the owner's shared GPU clock. */
class SEFrameTiming
{
public:
	static constexpr uint32_t kFrameLatency = 8;
	static constexpr float kMaxPresentIntervalMs = 1000.0f;
	using RollingHistory = Util::ProfilerHistory;

	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, double ticksToMs);
	void Release();
	void Clear();
	void SetUserEnabled(bool enabled);
	void SetCaptureAllowed(bool allowed);
	void RequestCapture();
	bool IsCaptureActive() const { return userEnabled.load(std::memory_order_acquire) && captureAllowed.load(std::memory_order_acquire) && captureActive.load(std::memory_order_acquire); }
	/** Called only at the real post-Present frame boundary, after the shared clock starts. */
	bool BeginFrame(uint32_t slot, bool gpuClockAvailable);
	void SkipFrame();
	void BeginPresent(UINT syncInterval, UINT presentFlags);
	void CompletePresent(HRESULT presentResult);
	bool IsPresentPending() const { return presentPending; }
	bool IsPendingRealPresent() const { return presentPending && pendingPresentIsReal; }
	/** Nonblocking read; publication waits until the owner can retire the entire query slot. */
	bool ReadGpuSample(uint32_t slot, uint64_t frequency, bool clockValid, float& milliseconds) const;
	void CompleteFrame(uint32_t slot, float gpuMilliseconds);
	float GetWholeFrameGpuTimeMs() const { return wholeFrameGpuHistory.lastMs; }
	float GetWholeFrameCpuTimeMs() const { return wholeFrameCpuHistory.lastMs; }
	float GetPresentIntervalMs() const { return presentIntervalHistory.lastMs; }
	float GetWholeFrameGpuTimeAverageMs(uint32_t maxSamples = 60) const { return wholeFrameGpuHistory.GetAverage(maxSamples); }
	float GetWholeFrameCpuTimeAverageMs(uint32_t maxSamples = 60) const { return wholeFrameCpuHistory.GetAverage(maxSamples); }
	float GetPresentIntervalAverageMs(uint32_t maxSamples = 60) const { return presentIntervalHistory.GetAverage(maxSamples); }
	bool HasWholeFrameGpuTime() const { return wholeFrameGpuHistory.count > 0; }
	bool HasWholeFrameCpuTime() const { return wholeFrameCpuHistory.count > 0; }
	bool HasPresentInterval() const { return presentIntervalHistory.count > 0; }
	bool HasLatestWholeFrameGpuSample() const { return wholeFrameSampleId != 0 && wholeFrameGpuSampleId == wholeFrameSampleId; }
	bool HasLatestWholeFrameCpuSample() const { return wholeFrameSampleId != 0 && wholeFrameCpuSampleId == wholeFrameSampleId; }
	bool HasLatestPresentIntervalSample() const { return presentIntervalSampleId != 0; }
	bool WasLatestPresentIntervalSynced() const { return HasLatestPresentIntervalSample() && latestPresentIntervalWasSynced; }
	uint64_t GetWholeFrameSampleId() const { return wholeFrameSampleId; }
	uint64_t GetWholeFrameGpuSampleId() const { return wholeFrameGpuSampleId; }
	uint64_t GetWholeFrameCpuSampleId() const { return wholeFrameCpuSampleId; }
	uint64_t GetWholeFramePresentIntervalSampleId() const { return wholeFramePresentIntervalSampleId; }
	uint64_t GetPresentIntervalSampleId() const { return presentIntervalSampleId; }
	// Monotonic capture-integrity diagnostics. Measurements compare boundary
	// snapshots without interpreting a missing timing value as zero.
	uint64_t GetPresentDiscontinuityEpoch() const { return presentDiscontinuityEpoch; }
	uint64_t GetSkippedWholeFrameCaptureCount() const { return skippedWholeFrameCaptureCount; }

private:
	struct FrameQueries
	{
		winrt::com_ptr<ID3D11Query> wholeFrameBegin;
		winrt::com_ptr<ID3D11Query> wholeFrameEnd;
		LARGE_INTEGER wholeFrameCpuBegin{};
		float wholeFrameCpuMs = 0.0f;
		bool hasWholeFrameCpuBegin = false;
		bool hasWholeFrameCpuTime = false;
		bool acceptedPresent = false;
		uint64_t presentIntervalSampleId = 0;
		bool hasGpuTiming = false;
	};
	FrameQueries frames[kFrameLatency];
	ID3D11DeviceContext* context = nullptr;
	double cpuTicksToMs = 0.0;
	bool initialized = false;
	bool frameActive = false;
	uint32_t activeFrame = 0;
	std::atomic_bool userEnabled{ false };
	std::atomic_bool captureAllowed{ true };
	std::atomic_bool captureRequested{ false };
	std::atomic_bool captureActive{ false };
	RollingHistory wholeFrameGpuHistory;
	RollingHistory wholeFrameCpuHistory;
	RollingHistory presentIntervalHistory;
	uint64_t wholeFrameSampleId = 0;
	uint64_t wholeFrameGpuSampleId = 0;
	uint64_t wholeFrameCpuSampleId = 0;
	uint64_t wholeFramePresentIntervalSampleId = 0;
	uint64_t presentIntervalSampleId = 0;
	uint64_t presentDiscontinuityEpoch = 0;
	uint64_t skippedWholeFrameCaptureCount = 0;
	int64_t lastAcceptedPresentStartCounter = 0;
	bool wholeFrameGpuTimingAvailable = false;
	bool hasLastAcceptedPresentStart = false;
	bool lastAcceptedPresentWasSynced = false;
	bool latestPresentIntervalWasSynced = false;

	LARGE_INTEGER pendingPresentStart{};
	UINT pendingPresentSyncInterval = 0;
	uint32_t pendingPresentFrame = 0;
	bool presentPending = false;
	bool pendingPresentIsReal = false;
	bool pendingPresentHasTimestamp = false;
	bool pendingPresentHasProfileFrame = false;

	void PromoteRequestedCapture();
	static void ResetFrameState(FrameQueries& frame);
	void ResetWholeFrameTimings();
	void RecordPresentDiscontinuity();
};
