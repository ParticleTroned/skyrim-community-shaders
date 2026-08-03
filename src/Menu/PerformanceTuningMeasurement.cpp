#include "Menu/PerformanceTuningMeasurement.h"

#include <algorithm>
#include <cmath>

namespace PerformanceTuning
{
	namespace
	{
		bool IsPositiveFinite(double value)
		{
			return std::isfinite(value) && value > 0.0;
		}

		void AddSplitMoment(
			Moments& total,
			Moments& firstHalf,
			Moments& secondHalf,
			double value,
			const PresentSampleContribution& contribution)
		{
			total.Add(value, contribution.totalWeight);
			firstHalf.Add(value, contribution.firstHalfWeight);
			secondHalf.Add(value, contribution.secondHalfWeight);
		}

		double CalculateCurrentMeanVariance(
			const Moments& currentBefore,
			const Moments& currentAfter)
		{
			const double withinCurrentVariance =
				(currentBefore.StandardError() *
					 currentBefore.StandardError() +
				 currentAfter.StandardError() *
					 currentAfter.StandardError()) *
				0.25;
			const double currentMeanDifference =
				currentBefore.Mean() -
				currentAfter.Mean();
			const double betweenCurrentVariance =
				currentMeanDifference *
				currentMeanDifference *
				0.25;
			return std::max(
				withinCurrentVariance,
				betweenCurrentVariance);
		}

		MetricDelta CalculateMetricDelta(
			const Moments& currentBefore,
			const Moments& comparison,
			const Moments& currentAfter,
			double minimumMeaningfulDeltaMs,
			bool available)
		{
			MetricDelta result;
			result.available = available &&
			                   currentBefore.sampleWeight > 0.0 &&
			                   comparison.sampleWeight > 0.0 &&
			                   currentAfter.sampleWeight > 0.0;
			if (!result.available)
				return result;

			const double currentMean = (currentBefore.Mean() + currentAfter.Mean()) * 0.5;
			result.valueMs = currentMean - comparison.Mean();

			const double currentVariance =
				CalculateCurrentMeanVariance(
					currentBefore,
					currentAfter);
			const double comparisonVariance =
				comparison.StandardError() * comparison.StandardError();
			result.margin95Ms = 1.96 * std::sqrt(std::max(0.0, currentVariance + comparisonVariance));
			result.significanceThresholdMs = std::max(minimumMeaningfulDeltaMs, result.margin95Ms);
			result.significant = std::abs(result.valueMs) > result.significanceThresholdMs;
			return result;
		}
	}

	void Moments::Add(double value, double weight)
	{
		if (!IsPositiveFinite(value) || !std::isfinite(weight) || weight <= 0.0)
			return;

		sum += value * weight;
		squaredSum += value * value * weight;
		sampleWeight += weight;
	}

	double Moments::Mean() const
	{
		return sampleWeight > 0.0 ? sum / sampleWeight : 0.0;
	}

	double Moments::Variance() const
	{
		if (sampleWeight <= 0.0)
			return 0.0;

		const double mean = Mean();
		return std::max(0.0, squaredSum / sampleWeight - mean * mean);
	}

	double Moments::StandardDeviation() const
	{
		return std::sqrt(Variance());
	}

	double Moments::StandardError() const
	{
		return sampleWeight > 0.0 ? StandardDeviation() / std::sqrt(sampleWeight) : 0.0;
	}

	bool AreMomentsEquivalent(
		const Moments& lhs,
		const Moments& rhs,
		const StabilityTolerance& tolerance)
	{
		if (lhs.sampleWeight < tolerance.minimumSampleWeight ||
			rhs.sampleWeight < tolerance.minimumSampleWeight) {
			return false;
		}

		const double lhsMean = lhs.Mean();
		const double rhsMean = rhs.Mean();
		const double meanTolerance = std::max(
			tolerance.meanAbsoluteMs,
			std::max(lhsMean, rhsMean) * tolerance.meanRelative);
		if (std::abs(lhsMean - rhsMean) > meanTolerance)
			return false;

		const double lhsDeviation = lhs.StandardDeviation();
		const double rhsDeviation = rhs.StandardDeviation();
		const double deviationTolerance = std::max(
			tolerance.deviationAbsoluteMs,
			std::max(lhsDeviation, rhsDeviation) * tolerance.deviationRelative);
		return std::abs(lhsDeviation - rhsDeviation) <= deviationTolerance;
	}

