#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace PerformanceTuning
{
	inline constexpr std::size_t kMeasurementBlockCount = 5;
	inline constexpr double kMeasurementBlockDurationMs = 1000.0;
	inline constexpr double kMeasurementDurationMs =
		kMeasurementBlockCount * kMeasurementBlockDurationMs;
	inline constexpr uint32_t kMinimumPresentSampleCount = 24;
	inline constexpr double kMaximumPresentIntervalMs = 1000.0;
	inline constexpr double kSampleWeightEpsilon = 1.0e-6;
	inline constexpr double kDefaultMinimumMetricCoverage = 0.90;
	inline constexpr double kPracticalFloorAbsoluteMs = 0.10;
	inline constexpr double kPracticalFloorAbsoluteFps = 0.50;
	inline constexpr double kPracticalFloorRelative = 0.01;
	inline constexpr double kStatisticalSignificanceLevel = 0.05;

	struct Moments
	{
		double mean = 0.0;
		double sampleWeight = 0.0;

		bool Add(double value, double weight);
		std::optional<double> Mean() const;
	};

	struct PresentSampleContribution
	{
		double timingMs = 0.0;
		double totalWeight = 0.0;
		std::array<double, kMeasurementBlockCount> blockWeights{};
	};

	struct SampleBlock
	{
		double sampledDurationMs = 0.0;
		double outputPresentDurationMs = 0.0;
		double outputPresentedFrameCount = 0.0;
		Moments present;
		Moments wholeFrameGpu;
		Moments wholeFrameCpu;
		uint32_t presentSampleCount = 0;
		uint32_t wholeFrameGpuSampleCount = 0;
		uint32_t wholeFrameCpuSampleCount = 0;
	};

	struct SampleWindow
	{
		double sampledDurationMs = 0.0;
		double outputPresentDurationMs = 0.0;
		Moments present;
		Moments wholeFrameGpu;
		Moments wholeFrameCpu;
		std::array<SampleBlock, kMeasurementBlockCount> blocks{};
		uint32_t presentSampleCount = 0;
		uint32_t presentSyncedSampleCount = 0;
		double framePacedSampleWeight = 0.0;
		double framePacingEligibleSampleWeight = 0.0;
		bool framePacingInferenceValid = true;
		bool complete = false;
		bool presentSourceDiscontinuous = false;
		uint64_t startPresentSampleId = 0;
		uint64_t endPresentSampleId = 0;
		uint64_t lastPresentSampleId = 0;
		uint64_t lastWholeFrameSampleId = 0;
		uint64_t lastOutputPresentSampleId = 0;
		uint64_t outputPresentDiscontinuityEpoch = 0;
		uint64_t latestWholeFramePresentSampleId = 0;
		bool wholeFrameSourceDiscontinuous = false;
		bool wholeFrameGpuCoverageDiscontinuous = false;
		bool wholeFrameCpuCoverageDiscontinuous = false;
		bool outputPresentSourceDiscontinuous = false;
		std::unordered_map<uint64_t, PresentSampleContribution> presentSamples;
		std::unordered_set<uint64_t> wholeFrameGpuPresentSampleIds;
		std::unordered_set<uint64_t> wholeFrameCpuPresentSampleIds;
	};

	enum class AddSampleResult
	{
		Ignored,
		Added,
		Complete,
		SourceGap,
		SourceReset,
		DuplicateAssociation,
		InvalidValue,
		IntervalTooLarge
	};

	void BeginSampleWindow(
		SampleWindow& window,
		uint64_t currentPresentSampleId,
		uint64_t currentWholeFrameSampleId,
		bool framePacingInferenceValid = true,
		uint64_t currentOutputPresentSampleId = 0,
		uint64_t outputPresentDiscontinuityEpoch = 0);

	AddSampleResult AddPresentSample(
		SampleWindow& window,
		uint64_t sampleId,
		double presentIntervalMs,
		bool presentSynced);

	AddSampleResult AddWholeFrameSample(
		SampleWindow& window,
		uint64_t wholeFrameSampleId,
		uint64_t associatedPresentSampleId,
		std::optional<double> gpuMs,
		std::optional<double> cpuMs,
		double framePacingEpsilonMs = 1.0);

	AddSampleResult AddOutputPresentSample(
		SampleWindow& window,
		uint64_t sampleId,
		uint64_t discontinuityEpoch,
		double sampledDurationMs,
		uint32_t presentedFrameCount);

	enum class MetricKind
	{
		Present,
		WholeFrameGpu,
		WholeFrameCpu
	};

	struct MetricCoverageDiagnostics
	{
		uint32_t expectedSampleCount = 0;
		uint32_t validSampleCount = 0;
		double expectedSampleWeight = 0.0;
		double validSampleWeight = 0.0;
		std::optional<double> sampleCoverage;
		std::optional<double> weightCoverage;
		bool continuous = false;

		bool Meets(double minimumCoverage) const;
	};

	struct BlockDiagnostics
	{
		MetricCoverageDiagnostics wholeFrameGpu;
		MetricCoverageDiagnostics wholeFrameCpu;
	};

	struct WindowDiagnostics
	{
		double sampledDurationMs = 0.0;
		uint32_t presentSampleCount = 0;
		MetricCoverageDiagnostics wholeFrameGpu;
		MetricCoverageDiagnostics wholeFrameCpu;
		std::array<BlockDiagnostics, kMeasurementBlockCount> blocks{};
	};

	MetricCoverageDiagnostics GetMetricCoverage(
		const SampleWindow& window,
		MetricKind metric);
	MetricCoverageDiagnostics GetMetricCoverage(
		const SampleWindow& window,
		std::size_t blockIndex,
		MetricKind metric);
	WindowDiagnostics BuildWindowDiagnostics(const SampleWindow& window);

	std::optional<double> GetWindowMeanMs(
		const SampleWindow& window,
		MetricKind metric,
		double minimumCoverage = kDefaultMinimumMetricCoverage);
	std::optional<double> GetBlockMeanMs(
		const SampleWindow& window,
		std::size_t blockIndex,
		MetricKind metric,
		double minimumCoverage = kDefaultMinimumMetricCoverage);
	std::optional<double> GetWindowFps(const SampleWindow& window);
	std::optional<double> GetBlockFps(
		const SampleWindow& window,
		std::size_t blockIndex);
	std::optional<double> GetBlockOutputFps(
		const SampleWindow& window,
		std::size_t blockIndex);

	bool IsFramePaced(const SampleWindow& window);

	enum class MetricReliability
	{
		Unavailable,
		InsufficientSampleCoverage,
		BelowPracticalFloor,
		NotStatisticallySignificant,
		Reliable
	};

	struct MetricDelta
	{
		std::optional<double> currentMeanMs;
		std::optional<double> currentStandardErrorMs;
		std::optional<double> comparisonMeanMs;
		std::optional<double> comparisonStandardErrorMs;
		std::optional<double> valueMs;
		std::optional<double> standardErrorMs;
		std::optional<double> pValue;
		std::optional<double> practicalFloorMs;
		MetricReliability reliability = MetricReliability::Unavailable;
		int direction = 0;

		bool IsAvailable() const { return valueMs.has_value(); }
		bool IsReliable() const { return reliability == MetricReliability::Reliable; }
	};

	struct FpsDelta
	{
		std::optional<double> current;
		std::optional<double> currentStandardError;
		std::optional<double> comparison;
		std::optional<double> comparisonStandardError;
		std::optional<double> value;
		std::optional<double> standardError;
		std::optional<double> pValue;
		std::optional<double> practicalFloor;
		MetricReliability reliability = MetricReliability::Unavailable;
		int direction = 0;

		bool IsAvailable() const { return value.has_value(); }
		bool IsReliable() const { return reliability == MetricReliability::Reliable; }
	};

	struct CostResult
	{
		MetricDelta present;
		MetricDelta wholeFrameGpu;
		MetricDelta wholeFrameCpu;
		FpsDelta fps;
		FpsDelta outputFps;
		WindowDiagnostics currentDiagnostics;
		WindowDiagnostics comparisonDiagnostics;
		bool presentSynced = false;
		bool framePaced = false;
	};

	CostResult CalculateCostResult(
		const SampleWindow& current,
		const SampleWindow& comparison,
		double minimumMetricCoverage = kDefaultMinimumMetricCoverage);
}
