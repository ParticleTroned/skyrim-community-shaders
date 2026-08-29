#pragma once

#include <atomic>
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
	static constexpr uint32_t kFrameLatency = 3;
	static constexpr uint32_t kHistorySize = 300;
	// Retain intermittent passes while removing entries absent across sustained capture.
	static constexpr uint64_t kTimerRetireCycles = 60;

	using PerfEventCallback = std::function<void(std::string_view)>;

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
		float GetPercentile(float p) const;
	};

	struct TimerResult
	{
		std::string name;
		float gpuTimeMs = 0.0f;
		// Portion of gpuTimeMs contributed by depth-0 intervals this cycle.
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
	};

	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
	void Release();
	void SetUserEnabled(bool a_enabled);
	bool IsUserEnabled() const { return userEnabled.load(std::memory_order_acquire); }
	void RequestCapture();
	bool StartBoundedCapture(uint32_t a_frameCount, bool a_clearHistory, uint64_t& a_sessionId);
	bool CancelBoundedCapture(uint64_t a_sessionId);
	CaptureSessionProgress GetBoundedCaptureProgress() const;
	const std::vector<TimerResult>* GetBoundedCaptureResults(uint64_t a_sessionId) const;
	bool IsEnabled() const { return IsUserEnabled() && captureActive.load(std::memory_order_acquire); }
	bool IsInitialized() const { return initialized; }

	void SetPerfEventCallbacks(PerfEventCallback beginCb, PerfEventCallback endCb)
	{
		beginPerfEvent = std::move(beginCb);
		endPerfEvent = std::move(endCb);
	}

	void BeginFrame();
	bool BeginPass(std::string_view name, bool fireCallbacks = true);
	void EndPass(bool fireCallbacks = true);
	bool BeginCpuPass(std::string_view name);
	void EndCpuPass();
	void EndFrame(uint32_t a_frameCount);

	const std::vector<TimerResult>& GetResults() const { return results; }
	float GetTotalTimeMs() const { return totalTimeMs; }
	float GetCpuTotalTimeMs() const { return cpuTotalTimeMs; }
	float GetResolvedTotalTimeMs() const { return resolvedTotalMs; }
	float GetResolvedCpuTotalTimeMs() const { return resolvedCpuTotalMs; }
	uint32_t GetCapturedFrameCount() const { return capturedFrameCount; }
	uint32_t GetAcquiredSlots() const { return acquiredSlots; }
	uint32_t GetPeakAcquiredSlots() const { return peakAcquiredSlots; }
	uint32_t GetSlotRefusals() const { return slotRefusals; }
	void ClearTimers();
	void ClearTimersForFeature(const std::string& featureName);

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
		bool hasGpu = false;
		bool hasCpu = false;
	};

	struct CompletedCpuTimer
	{
		std::string name;
		float cpuMs = 0.0f;
		uint32_t depth = 0;
	};

	struct FrameQueries
	{
		winrt::com_ptr<ID3D11Query> disjoint;
		struct TimerPair
		{
			winrt::com_ptr<ID3D11Query> begin;
			winrt::com_ptr<ID3D11Query> end;
			std::string name;
			LARGE_INTEGER cpuBegin{};
			float cpuMs = 0.0f;
			uint32_t depth = 0;
			bool ended = false;
		};
		std::vector<TimerPair> timers;
		std::vector<CompletedCpuTimer> cpuTimers;
		std::vector<uint32_t> activeTimerStack;
		uint32_t activeCount = 0;
		uint32_t capturedFrame = 0;
		uint64_t captureSessionId = 0;
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
	std::atomic_bool captureRequested{ false };
	std::atomic_bool captureActive{ false };
	double cpuTicksToMs = 0.0;

	PerfEventCallback beginPerfEvent;
	PerfEventCallback endPerfEvent;

	std::vector<TimerResult> results;

	struct CpuTimer
	{
		std::string name;
		LARGE_INTEGER cpuBegin{};
		uint32_t depth = 0;
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
	struct CaptureKnownTimer
	{
		std::string name;
		RollingHistory gpu;
		RollingHistory cpu;
		float topLevelMs = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
	};
	std::vector<KnownTimer> knownTimers;
	std::unordered_map<std::string, size_t> knownTimerIndex;
	uint64_t collectedDetailedCycles = 0;
	std::vector<CpuTimer> activeCpuTimers;
	std::vector<CompletedCpuTimer> completedCpuTimers;
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
	std::vector<CaptureKnownTimer> boundedCaptureTimers;
	std::unordered_map<std::string, size_t> boundedCaptureTimerIndex;
	std::vector<TimerResult> boundedCaptureResults;

	bool CollectResults();
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
	void ResetFrameState(FrameQueries& frame);
	void ResetPendingFrames();
	static bool HasPendingFrameData(const FrameQueries& frame);
};

#define CS_PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define CS_PROFILE_SCOPE_CONCAT(a, b) CS_PROFILE_SCOPE_CONCAT_INNER(a, b)
#define CS_PROFILE_CPU_SCOPE(name) Profiler::ScopedCpuPass CS_PROFILE_SCOPE_CONCAT(csCpuProfileScope_, __LINE__)(globals::profiler, name)