	void BeginSampleWindow(
		SampleWindow& window,
		uint64_t currentPresentSampleId,
		uint64_t currentWholeFrameSampleId,
		bool framePacingInferenceValid)
	{
		window = {};
		window.startPresentSampleId = currentPresentSampleId;
		window.lastPresentSampleId = currentPresentSampleId;
		window.lastWholeFrameSampleId = currentWholeFrameSampleId;
		window.framePacingInferenceValid = framePacingInferenceValid;
	}

	AddSampleResult AddPresentSample(
		SampleWindow& window,
		uint64_t sampleId,
		double presentIntervalMs,
		bool presentSynced)
	{
		if (sampleId == 0 || sampleId == window.lastPresentSampleId || !IsPositiveFinite(presentIntervalMs))
			return AddSampleResult::Ignored;
		if (window.lastPresentSampleId != 0 && sampleId < window.lastPresentSampleId)
			return AddSampleResult::SourceReset;
		if (window.lastPresentSampleId != 0 &&
			sampleId > window.lastPresentSampleId &&
			sampleId - window.lastPresentSampleId > 1) {
			return AddSampleResult::SourceGap;
		}

		window.lastPresentSampleId = sampleId;
		if (window.sampledDurationMs >= kMeasurementDurationMs)
			return AddSampleResult::Complete;

		const double remainingDurationMs = kMeasurementDurationMs - window.sampledDurationMs;
		const double totalWeight = std::min(1.0, remainingDurationMs / presentIntervalMs);
		if (totalWeight <= 0.0)
			return AddSampleResult::Ignored;

		const double sampleStartMs = window.sampledDurationMs;
		const double sampleDurationMs = presentIntervalMs * totalWeight;
		const double sampleEndMs = sampleStartMs + sampleDurationMs;
		const double firstHalfDurationMs =
			std::max(0.0, std::min(sampleEndMs, kMeasurementHalfDurationMs) - sampleStartMs);
		const double secondHalfDurationMs = sampleDurationMs - firstHalfDurationMs;

		PresentSampleContribution contribution;
		contribution.timingMs = presentIntervalMs;
		contribution.totalWeight = totalWeight;
		contribution.firstHalfWeight = firstHalfDurationMs / presentIntervalMs;
		contribution.secondHalfWeight = secondHalfDurationMs / presentIntervalMs;
		window.presentSamples[sampleId] = contribution;

		AddSplitMoment(
			window.present,
			window.firstHalfPresent,
			window.secondHalfPresent,
			presentIntervalMs,
			contribution);
		window.sampledDurationMs = std::min(kMeasurementDurationMs, sampleEndMs);
		if (presentSynced)
			window.presentSyncedSamples++;

		if (window.sampledDurationMs >= kMeasurementDurationMs) {
			window.endPresentSampleId = sampleId;
			return AddSampleResult::Complete;
		}
		return AddSampleResult::Added;
	}

