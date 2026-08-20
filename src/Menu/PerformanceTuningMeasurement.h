#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace PerformanceTuning
{
	inline constexpr std::size_t kMeasurementBlockCount = 4;
	inline constexpr double kMeasurementBlockDurationMs = 500.0;
	inline constexpr double kMeasurementDurationMs =
		kMeasurementBlockCount * kMeasurementBlockDurationMs;
	inline constexpr uint32_t kMinimumPresentSampleCount = 24;
	inline constexpr double kMaximumPresentIntervalMs = 1000.0;
	inline constexpr double kSampleWeightEpsilon = 1.0e-6;
	inline constexpr double kDefaultMinimumMetricCoverage = 0.90;
	inline constexpr double kPracticalFloorAbsoluteMs = 0.10;
	inline constexpr double kPracticalFloorRelative = 0.01;
	inline constexpr uint32_t kRequiredAgreeingBlockCount = 3;
	inline constexpr double kDriftDominanceRatio = 2.0;

	struct Moments
	{
		double mean = 0.0;
		double sampleWeight = 0.0;

		bool Add(double value, double weight);
		std::optional<double> Mean() const;
	};

	struct TransitionGateState
	{
		bool continuouslyReady = false;
		double readyStartTime = 0.0;
		uint64_t readyStartPresentSampleId = 0;
		bool soakStarted = false;
		double soakStartTime = 0.0;
	};

	enum class TransitionGateResult
	{
		Pending,
		Ready,
		TimingReset
	};

	TransitionGateResult UpdateTransitionGate(
		TransitionGateState& state,
		bool featureReady,
		double currentTime,
		uint64_t currentPresentSampleId,
		double minimumReadySeconds,
		uint64_t minimumFreshPresentCount,
		double postFreshSoakSeconds);

	struct PresentSampleContribution
	{
		double timingMs = 0.0;
		double totalWeight = 0.0;
		std::array<double, kMeasurementBlockCount> blockWeights{};
	};

	struct SampleBlock
	{
		double sampledDurationMs = 0.0;
		Moments present;
		Moments wholeFrameGpu;
		Moments wholeFrameCpu;
		uint32_t presentSampleCount = 0;
		uint32_t wholeFrameGpuSampleCount = 0;
		uint32_t wholeFrameCpuSampleCount = 0;
	};

	struct SampleWindow
	{
		double captureStartTimeSeconds = 0.0;
		double sampledDurationMs = 0.0;
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
		uint64_t latestWholeFramePresentSampleId = 0;
		bool wholeFrameSourceDiscontinuous = false;
		bool wholeFrameGpuCoverageDiscontinuous = false;
		bool wholeFrameCpuCoverageDiscontinuous = false;
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
		double captureStartTimeSeconds,
		bool framePacingInferenceValid = true);

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
	std::optional<double> GetWindowMidpointTimeSeconds(
		const SampleWindow& window);
	std::optional<double> GetBlockMidpointTimeSeconds(
		const SampleWindow& window,
		std::size_t blockIndex);

	bool IsFramePaced(const SampleWindow& window);

	struct RepeatabilityBand
	{
		std::array<std::optional<double>, kMeasurementBlockCount> blockDeltas{};
		std::optional<double> median;
		std::optional<double> minimum;
		std::optional<double> maximum;
		uint32_t availableBlockCount = 0;
		uint32_t agreeingBlockCount = 0;
	};

	enum class MetricReliability
	{
		Unavailable,
		InsufficientBlockCoverage,
		BelowPracticalFloor,
		DriftDominated,
		MixedBlockDirections,
		Reliable
	};

	struct MetricDelta
	{
		std::optional<double> currentBeforeMeanMs;
		std::optional<double> comparisonMeanMs;
		std::optional<double> currentAfterMeanMs;
		std::optional<double> interpolatedCurrentMeanMs;
		std::optional<double> valueMs;
		std::optional<double> currentDriftMs;
		std::optional<double> practicalFloorMs;
		RepeatabilityBand repeatability;
		MetricReliability reliability = MetricReliability::Unavailable;
		int direction = 0;

		bool IsAvailable() const { return valueMs.has_value(); }
		bool IsReliable() const { return reliability == MetricReliability::Reliable; }
	};

	struct FpsDelta
	{
		std::optional<double> currentBefore;
		std::optional<double> comparison;
		std::optional<double> currentAfter;
		std::optional<double> interpolatedCurrent;
		std::optional<double> value;
		std::optional<double> currentDrift;
		RepeatabilityBand repeatability;

		bool IsAvailable() const { return value.has_value(); }
	};

	struct CostResult
	{
		MetricDelta present;
		MetricDelta wholeFrameGpu;
		MetricDelta wholeFrameCpu;
		FpsDelta fps;
		WindowDiagnostics currentBeforeDiagnostics;
		WindowDiagnostics comparisonDiagnostics;
		WindowDiagnostics currentAfterDiagnostics;
		bool presentSynced = false;
		bool framePaced = false;
	};

	CostResult CalculateCostResult(
		const SampleWindow& currentBefore,
		const SampleWindow& comparison,
		const SampleWindow& currentAfter,
		double minimumMetricCoverage = kDefaultMinimumMetricCoverage);
}
