#pragma once

#include "RenderTargetStats.h"
#include "SEFrameTiming.h"
#include "Utils/ProfilerHistory.h"
#include "Utils/PassTimingCapture.h"
#include "Utils/ProfilerTiming.h"

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

class Profiler
{
public:
#ifdef DEVBENCH_BRIDGE_ENABLED
	// Repeated draw scopes need more query slots than the unique pass catalog.
	static constexpr uint32_t kMaxTimers = 512;
#else
	static constexpr uint32_t kMaxTimers = 128;
#endif
	static constexpr uint32_t kMaxDetailTimers = 64;
	static constexpr uint32_t kFrameLatency = SEFrameTiming::kFrameLatency;
	static constexpr uint32_t kHistorySize = Util::ProfilerHistory::kCapacity;
	static constexpr float kMaxPresentIntervalMs = SEFrameTiming::kMaxPresentIntervalMs;
	// Retain intermittent passes while removing entries absent across sustained capture.
	static constexpr uint64_t kTimerRetireCycles = 60;

	using PerfEventCallback = std::function<void(std::string_view)>;
	using PassTimingHandle = Util::PassTimingHandle;
	/** @brief Records a scope that cannot enter the profiler without replacing an earlier invocation. */
	static void MarkCaptureUnavailable(const PassTimingHandle& capture, const char* reason, bool detailOnly) noexcept;

	enum class CaptureMode : uint8_t
	{
		None = 0,
		WholeFrameOnly = 1,
		DetailedPasses = 2,
		GPU = 4,
		CPU = 8,
		Both = 12
	};

	using RollingHistory = Util::ProfilerHistory;

	struct TimerResult
	{
		std::string name;
		// Self time; profiled descendants are excluded before same-name aggregation.
		float gpuTimeMs = 0.0f;
		// Inclusive depth-0 contribution; its sum remains the resolved GPU total.
		float topLevelMs = 0.0f;
		float avgMs = 0.0f;
		float p95Ms = 0.0f;
		float p99Ms = 0.0f;
		float cpuTimeMs = 0.0f;
		float cpuAvgMs = 0.0f;
		float cpuP95Ms = 0.0f;
		float cpuP99Ms = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
		bool activeGpu = false;
		bool activeCpu = false;
		bool valid = false;

		const float* historyBuffer = nullptr;
		uint32_t historyHead = 0;
		uint32_t historyCount = 0;
		const float* cpuHistoryBuffer = nullptr;
		uint32_t cpuHistoryHead = 0;
		uint32_t cpuHistoryCount = 0;
		// Inclusive feature-root histories remain aligned with the self-time histories.
		const float* outermostGpuHistoryBuffer = nullptr;
		const float* outermostCpuHistoryBuffer = nullptr;

		float GetHistorySample(uint32_t index) const
		{
			if (!historyBuffer || index >= historyCount)
				return 0.0f;
			return historyBuffer[(historyHead - historyCount + index + kHistorySize) % kHistorySize];
		}

		float GetCpuHistorySample(uint32_t index) const
		{
			if (!cpuHistoryBuffer || index >= cpuHistoryCount)
				return 0.0f;
			return cpuHistoryBuffer[(cpuHistoryHead - cpuHistoryCount + index + kHistorySize) % kHistorySize];
		}

		/** @brief Reads GPU work not already covered by an ancestor in this timer root. */
		float GetOutermostGpuHistorySample(uint32_t index) const
		{
			if (!outermostGpuHistoryBuffer || index >= historyCount)
				return 0.0f;
			return outermostGpuHistoryBuffer[(historyHead - historyCount + index + kHistorySize) % kHistorySize];
		}

		/** @brief Reads CPU work not already covered by an ancestor in this timer root. */
		float GetOutermostCpuHistorySample(uint32_t index) const
		{
			if (!outermostCpuHistoryBuffer || index >= cpuHistoryCount)
				return 0.0f;
			return outermostCpuHistoryBuffer[(cpuHistoryHead - cpuHistoryCount + index + kHistorySize) % kHistorySize];
		}
	};

	struct ExternalGpuTiming
	{
		std::string name;
		RollingHistory history;
		uint32_t frameId = 0;
	};

