#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include <cstddef>
#	include <cstdint>
#	include <filesystem>
#	include <functional>
#	include <memory>
#	include <string>
#	include <string_view>
#	include <vector>

/** D3D11 immediate-context hardware counters for developer builds.
 * Captures use live frames, not deterministic replay. D3D12 NR eye queues
 * are not instrumented; timing totals and per-eye concurrency are not inferred.
 */
namespace NvPerf
{
	inline constexpr std::size_t MaxMetrics = 32;
	inline constexpr std::size_t MaxFilters = 32;
	inline constexpr std::size_t MaxNameLength = 240;
	inline constexpr std::uint32_t MaxSamples = 64;
	inline constexpr std::uint32_t MaxDelayFrames = 600;
	inline constexpr std::uint16_t MaxNestingLevels = 8;

	struct PassFrame
	{
		std::uint32_t sample;
		std::size_t pass;
		std::uint64_t frame;
	};
	/// One metric as the metrics evaluator names it (e.g. "sm__throughput"), with the evaluator's enums.
	struct MetricRequest
	{
		std::string name;
		std::uint8_t metricType = 0;  // NVPW_MetricType
		std::uint8_t rollupOp = 0;    // NVPW_RollupOp
		std::uint16_t submetric = 0;  // NVPW_Submetric
	};

	struct CaptureConfiguration
	{
		std::vector<MetricRequest> metrics;  // empty: DefaultMetrics()
		// Range names to measure; a trailing '*' matches every name with that prefix. Empty: every range.
		std::vector<std::string> ranges;
		std::uint32_t samples = 1;        // frames' worth of counters to collect, one after another
		std::uint32_t delayFrames = 0;    // frames to wait before the first pass
		std::uint16_t nestingLevels = 1;  // selected ranges nested this deep are measured; each level adds passes
	};

	/// One measured range of one sample; values follow CaptureResult::metrics.
	struct RangeResult
	{
		std::uint32_t sample = 0;
		std::string name;                    // the range's own name
		std::string path;                    // its enclosing selected ranges and itself, joined with " / "
		std::vector<std::string> enclosing;  // its enclosing selected ranges, outermost first
		std::uint32_t occurrence = 0;
		std::vector<double> values;
	};

	/// The ranges matching one filter (or one name, with no filters), per frame: counters summed, rates weighted by each
	/// range's gpu__time_duration. A range nested in one that matches the same filter is already part of it and is left
	/// out. A value a replay pass could not complete (NaN) is left out and counted.
	struct RangeSummary
	{
		std::string name;
		double occurrencesPerFrame = 0.0;
		std::uint32_t incomplete = 0;
		std::vector<double> values;
	};

	struct CaptureResult
	{
		std::uint64_t id = 0;
		bool success = false;
		std::string error;
		std::string chipName;
		std::uint64_t startFrame = 0;
		std::uint64_t endFrame = 0;
		std::uint32_t samples = 0;
		std::size_t passesPerSample = 0;
		std::string buildId;
		std::vector<PassFrame> frames;
		std::uint64_t incompleteRanges = 0;
		std::uint64_t unbalancedRanges = 0;
		bool csvWritten = false;
		std::string exportError;
		std::uint64_t droppedRanges = 0;
		std::uint64_t droppedTraceBytes = 0;
		std::vector<std::string> ranges;  // the requested filters
		std::vector<MetricRequest> metrics;
		std::vector<MetricRequest> unsupportedMetrics;
		std::vector<RangeResult> rangeResults;
		std::vector<RangeSummary> summary;
		std::filesystem::path csvPath;
	};

	enum class CaptureState
	{
		Idle,
		Waiting,    // requested; waiting for its first pass
		Capturing,  // passes are being submitted
		Decoding,   // every pass of a sample is submitted; waiting for the GPU
		Done,
		Failed,
	};

	struct Status
	{
		CaptureState state = CaptureState::Idle;
		std::uint64_t captureId = 0;
		std::uint32_t sample = 0;
		std::uint32_t samples = 0;
		std::size_t pass = 0;
		std::size_t passes = 0;
		std::string error;
	};

	struct DeviceInfo
	{
		bool built = false;      // compiled with the SDK
		bool probed = false;     // Probe() has run
		bool available = false;  // library loaded and initialized
		bool gpuSupported = false;
		std::string deviceName;
		std::string chipName;
		std::string error;
		std::filesystem::path libraryPath;
	};

	/// Queues device probing on the render thread; returns the last known support state.
	bool Probe();
	DeviceInfo GetDeviceInfo();

	/// Time, throughput of the main units, the instruction mix and the warp stall reasons.
	std::vector<MetricRequest> DefaultMetrics();
	/// Parses "name[:counter|ratio|throughput|active|elapsed|percycle]" (default counter).
	bool ParseMetric(std::string_view text, MetricRequest& metric, std::string& error);
	/// The inverse of ParseMetric.
	std::string FormatMetric(const MetricRequest& metric);
	/// The name a metric's values are reported under (e.g. "sm__throughput.avg.pct_of_peak_sustained_elapsed").
	std::string MetricLabel(const MetricRequest& metric);

	/// Queues a capture; it starts at the next Present. Fails while another is pending or running.
	bool RequestCapture(const CaptureConfiguration& configuration, std::uint64_t& captureId, std::string& error);
	/// Cancels only the matching pending/running capture; returns false for a stale id.
	bool CancelCapture(std::uint64_t captureId);
	Status GetStatus();
	std::shared_ptr<const CaptureResult> LatestResult();
	/// Called on the render thread whenever a capture completes or fails.
	void SetCompletionCallback(std::function<void(const CaptureResult&)> callback);
	std::filesystem::path CsvPath();

	/// Render thread: before the frame's Present (ends the frame's pass, so the overlay is not measured).
	void OnPresentBegin() noexcept;
	/// Render thread: after the frame's Present (decodes, then begins the next frame's pass).
	void OnPresentEnd() noexcept;

	/// Whether a pass is open, so ranges are recorded. Cheap: called on every perf event.
	bool RangesActive() noexcept;
	/// A range on the immediate context, measured when the capture selects its name. Pair with PopRange.
	void PushRange(std::string_view name) noexcept;
	void PopRange() noexcept;
	/// Developer-only profiling controls and DevBench registration.
	void DrawSettings();
	void InstallDevBench();
}
#endif
