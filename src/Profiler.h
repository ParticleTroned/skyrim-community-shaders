#pragma once

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

class Profiler
{
public:
	static constexpr uint32_t kMaxTimers = 128;
	// Keep enough independent query slots for a busy GPU without blocking the
	// render thread or immediately dropping the next whole-frame capture.
	static constexpr uint32_t kFrameLatency = 8;
	static constexpr uint32_t kHistorySize = 300;
	// Retain intermittent passes while removing entries that remain absent across
	// a sustained detailed-profiling capture.
	static constexpr uint64_t kTimerRetireCycles = 60;
	// Longer accepted-Present gaps are treated as a timing-source discontinuity
	// (pause, loading, or alt-tab), not as an ordinary in-scene hitch sample.
	static constexpr float kMaxPresentIntervalMs = 1000.0f;

	using PerfEventCallback = std::function<void(std::string_view)>;

	enum class CaptureMode : uint8_t
	{
		None,
		// Collect whole-frame GPU/CPU boundaries without named pass scopes.
		// Direct Present intervals remain independent and always-on.
		WholeFrameOnly,
		// Also execute and collect named GPU/CPU pass scopes.
		DetailedPasses
	};

	struct RollingHistory
	{
		float history[kHistorySize]{};
		uint32_t head = 0;
		uint32_t count = 0;
		float lastMs = 0.0f;

		void PushSample(float ms)
		{
			history[head] = ms;
			head = (head + 1) % kHistorySize;
			if (count < kHistorySize)
				count++;
			lastMs = ms;
		}

		float GetAverage() const;
		float GetAverage(uint32_t maxSamples) const;
		float GetPercentile(float p) const;
		float GetSample(uint32_t index) const
		{
			if (index >= count)
				return 0.0f;
			return history[(head - count + index + kHistorySize) % kHistorySize];
		}
	};

	struct TimerResult
	{
		std::string name;
		float gpuTimeMs = 0.0f;
		float avgMs = 0.0f;
		float p95Ms = 0.0f;
		float p99Ms = 0.0f;
		float cpuTimeMs = 0.0f;
		float cpuAvgMs = 0.0f;
		float cpuP95Ms = 0.0f;
		float cpuP99Ms = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
		bool valid = false;

		const float* historyBuffer = nullptr;
		uint32_t historyHead = 0;
		uint32_t historyCount = 0;
		const float* cpuHistoryBuffer = nullptr;
		uint32_t cpuHistoryHead = 0;
		uint32_t cpuHistoryCount = 0;

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
	};

	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
	void Release();
	void SetUserEnabled(bool a_enabled);
	bool IsUserEnabled() const { return userEnabled.load(std::memory_order_acquire); }
	void SetCaptureModeLimit(CaptureMode a_mode);
	CaptureMode GetCaptureModeLimit() const { return captureModeLimit.load(std::memory_order_acquire); }
	void RequestCapture(CaptureMode a_mode = CaptureMode::DetailedPasses);
	bool IsWholeFrameCaptureActive() const
	{
		return IsUserEnabled() && captureActive.load(std::memory_order_acquire) != CaptureMode::None;
	}
	bool IsDetailedCaptureActive() const
	{
		return IsUserEnabled() && captureActive.load(std::memory_order_acquire) == CaptureMode::DetailedPasses;
	}

	void SetPerfEventCallbacks(PerfEventCallback beginCb, PerfEventCallback endCb)
	{
		beginPerfEvent = std::move(beginCb);
		endPerfEvent = std::move(endCb);
	}

	void BeginFrame();
	bool BeginPass(std::string_view name);
	void EndPass();
	bool BeginCpuPass(std::string_view name);
	void EndCpuPass();
	// These calls must immediately bracket the actual DXGI Present. A direct
	// Present interval is committed only after a successful, non-test, visible Present.
	void BeginPresent(UINT syncInterval, UINT presentFlags);
	void CompletePresent(HRESULT presentResult);

	const std::vector<TimerResult>& GetResults() const { return results; }
	float GetProfiledPassGpuTotalMs() const { return profiledPassGpuTotalMs; }
	float GetProfiledPassCpuTotalMs() const { return profiledPassCpuTotalMs; }
	float GetProfiledPassGpuTotalAverageMs(uint32_t maxSamples = 60) const { return profiledPassGpuTotalHistory.GetAverage(maxSamples); }
	float GetProfiledPassCpuTotalAverageMs(uint32_t maxSamples = 60) const { return profiledPassCpuTotalHistory.GetAverage(maxSamples); }
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
			if (a_profiler && a_profiler->BeginCpuPass(a_name)) {
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
		float cpuMs = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
	};