	enum class CaptureSessionState : uint32_t
	{
		None = 0,
		Running = 1,
		Completed = 2,
		Cancelled = 3
	};

	struct CaptureSessionProgress
	{
		uint64_t sessionId = 0;
		CaptureSessionState state = CaptureSessionState::None;
		uint32_t requestedFrames = 0;
		uint32_t submittedFrames = 0;
		uint32_t resolvedFrames = 0;
		// A resolved frame can contain missing or invalid query data.
		uint32_t invalidGpuFrames = 0;
		uint32_t invalidCpuFrames = 0;
	};

	/** @brief Recreates queries while retaining the user capture preference. */
	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
	void Release();
	void SetUserEnabled(bool a_enabled);
	bool IsUserEnabled() const { return userEnabled.load(std::memory_order_acquire); }
	/** @brief Requests sources for the next frame; concurrent requests combine. */
	void RequestCapture(CaptureMode a_mode = CaptureMode::DetailedPasses);
	void SetCaptureModeLimit(CaptureMode mode);
	CaptureMode GetCaptureModeLimit() const { return captureModeLimit.load(std::memory_order_acquire); }
	bool IsWholeFrameCaptureActive() const { return seFrameTiming.IsCaptureActive(); }
	bool IsDetailedCaptureActive() const { return IsEnabled(); }
	/** Render-thread request for the next capture frame; false immediately releases target queries. */
	void RequestRenderTargetCapture(bool requested);
	/** Allocation-free draw-path gate; target intervals are independent of pass totals. */
	bool IsRenderTargetCaptureActive() const { return IsEnabled() && renderTargetCaptureActive; }
	/** Record one engine draw using the active profiler frame's shared GPU clock. */
	void CountRenderTargetDraw(uint32_t depthStencil, uint32_t renderTarget);
	/** Owner-thread published view, sharing the existing frame clock but no pass slots. */
	const RenderTargetStats& GetRenderTargetStats() const { return renderTargetStats; }
	/** Number of retained target timestamps; zero after capture is stopped. */
	uint32_t GetRenderTargetQueryCount() const;
	/** Optionally align newly observed scopes with preceding valid inactive frames. */
	bool StartBoundedCapture(uint32_t a_frameCount, bool a_clearHistory, uint64_t& a_sessionId, bool a_alignInactiveFrames = false);
	bool CancelBoundedCapture(uint64_t a_sessionId);
	CaptureSessionProgress GetBoundedCaptureProgress() const;
	/** @brief Owner-thread view; false skips statistics for count/history-only readers. */
	const std::vector<TimerResult>* GetBoundedCaptureResults(uint64_t a_sessionId, bool includeStatistics = true) const;
	bool IsEnabled() const { return IsUserEnabled() && GetCaptureModeLimit() == CaptureMode::DetailedPasses && captureActive.load(std::memory_order_acquire); }
	bool IsInitialized() const { return initialized; }
	/** @brief Thread-safe token for active GPU capture; zero disables external queries. */
	uint64_t GetGpuCaptureEpoch() const { return IsEnabled() ? gpuCaptureEpoch.load(std::memory_order_acquire) : 0; }
	/** @brief Accepts queue-ordered contributions; the next frame publishes their sum outside D3D11 totals. */
	bool PublishExternalGpuTiming(uint64_t epoch, std::string_view name, uint32_t frameId, float milliseconds);
	/** @brief Copies external histories so a submission thread cannot invalidate UI samples. */
	std::vector<ExternalGpuTiming> GetExternalGpuTimings() const;

	void SetPerfEventCallbacks(PerfEventCallback beginCb, PerfEventCallback endCb)
	{
		beginPerfEvent = std::move(beginCb);
		endPerfEvent = std::move(endCb);
	}