	AddSampleResult AddWholeFrameSample(
		SampleWindow& window,
		uint64_t wholeFrameSampleId,
		uint64_t associatedPresentSampleId,
		std::optional<double> gpuMs,
		std::optional<double> cpuMs,
		double framePacingEpsilonMs)
	{
		if (wholeFrameSampleId == 0 || wholeFrameSampleId == window.lastWholeFrameSampleId)
			return AddSampleResult::Ignored;
		if (window.lastWholeFrameSampleId != 0 && wholeFrameSampleId < window.lastWholeFrameSampleId)
			return AddSampleResult::SourceReset;

		const bool sourceGap =
			window.lastWholeFrameSampleId != 0 &&
			wholeFrameSampleId > window.lastWholeFrameSampleId &&
			wholeFrameSampleId - window.lastWholeFrameSampleId > 1;
		window.wholeFrameSourceDiscontinuous |= sourceGap;
		window.wholeFrameGpuCoverageDiscontinuous |= sourceGap;
		window.wholeFrameCpuCoverageDiscontinuous |= sourceGap;
		window.lastWholeFrameSampleId = wholeFrameSampleId;
		window.latestWholeFramePresentSampleId =
			std::max(window.latestWholeFramePresentSampleId, associatedPresentSampleId);

		const auto sampleIt = window.presentSamples.find(associatedPresentSampleId);
		if (sampleIt == window.presentSamples.end())
			return sourceGap ? AddSampleResult::SourceGap : AddSampleResult::Ignored;

		const auto& contribution = sampleIt->second;
		const bool hasGpu = gpuMs && IsPositiveFinite(*gpuMs);
		const bool hasCpu = cpuMs && IsPositiveFinite(*cpuMs);
		const bool addGpu =
			hasGpu &&
			window.wholeFrameGpuPresentSampleIds
				.insert(associatedPresentSampleId)
				.second;
		const bool addCpu =
			hasCpu &&
			window.wholeFrameCpuPresentSampleIds
				.insert(associatedPresentSampleId)
				.second;
		const bool duplicateGpuAssociation = hasGpu && !addGpu;
		const bool duplicateCpuAssociation = hasCpu && !addCpu;
		window.wholeFrameGpuCoverageDiscontinuous |=
			duplicateGpuAssociation;
		window.wholeFrameCpuCoverageDiscontinuous |=
			duplicateCpuAssociation;

		if (addGpu) {
			AddSplitMoment(
				window.wholeFrameGpu,
				window.firstHalfWholeFrameGpu,
				window.secondHalfWholeFrameGpu,
				*gpuMs,
				contribution);
		}
		if (addCpu) {
			AddSplitMoment(
				window.wholeFrameCpu,
				window.firstHalfWholeFrameCpu,
				window.secondHalfWholeFrameCpu,
				*cpuMs,
				contribution);
		}

		if (addGpu && addCpu) {
			window.framePacingEligibleSampleWeight += contribution.totalWeight;
			if (contribution.timingMs > std::max(*gpuMs, *cpuMs) + framePacingEpsilonMs)
				window.framePacedSampleWeight += contribution.totalWeight;
		}
		if (sourceGap)
			return AddSampleResult::SourceGap;
		if (duplicateGpuAssociation || duplicateCpuAssociation)
			return AddSampleResult::DuplicateAssociation;
		return AddSampleResult::Added;
	}

	bool HasCompleteMetricCoverage(const SampleWindow& window, const Moments& metric)
	{
		const bool metricDiscontinuous =
			(&metric == &window.wholeFrameGpu &&
			 window.wholeFrameGpuCoverageDiscontinuous) ||
			(&metric == &window.wholeFrameCpu &&
			 window.wholeFrameCpuCoverageDiscontinuous);
		return !window.wholeFrameSourceDiscontinuous &&
		       !metricDiscontinuous &&
		       window.present.sampleWeight > 0.0 &&
		       std::abs(
				   metric.sampleWeight -
				   window.present.sampleWeight) <=
			       kSampleWeightEpsilon;
	}

	bool IsInternallyStable(const SampleWindow& window, const StabilityTolerance& tolerance)
	{
		if (!AreMomentsEquivalent(window.firstHalfPresent, window.secondHalfPresent, tolerance))
			return false;

		auto optionalMetricStable = [&](const Moments& total, const Moments& firstHalf, const Moments& secondHalf) {
			if (!HasCompleteMetricCoverage(window, total))
				return true;
			return AreMomentsEquivalent(firstHalf, secondHalf, tolerance);
		};

		return optionalMetricStable(
				   window.wholeFrameGpu,
				   window.firstHalfWholeFrameGpu,
				   window.secondHalfWholeFrameGpu) &&
		       optionalMetricStable(
				   window.wholeFrameCpu,
				   window.firstHalfWholeFrameCpu,
				   window.secondHalfWholeFrameCpu);
	}