	struct CompletedCpuTimer
	{
		std::string name;
		float cpuMs = 0.0f;
		bool hasCpuTime = false;
	};

	struct FrameQueries
	{
		winrt::com_ptr<ID3D11Query> disjoint;
		winrt::com_ptr<ID3D11Query> wholeFrameBegin;
		winrt::com_ptr<ID3D11Query> wholeFrameEnd;
		struct TimerPair
		{
			winrt::com_ptr<ID3D11Query> begin;
			winrt::com_ptr<ID3D11Query> end;
			std::string name;
			LARGE_INTEGER cpuBegin{};
			float cpuMs = 0.0f;
			bool hasCpuBegin = false;
			bool hasCpuTime = false;
			bool ended = false;
		};
		std::vector<TimerPair> timers;
		std::vector<CompletedCpuTimer> cpuTimers;
		std::vector<uint32_t> activeTimerStack;
		uint32_t activeCount = 0;
		CaptureMode captureMode = CaptureMode::None;
		LARGE_INTEGER wholeFrameCpuBegin{};
		float wholeFrameCpuMs = 0.0f;
		bool hasWholeFrameCpuBegin = false;
		bool hasWholeFrameCpuTime = false;
		bool acceptedPresent = false;
		uint64_t presentIntervalSampleId = 0;
		bool inFlight = false;
	};

	ID3D11DeviceContext* context = nullptr;

	FrameQueries frames[kFrameLatency];
	uint32_t writeFrame = 0;
	uint32_t readFrame = 0;
	uint32_t framesSinceInit = 0;
	bool initialized = false;
	bool frameActive = false;
	std::atomic_bool userEnabled{ false };
	std::atomic<CaptureMode> captureModeLimit{ CaptureMode::DetailedPasses };
	std::atomic<CaptureMode> captureRequested{ CaptureMode::None };
	std::atomic<CaptureMode> captureActive{ CaptureMode::None };
	double cpuTicksToMs = 0.0;

	PerfEventCallback beginPerfEvent;
	PerfEventCallback endPerfEvent;

	std::vector<TimerResult> results;

	struct CpuTimer
	{
		std::string name;
		LARGE_INTEGER cpuBegin{};
	};

	struct KnownTimer
	{
		std::string name;
		RollingHistory gpu;
		RollingHistory cpu;
		bool hasGpu = false;
		bool hasCpu = false;
		uint64_t lastSampleCycle = 0;
	};
	std::vector<KnownTimer> knownTimers;
	std::unordered_map<std::string, size_t> knownTimerIndex;
	uint64_t collectedDetailedCycles = 0;
	std::vector<CpuTimer> activeCpuTimers;
	std::vector<CompletedCpuTimer> completedCpuTimers;
	RollingHistory profiledPassGpuTotalHistory;
	RollingHistory profiledPassCpuTotalHistory;
	float profiledPassGpuTotalMs = 0.0f;
	float profiledPassCpuTotalMs = 0.0f;
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
	bool detailedPassGpuTimingAvailable = false;
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

	bool CollectResults();
	void PromoteRequestedCapture();
	KnownTimer& GetOrCreateTimer(const std::string& name);
	void RetireStaleTimers();
	void RebuildTimerIndex();
	void RebuildResults(const std::unordered_map<std::string, ActiveTimerData>* activeTimers);
	void StoreCompletedCpuTimers(FrameQueries& frame);
	void ResetFrameState(FrameQueries& frame);
	void ResetWholeFrameTimings();
	void RecordPresentDiscontinuity();
	static bool HasPendingFrameData(const FrameQueries& frame);
};

#define CS_PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define CS_PROFILE_SCOPE_CONCAT(a, b) CS_PROFILE_SCOPE_CONCAT_INNER(a, b)
#define CS_PROFILE_SCOPE(name) Profiler::ScopedPass CS_PROFILE_SCOPE_CONCAT(csProfileScope_, __LINE__)(globals::profiler, name)
#define CS_PROFILE_CPU_SCOPE(name) Profiler::ScopedCpuPass CS_PROFILE_SCOPE_CONCAT(csCpuProfileScope_, __LINE__)(globals::profiler, name)