	void BeginFrame();
	bool BeginPass(std::string_view name, bool fireCallbacks = true, const PassTimingHandle& capture = {});
	void EndPass(bool fireCallbacks = true);
	/** @brief Captures a detail scope without changing legacy nesting, slots, or aggregate rows. */
	bool BeginDetailPass(std::string_view name, const PassTimingHandle& capture) noexcept;
	void EndDetailPass() noexcept;
	bool BeginCpuPass(std::string_view name);
	void EndCpuPass();
	void EndFrame(uint32_t a_frameCount);
	/** @brief Records the engine frame identity without changing Present ownership. */
	void SetEngineFrameCount(uint32_t frame) { engineFrameCount = frame; }
	/** @brief Must immediately bracket the actual DXGI Present. Test Presents leave capture open. */
	void BeginPresent(UINT syncInterval, UINT flags);
	void CompletePresent(HRESULT result);

	float GetWholeFrameGpuTimeMs() const { return seFrameTiming.GetWholeFrameGpuTimeMs(); }
	float GetWholeFrameCpuTimeMs() const { return seFrameTiming.GetWholeFrameCpuTimeMs(); }
	float GetPresentIntervalMs() const { return seFrameTiming.GetPresentIntervalMs(); }
	float GetWholeFrameGpuTimeAverageMs(uint32_t maxSamples = 60) const { return seFrameTiming.GetWholeFrameGpuTimeAverageMs(maxSamples); }
	float GetWholeFrameCpuTimeAverageMs(uint32_t maxSamples = 60) const { return seFrameTiming.GetWholeFrameCpuTimeAverageMs(maxSamples); }
	float GetPresentIntervalAverageMs(uint32_t maxSamples = 60) const { return seFrameTiming.GetPresentIntervalAverageMs(maxSamples); }
	bool HasWholeFrameGpuTime() const { return seFrameTiming.HasWholeFrameGpuTime(); }
	bool HasWholeFrameCpuTime() const { return seFrameTiming.HasWholeFrameCpuTime(); }
	bool HasPresentInterval() const { return seFrameTiming.HasPresentInterval(); }
	bool HasLatestWholeFrameGpuSample() const { return seFrameTiming.HasLatestWholeFrameGpuSample(); }
	bool HasLatestWholeFrameCpuSample() const { return seFrameTiming.HasLatestWholeFrameCpuSample(); }
	bool HasLatestPresentIntervalSample() const { return seFrameTiming.HasLatestPresentIntervalSample(); }
	bool WasLatestPresentIntervalSynced() const { return seFrameTiming.WasLatestPresentIntervalSynced(); }
	uint64_t GetWholeFrameSampleId() const { return seFrameTiming.GetWholeFrameSampleId(); }
	uint64_t GetWholeFrameGpuSampleId() const { return seFrameTiming.GetWholeFrameGpuSampleId(); }
	uint64_t GetWholeFrameCpuSampleId() const { return seFrameTiming.GetWholeFrameCpuSampleId(); }
	uint64_t GetWholeFramePresentIntervalSampleId() const { return seFrameTiming.GetWholeFramePresentIntervalSampleId(); }
	uint64_t GetPresentIntervalSampleId() const { return seFrameTiming.GetPresentIntervalSampleId(); }
	// Monotonic capture-integrity diagnostics. Measurements compare boundary
	// snapshots without interpreting a missing timing value as zero.
	uint64_t GetPresentDiscontinuityEpoch() const { return seFrameTiming.GetPresentDiscontinuityEpoch(); }
	uint64_t GetSkippedWholeFrameCaptureCount() const { return seFrameTiming.GetSkippedWholeFrameCaptureCount(); }

