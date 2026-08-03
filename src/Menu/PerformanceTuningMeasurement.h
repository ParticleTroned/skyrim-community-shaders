#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace PerformanceTuning
{
	inline constexpr double kMeasurementDurationMs = 3000.0;
	inline constexpr double kMeasurementHalfDurationMs = kMeasurementDurationMs * 0.5;
	inline constexpr double kSampleWeightEpsilon = 1.0e-6;
	inline constexpr double k95PercentConfidenceZScore = 1.96;

	struct Moments
	{
		double sum = 0.0;
		double squaredSum = 0.0;
		double sampleWeight = 0.0;

		void Add(double value, double weight);
		double Mean() const;
		double Variance() const;
		double StandardDeviation() const;
		double StandardError() const;
	};

	struct PresentSampleContribution
	{
		double timingMs = 0.0;
		double totalWeight = 0.0;
		double firstHalfWeight = 0.0;
		double secondHalfWeight = 0.0;
	};

	struct SampleWindow
	{
		double sampledDurationMs = 0.0;
		Moments present;
		Moments wholeFrameGpu;
		Moments wholeFrameCpu;
		Moments firstHalfPresent;
		Moments secondHalfPresent;
		Moments firstHalfWholeFrameGpu;
		Moments secondHalfWholeFrameGpu;
		Moments firstHalfWholeFrameCpu;
		Moments secondHalfWholeFrameCpu;
		double framePacedSampleWeight = 0.0;
		double framePacingEligibleSampleWeight = 0.0;
		bool framePacingInferenceValid = true;
		int presentSyncedSamples = 0;
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
		DuplicateAssociation
	};

	void BeginSampleWindow(
		SampleWindow& window,
		uint64_t currentPresentSampleId,
		uint64_t currentWholeFrameSampleId,
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

	bool HasCompleteMetricCoverage(const SampleWindow& window, const Moments& metric);
	bool IsFramePaced(const SampleWindow& window);

	struct MetricDelta
	{
		double valueMs = 0.0;
		double margin95Ms = 0.0;
		double significanceThresholdMs = 0.0;
		bool available = false;
		bool statisticallySignificant = false;
		bool significant = false;
	};

	struct CostResult
	{
		MetricDelta present;
		MetricDelta wholeFrameGpu;
		MetricDelta wholeFrameCpu;
		double fpsDelta = 0.0;
		double fpsMargin95 = 0.0;
		bool hasFps = false;
		bool fpsStatisticallySignificant = false;
		bool fpsSignificant = false;
		bool presentSynced = false;
		bool framePaced = false;
	};

	CostResult CalculateCostResult(
		const SampleWindow& currentBefore,
		const SampleWindow& comparison,
		const SampleWindow& currentAfter,
		double minimumMeaningfulDeltaMs = 0.099);
}
