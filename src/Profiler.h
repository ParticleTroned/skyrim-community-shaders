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
	static constexpr uint32_t kFrameLatency = 3;
	static constexpr uint32_t kHistorySize = 300;

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
	void RequestCapture();
	bool IsEnabled() const { return IsUserEnabled() && captureActive.load(std::memory_order_acquire); }

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
	void EndFrame(UINT syncInterval);

	const std::vector<TimerResult>& GetResults() const { return results; }
	float GetTotalTimeMs() const { return totalTimeMs; }
	float GetCpuTotalTimeMs() const { return cpuTotalTimeMs; }
	float GetTotalTimeAverageMs(uint32_t maxSamples = 60) const { return totalGpuHistory.GetAverage(maxSamples); }
	float GetCpuTotalTimeAverageMs(uint32_t maxSamples = 60) const { return totalCpuHistory.GetAverage(maxSamples); }
	float GetWholeFrameGpuTimeMs() const { return wholeFrameGpuHistory.lastMs; }
	float GetWholeFrameCpuTimeMs() const { return wholeFrameCpuHistory.lastMs; }
	float GetFrameTimeMs() const { return frameTimeHistory.lastMs; }
	float GetWholeFrameGpuTimeAverageMs(uint32_t maxSamples = 60) const { return wholeFrameGpuHistory.GetAverage(maxSamples); }
	float GetWholeFrameCpuTimeAverageMs(uint32_t maxSamples = 60) const { return wholeFrameCpuHistory.GetAverage(maxSamples); }
	float GetFrameTimeAverageMs(uint32_t maxSamples = 60) const { return frameTimeHistory.GetAverage(maxSamples); }
	bool HasWholeFrameGpuTime() const { return wholeFrameGpuHistory.count > 0; }
	bool HasWholeFrameCpuTime() const { return wholeFrameCpuHistory.count > 0; }
	bool HasFrameTime() const { return frameTimeHistory.count > 0; }
	bool HasLatestWholeFrameGpuSample() const { return wholeFrameSampleId != 0 && wholeFrameGpuSampleId == wholeFrameSampleId; }
	bool HasLatestWholeFrameCpuSample() const { return wholeFrameSampleId != 0 && wholeFrameCpuSampleId == wholeFrameSampleId; }
	bool HasLatestFrameTimeSample() const { return wholeFrameSampleId != 0 && frameTimeSampleId == wholeFrameSampleId; }
	bool WasLatestFramePresentSynced() const { return HasLatestFrameTimeSample() && latestFrameWasPresentSynced; }
	uint64_t GetWholeFrameSampleId() const { return wholeFrameSampleId; }
	void ClearTimers();
	void ClearTimersForFeature(const std::string& featureName);

	class ScopedPass
	{
	public:
		ScopedPass(Profiler* a_profiler, std::string_view a_name)
		{
			if (a_profiler && a_profiler->IsEnabled() && a_profiler->BeginPass(a_name)) {
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
		float cpuMs = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
	};

	struct CompletedCpuTimer
	{
		std::string name;
		float cpuMs = 0.0f;
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
			bool ended = false;
		};
		std::vector<TimerPair> timers;
		std::vector<CompletedCpuTimer> cpuTimers;
		std::vector<uint32_t> activeTimerStack;
		uint32_t activeCount = 0;
		LARGE_INTEGER wholeFrameCpuBegin{};
		float wholeFrameCpuMs = 0.0f;
		float frameTimeMs = 0.0f;
		bool hasWholeFrameCpuTime = false;
		bool hasFrameTime = false;
		bool presentSynced = false;
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
	};

	struct KnownTimer
	{
		std::string name;
		RollingHistory gpu;
		RollingHistory cpu;
		bool hasGpu = false;
		bool hasCpu = false;
	};
	std::vector<KnownTimer> knownTimers;
	std::unordered_map<std::string, size_t> knownTimerIndex;
	std::vector<CpuTimer> activeCpuTimers;
	std::vector<CompletedCpuTimer> completedCpuTimers;
	RollingHistory totalGpuHistory;
	RollingHistory totalCpuHistory;
	float totalTimeMs = 0.0f;
	float cpuTotalTimeMs = 0.0f;
	RollingHistory wholeFrameGpuHistory;
	RollingHistory wholeFrameCpuHistory;
	RollingHistory frameTimeHistory;
	uint64_t wholeFrameSampleId = 0;
	uint64_t wholeFrameGpuSampleId = 0;
	uint64_t wholeFrameCpuSampleId = 0;
	uint64_t frameTimeSampleId = 0;
	int64_t wholeFrameLastPresentStartCounter = 0;
	bool wholeFrameGpuTimingAvailable = false;
	bool wholeFrameHasLastPresentStart = false;
	bool wholeFrameLastPresentSynced = false;
	bool latestFrameWasPresentSynced = false;

	bool CollectResults();
	KnownTimer& GetOrCreateTimer(const std::string& name);
	void RebuildResults(const std::unordered_map<std::string, ActiveTimerData>* activeTimers);
	void StoreCompletedCpuTimers(FrameQueries& frame);
	void ResetFrameState(FrameQueries& frame);
	void ResetWholeFrameTimings();
	static bool HasPendingFrameData(const FrameQueries& frame);
};

#define CS_PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define CS_PROFILE_SCOPE_CONCAT(a, b) CS_PROFILE_SCOPE_CONCAT_INNER(a, b)
#define CS_PROFILE_SCOPE(name) Profiler::ScopedPass CS_PROFILE_SCOPE_CONCAT(csProfileScope_, __LINE__)(globals::profiler, name)
#define CS_PROFILE_CPU_SCOPE(name) Profiler::ScopedCpuPass CS_PROFILE_SCOPE_CONCAT(csCpuProfileScope_, __LINE__)(globals::profiler, name)