	/** @brief Owner-thread view; false leaves statistics unspecified and avoids calculating them. */
	const std::vector<TimerResult>& GetResults(bool includeStatistics = true) const;
	/** @brief Changes when live histories are reset or removed, invalidating display caches. */
	uint64_t GetHistoryEpoch() const { return historyEpoch; }
	/** @brief Returns the namespace used to group and aggregate a timer name. */
	static std::string_view GetTimerRootName(std::string_view name);
	float GetProfiledPassGpuTotalMs() const { return profiledPassGpuTotalHistory.lastMs; }
	float GetProfiledPassCpuTotalMs() const { return profiledPassCpuTotalHistory.lastMs; }
	float GetProfiledPassGpuTotalAverageMs(uint32_t maxSamples = 60) const { return profiledPassGpuTotalHistory.GetAverage(maxSamples); }
	float GetProfiledPassCpuTotalAverageMs(uint32_t maxSamples = 60) const { return profiledPassCpuTotalHistory.GetAverage(maxSamples); }
	float GetTotalTimeMs() const { return totalTimeMs; }
	float GetCpuTotalTimeMs() const { return cpuTotalTimeMs; }
	float GetResolvedTotalTimeMs() const { return resolvedTotalMs; }
	float GetResolvedCpuTotalTimeMs() const { return resolvedCpuTotalMs; }
	uint32_t GetCapturedFrameCount() const { return capturedFrameCount; }
	uint32_t GetAcquiredSlots() const { return acquiredSlots; }
	uint32_t GetPeakAcquiredSlots() const { return peakAcquiredSlots; }
	uint32_t GetSlotRefusals() const { return slotRefusals; }
	/** @brief Owner-thread CPU view, independent of GPU queries; false skips statistics. */
	const std::vector<TimerResult>& GetImmediateCpuResults(bool includeStatistics = true) const;
	/** @brief Engine frame stamped on the retained independent CPU publication. */
	uint32_t GetCapturedCpuFrameCount() const { return capturedCpuFrameCount; }
	/** @brief CPU publications since initialization or history reset; zero means no data. */
	uint64_t GetCpuPublicationCount() const { return cpuPublicationCount; }
	/** @brief Sum of CPU self times in the retained publication, including while idle. */
	float GetImmediateCpuTotalTimeMs() const { return immediateCpuTotalMs; }
	/** @brief Cumulative CPU-only/fallback capacity refusals since initialization. */
	uint32_t GetCpuSlotRefusals() const { return cpuSlotRefusals; }
	bool IsCpuCaptureActive() const { return IsEnabled() && Captures(CaptureMode::CPU); }
	void ClearTimers();
	void ClearTimersForFeature(const std::string& featureName);

	class ScopedPass
	{
	public:
		ScopedPass(Profiler* a_profiler, std::string_view a_name)
		{
			if (a_profiler && a_profiler->BeginPass(a_name)) {
				profiler = a_profiler;
			}
		}

		~ScopedPass()
		{
			if (profiler) {
				profiler->EndPass();
			}
		}

		ScopedPass(const ScopedPass&) = delete;
		ScopedPass& operator=(const ScopedPass&) = delete;
		ScopedPass(ScopedPass&&) = delete;
		ScopedPass& operator=(ScopedPass&&) = delete;

	private:
		Profiler* profiler = nullptr;
	};

	class ScopedCpuPass
	{
	public:
		ScopedCpuPass(Profiler* a_profiler, std::string_view a_name)
		{
			if (a_profiler && a_profiler->IsEnabled() && a_profiler->BeginCpuPass(a_name)) {
				profiler = a_profiler;
			}
		}

		~ScopedCpuPass()
		{
			if (profiler) {
				profiler->EndCpuPass();
			}
		}

		ScopedCpuPass(const ScopedCpuPass&) = delete;
		ScopedCpuPass& operator=(const ScopedCpuPass&) = delete;
		ScopedCpuPass(ScopedCpuPass&&) = delete;
		ScopedCpuPass& operator=(ScopedCpuPass&&) = delete;

	private:
		Profiler* profiler = nullptr;
	};

private:
	struct ActiveTimerData
	{
		float gpuMs = 0.0f;
		float topLevelMs = 0.0f;
		float cpuMs = 0.0f;
		float outermostGpuMs = 0.0f;
		float outermostCpuMs = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
	};

	struct CompletedCpuTimer
	{
		std::string name;
		float cpuMs = 0.0f;
		float cpuSelfMs = 0.0f;
		bool outermostCpuInRoot = true;
	};