	bool AreCurrentWindowsEquivalent(
		const SampleWindow& before,
		const SampleWindow& after,
		const StabilityTolerance& tolerance)
	{
		if (!AreMomentsEquivalent(before.present, after.present, tolerance))
			return false;

		auto optionalMetricEquivalent = [&](const Moments& lhs, const Moments& rhs, bool lhsComplete, bool rhsComplete) {
			// Query availability is not itself a scene-stability signal. If either
			// current window lacks full coverage, the metric is omitted from the
			// final A-B-A result and Present timing remains authoritative.
			return !lhsComplete || !rhsComplete || AreMomentsEquivalent(lhs, rhs, tolerance);
		};

		return optionalMetricEquivalent(
				   before.wholeFrameGpu,
				   after.wholeFrameGpu,
				   HasCompleteMetricCoverage(before, before.wholeFrameGpu),
				   HasCompleteMetricCoverage(after, after.wholeFrameGpu)) &&
		       optionalMetricEquivalent(
				   before.wholeFrameCpu,
				   after.wholeFrameCpu,
				   HasCompleteMetricCoverage(before, before.wholeFrameCpu),
				   HasCompleteMetricCoverage(after, after.wholeFrameCpu));
	}

	bool IsFramePaced(const SampleWindow& window)
	{
		return window.framePacingInferenceValid &&
		       HasCompleteMetricCoverage(
				   window,
				   window.wholeFrameGpu) &&
		       HasCompleteMetricCoverage(
				   window,
				   window.wholeFrameCpu) &&
		       window.framePacingEligibleSampleWeight > 0.0 &&
		       window.framePacedSampleWeight * 2.0 >= window.framePacingEligibleSampleWeight;
	}

	CostResult CalculateCostResult(
		const SampleWindow& currentBefore,
		const SampleWindow& comparison,
		const SampleWindow& currentAfter,
		double minimumMeaningfulDeltaMs)
	{
		CostResult result;
		result.present = CalculateMetricDelta(
			currentBefore.present,
			comparison.present,
			currentAfter.present,
			minimumMeaningfulDeltaMs,
			true);

		const bool hasGpu =
			HasCompleteMetricCoverage(currentBefore, currentBefore.wholeFrameGpu) &&
			HasCompleteMetricCoverage(comparison, comparison.wholeFrameGpu) &&
			HasCompleteMetricCoverage(currentAfter, currentAfter.wholeFrameGpu);
		result.wholeFrameGpu = CalculateMetricDelta(
			currentBefore.wholeFrameGpu,
			comparison.wholeFrameGpu,
			currentAfter.wholeFrameGpu,
			minimumMeaningfulDeltaMs,
			hasGpu);

		const bool hasCpu =
			HasCompleteMetricCoverage(currentBefore, currentBefore.wholeFrameCpu) &&
			HasCompleteMetricCoverage(comparison, comparison.wholeFrameCpu) &&
			HasCompleteMetricCoverage(currentAfter, currentAfter.wholeFrameCpu);
		result.wholeFrameCpu = CalculateMetricDelta(
			currentBefore.wholeFrameCpu,
			comparison.wholeFrameCpu,
			currentAfter.wholeFrameCpu,
			minimumMeaningfulDeltaMs,
			hasCpu);

		if (result.present.available) {
			const double currentMean =
				(currentBefore.present.Mean() + currentAfter.present.Mean()) * 0.5;
			const double comparisonMean = comparison.present.Mean();
			if (IsPositiveFinite(currentMean) && IsPositiveFinite(comparisonMean)) {
				result.hasFps = true;
				result.fpsDelta = 1000.0 / currentMean - 1000.0 / comparisonMean;

				const double currentVariance =
					CalculateCurrentMeanVariance(
						currentBefore.present,
						currentAfter.present);
				const double comparisonVariance =
					comparison.present.StandardError() * comparison.present.StandardError();
				const double fpsVariance =
					(1000.0 / (currentMean * currentMean)) *
						(1000.0 / (currentMean * currentMean)) * currentVariance +
					(1000.0 / (comparisonMean * comparisonMean)) *
						(1000.0 / (comparisonMean * comparisonMean)) * comparisonVariance;
				result.fpsMargin95 = 1.96 * std::sqrt(std::max(0.0, fpsVariance));
				result.fpsSignificant = result.present.significant;
			}
		}

		result.presentSynced =
			currentBefore.presentSyncedSamples > 0 ||
			comparison.presentSyncedSamples > 0 ||
			currentAfter.presentSyncedSamples > 0;
		result.framePaced =
			currentBefore.framePacingInferenceValid &&
			comparison.framePacingInferenceValid &&
			currentAfter.framePacingInferenceValid &&
			(IsFramePaced(currentBefore) ||
				IsFramePaced(comparison) ||
				IsFramePaced(currentAfter));
		return result;
	}
}