	struct FrameQueries
	{
		RenderTargetStats::Frame renderTargets;
		winrt::com_ptr<ID3D11Query> disjoint;
		bool wholeFrameCaptured = false;
		bool capturedGpu = false;
		struct TimerPair
		{
			winrt::com_ptr<ID3D11Query> begin;
			winrt::com_ptr<ID3D11Query> end;
			std::string name;
			LARGE_INTEGER cpuBegin{};
			float cpuMs = 0.0f;
			float cpuSelfMs = 0.0f;
			double nestedCpuMs = 0.0;
			uint64_t cpuOrdinal = 0;
			int32_t parentSlot = -1;
			uint32_t depth = 0;
			bool ended = false;
			bool outermostGpuInRoot = true;
			bool outermostCpuInRoot = true;
			PassTimingHandle capture;
		};
		std::vector<TimerPair> timers;
		std::vector<TimerPair> detailTimers;
		std::vector<CompletedCpuTimer> cpuTimers;
		std::vector<uint32_t> activeTimerStack;
		std::vector<uint32_t> activeDetailStack;
		uint32_t activeCount = 0;
		uint32_t detailCount = 0;
		uint32_t capturedFrame = 0;
		uint64_t captureSessionId = 0;
		bool capturedCpu = false;
		bool gpuTimingsIncomplete = false;
		bool cpuTimingsIncomplete = false;
		bool inFlight = false;
	};

	ID3D11DeviceContext* context = nullptr;
	RenderTargetStats renderTargetStats;
	bool renderTargetCaptureRequested = false;
	bool renderTargetCaptureActive = false;
	bool renderTargetCaptureRetained = false;
	bool renderTargetFrameUnavailable = false;
	void StopRenderTargetCapture();

	FrameQueries frames[kFrameLatency];
	SEFrameTiming seFrameTiming;
	uint32_t engineFrameCount = 0;
	std::atomic<CaptureMode> captureModeLimit{ CaptureMode::DetailedPasses };
	RollingHistory profiledPassGpuTotalHistory;
	RollingHistory profiledPassCpuTotalHistory;
	void BeginCaptureFrame(bool atPresentBoundary);
	uint32_t writeFrame = 0;
	uint32_t readFrame = 0;
	bool initialized = false;
	bool frameActive = false;
	std::atomic_bool userEnabled{ false };
	std::atomic<uint8_t> captureRequested{ 0 };
	std::atomic_bool captureActive{ false };
	CaptureMode activeCaptureMode = CaptureMode::None;
	std::atomic<uint64_t> gpuCaptureEpoch{ 0 };
	uint64_t nextGpuCaptureEpoch = 0;
	mutable std::mutex externalGpuMutex;
	struct PendingExternalGpuTiming
	{
		ExternalGpuTiming published;
		uint32_t pendingFrameId;
		float pendingMilliseconds;
	};
	std::vector<PendingExternalGpuTiming> externalGpuTimings;
	void UpdateGpuCaptureEpoch(bool active, bool reset = false);
	uint64_t activeCaptureSessionId = 0;
	bool gpuAcquisitionBlocked = false;
	// Each successful BeginPass owns either a GPU interval or a CPU fallback scope.
	std::vector<bool> activePassUsesGpu;
	double cpuTicksToMs = 0.0;

	PerfEventCallback beginPerfEvent;
	PerfEventCallback endPerfEvent;

	mutable std::vector<TimerResult> results;
	mutable bool resultsStatisticsDirty = true;
	uint64_t historyEpoch = 0;

	struct CpuTimer
	{
		std::string name;
		LARGE_INTEGER cpuBegin{};
		double nestedCpuMs = 0.0;
		uint64_t ordinal = 0;
		bool outermostCpuInRoot = true;
	};

	struct KnownTimer
	{
		std::string name;
		RollingHistory gpu;
		RollingHistory cpu;
		RollingHistory outermostGpu;
		RollingHistory outermostCpu;
		bool hasGpu = false;
		bool hasCpu = false;
		uint64_t lastSampleCycle = 0;
	};
	struct CaptureKnownTimer
	{
		std::string name;
		RollingHistory gpu;
		RollingHistory cpu;
		RollingHistory outermostGpu;
		RollingHistory outermostCpu;
		float topLevelMs = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
	};
	std::vector<KnownTimer> knownTimers;
	std::unordered_map<std::string, size_t> knownTimerIndex;
	uint64_t collectedDetailedCycles = 0;
	std::vector<CpuTimer> activeCpuTimers;
	std::vector<CompletedCpuTimer> completedCpuTimers;
	uint64_t nextCpuOrdinal = 0;
	float totalTimeMs = 0.0f;
	float cpuTotalTimeMs = 0.0f;
	// Resolve-consistent totals remain paired with results while live totals idle at zero.
	float resolvedTotalMs = 0.0f;
	float resolvedCpuTotalMs = 0.0f;
	uint32_t capturedFrameCount = 0;
	uint32_t acquiredSlotsThisFrame = 0;
	uint32_t acquiredSlots = 0;
	uint32_t peakAcquiredSlots = 0;
	uint32_t slotRefusals = 0;
	uint64_t nextCaptureSessionId = 1;
	CaptureSessionProgress boundedCapture;
	bool boundedCaptureAlignInactiveFrames = false;
	std::vector<CaptureKnownTimer> boundedCaptureTimers;
	std::unordered_map<std::string, size_t> boundedCaptureTimerIndex;
	mutable std::vector<TimerResult> boundedCaptureResults;
	mutable bool boundedStatisticsDirty = true;

	struct ImmediateCpuTimer
	{
		std::string name;
		RollingHistory history;
		RollingHistory outermost;
		uint64_t lastSampleCycle = 0;
		bool active = false;
	};
	std::vector<ImmediateCpuTimer> immediateCpuTimers;
	std::unordered_map<std::string, size_t> immediateCpuTimerIndex;
	mutable std::vector<TimerResult> immediateCpuResults;
	mutable bool immediateCpuStatisticsDirty = true;
	uint32_t capturedCpuFrameCount = 0;
	uint64_t cpuPublicationCount = 0;
	float immediateCpuTotalMs = 0.0f;
	uint32_t cpuSlotRefusals = 0;

	bool Captures(CaptureMode a_mode) const
	{
		return (static_cast<uint8_t>(activeCaptureMode) & static_cast<uint8_t>(a_mode)) != 0;
	}
	void LatchCaptureRequest();
	bool BeginFallbackCpuPass(std::string_view name, bool fireCallbacks);
	void PublishImmediateCpuResults(uint32_t a_frameCount);
	void RebuildImmediateCpuResults();
	void ClearImmediateCpuResults();

	bool CollectResults();
	PassTimingHandle ClaimCapture(const PassTimingHandle& capture, bool detailOnly) noexcept;
	static void InvalidateCapture(const PassTimingHandle& capture, const char* reason) noexcept;
	static void CompleteCapturedCpu(FrameQueries::TimerPair& timer) noexcept;
	static void CompleteCapturedGpu(FrameQueries::TimerPair& timer, double inclusiveMs, double selfMs, const char* failure) noexcept;
	static void BindCapturedFrame(FrameQueries::TimerPair& timer, uint32_t frame) noexcept;
	static void CancelFrameCaptures(FrameQueries& frame, const char* reason) noexcept;
	KnownTimer& GetOrCreateTimer(const std::string& name);
	void RetireStaleTimers();
	void RebuildTimerIndex();
	void RebuildResults(const std::unordered_map<std::string, ActiveTimerData>* activeTimers);
	void StoreBoundedCaptureResults(
		const std::unordered_map<std::string, ActiveTimerData>& a_activeTimers,
		bool a_gpuCycleResolved,
		bool a_cpuCycleResolved);
	void RebuildBoundedCaptureResults();
	void StoreCompletedCpuTimers(FrameQueries& frame);
	bool HasActiveGpuAncestorWithSameRoot(std::string_view name) const;
	bool HasActiveCpuAncestorWithSameRoot(std::string_view name) const;
	void AddCpuChildTime(uint64_t childOrdinal, double coveredMs);
	void ResetFrameState(FrameQueries& frame);
	void ResetPendingFrames();
	static bool HasPendingFrameData(const FrameQueries& frame);
};

#define CS_PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define CS_PROFILE_SCOPE_CONCAT(a, b) CS_PROFILE_SCOPE_CONCAT_INNER(a, b)
#define CS_PROFILE_SCOPE(name) Profiler::ScopedPass CS_PROFILE_SCOPE_CONCAT(csProfileScope_, __LINE__)(globals::profiler, name)
#define CS_PROFILE_CPU_SCOPE(name) Profiler::ScopedCpuPass CS_PROFILE_SCOPE_CONCAT(csCpuProfileScope_, __LINE__)(globals::profiler, name)
