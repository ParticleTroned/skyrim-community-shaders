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

		struct MetricWindowMoments
		{
			const Moments& total;
			const Moments& firstHalf;
			const Moments& secondHalf;
		};

		MetricWindowMoments GetPresentWindow(const SampleWindow& window)
		{
			return {
				window.present,
				window.firstHalfPresent,
				window.secondHalfPresent
			};
		}

		MetricWindowMoments GetWholeFrameGpuWindow(const SampleWindow& window)
		{
			return {
				window.wholeFrameGpu,
				window.firstHalfWholeFrameGpu,
				window.secondHalfWholeFrameGpu
			};
		}

		MetricWindowMoments GetWholeFrameCpuWindow(const SampleWindow& window)
		{
			return {
				window.wholeFrameCpu,
				window.firstHalfWholeFrameCpu,
				window.secondHalfWholeFrameCpu
			};
		}

		double CalculateWindowMeanVariance(const MetricWindowMoments& window)
		{
			const double standardError = window.total.StandardError();
			const double standardErrorVariance =
				standardError * standardError;
			if (window.firstHalf.sampleWeight <= 0.0 ||
				window.secondHalf.sampleWeight <= 0.0) {
				return standardErrorVariance;
			}

			const double halfSampleWeight =
				window.firstHalf.sampleWeight + window.secondHalf.sampleWeight;
			const double firstHalfWeight =
				window.firstHalf.sampleWeight / halfSampleWeight;
			const double secondHalfWeight =
				window.secondHalf.sampleWeight / halfSampleWeight;
			const double firstHalfError = window.firstHalf.StandardError();
			const double secondHalfError = window.secondHalf.StandardError();
			const double samplingVariance =
				firstHalfWeight * firstHalfWeight *
					firstHalfError * firstHalfError +
				secondHalfWeight * secondHalfWeight *
					secondHalfError * secondHalfError;
			const double halfMeanDifference =
				window.firstHalf.Mean() - window.secondHalf.Mean();
			// Treat the two half-window means as repeated estimates. Any gap
			// beyond their sampling error contributes temporal variance to the
			// window mean instead of rejecting the captured window.
			const double temporalVariance = std::max(
				0.0,
				(halfMeanDifference * halfMeanDifference -
				 firstHalfError * firstHalfError -
				 secondHalfError * secondHalfError) *
					0.5);
			const double temporalMeanVariance =
				(firstHalfWeight * firstHalfWeight +
				 secondHalfWeight * secondHalfWeight) *
				temporalVariance;
			return std::max(
				standardErrorVariance,
				samplingVariance + temporalMeanVariance);
		}

		double CalculateCurrentMeanVariance(
			const MetricWindowMoments& currentBefore,
			const MetricWindowMoments& currentAfter)
		{
			const double withinCurrentVariance =
				(CalculateWindowMeanVariance(currentBefore) +
				 CalculateWindowMeanVariance(currentAfter)) *
				0.25;
			const double currentMeanDifference =
				currentBefore.total.Mean() -
				currentAfter.total.Mean();
			// The two A windows are the repeated current-state observations in
			// A-B-A. Restored-state drift therefore widens the same interval.
			const double betweenCurrentVariance =
				currentMeanDifference *
				currentMeanDifference *
				0.25;
			return std::max(
				withinCurrentVariance,
				betweenCurrentVariance);
		}

		MetricDelta CalculateMetricDelta(
			const MetricWindowMoments& currentBefore,
			const MetricWindowMoments& comparison,
			const MetricWindowMoments& currentAfter,
			bool available)
		{
			MetricDelta result;
			result.available = available &&
			                   currentBefore.total.sampleWeight > 0.0 &&
			                   comparison.total.sampleWeight > 0.0 &&
			                   currentAfter.total.sampleWeight > 0.0;
			if (!result.available)
				return result;

			result.currentBeforeMeanMs = currentBefore.total.Mean();
			result.comparisonMeanMs = comparison.total.Mean();
			result.currentAfterMeanMs = currentAfter.total.Mean();
			const double currentMean =
				(result.currentBeforeMeanMs + result.currentAfterMeanMs) * 0.5;
			result.valueMs = currentMean - result.comparisonMeanMs;

			const double currentVariance =
				CalculateCurrentMeanVariance(
					currentBefore,
					currentAfter);
			const double comparisonVariance =
				CalculateWindowMeanVariance(comparison);
			result.margin95Ms = k95PercentConfidenceZScore *
			                    std::sqrt(std::max(0.0, currentVariance + comparisonVariance));
			result.statisticallySignificant =
				std::abs(result.valueMs) > result.margin95Ms;
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

	TransitionGateResult UpdateTransitionGate(
		TransitionGateState& state,
		bool featureReady,
		double currentTime,
		uint64_t currentPresentSampleId,
		double minimumReadySeconds,
		uint64_t minimumFreshPresentCount,
		double postFreshSoakSeconds)
	{
		if (!featureReady) {
			state = {};
			return TransitionGateResult::Pending;
		}

		if (!state.continuouslyReady) {
			state.continuouslyReady = true;
			state.readyStartTime = currentTime;
			state.readyStartPresentSampleId = currentPresentSampleId;
			return TransitionGateResult::Pending;
		}

		if (currentPresentSampleId < state.readyStartPresentSampleId) {
			state = {};
			return TransitionGateResult::TimingReset;
		}

		const bool readyLongEnough =
			currentTime - state.readyStartTime >=
			std::max(0.0, minimumReadySeconds);
		const bool hasFreshSamples =
			currentPresentSampleId - state.readyStartPresentSampleId >=
			minimumFreshPresentCount;
		if (!readyLongEnough || !hasFreshSamples)
			return TransitionGateResult::Pending;

		const double soakSeconds = std::max(0.0, postFreshSoakSeconds);
		if (soakSeconds <= 0.0)
			return TransitionGateResult::Ready;

		if (!state.soakStarted) {
			state.soakStarted = true;
			state.soakStartTime = currentTime;
			return TransitionGateResult::Pending;
		}

		return currentTime - state.soakStartTime >= soakSeconds ?
		           TransitionGateResult::Ready :
		           TransitionGateResult::Pending;
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
		const SampleWindow& currentAfter)
	{
		CostResult result;
		const auto currentBeforePresent = GetPresentWindow(currentBefore);
		const auto comparisonPresent = GetPresentWindow(comparison);
		const auto currentAfterPresent = GetPresentWindow(currentAfter);
		result.present = CalculateMetricDelta(
			currentBeforePresent,
			comparisonPresent,
			currentAfterPresent,
			true);

		const bool hasGpu =
			HasCompleteMetricCoverage(currentBefore, currentBefore.wholeFrameGpu) &&
			HasCompleteMetricCoverage(comparison, comparison.wholeFrameGpu) &&
			HasCompleteMetricCoverage(currentAfter, currentAfter.wholeFrameGpu);
		result.wholeFrameGpu = CalculateMetricDelta(
			GetWholeFrameGpuWindow(currentBefore),
			GetWholeFrameGpuWindow(comparison),
			GetWholeFrameGpuWindow(currentAfter),
			hasGpu);

		const bool hasCpu =
			HasCompleteMetricCoverage(currentBefore, currentBefore.wholeFrameCpu) &&
			HasCompleteMetricCoverage(comparison, comparison.wholeFrameCpu) &&
			HasCompleteMetricCoverage(currentAfter, currentAfter.wholeFrameCpu);
		result.wholeFrameCpu = CalculateMetricDelta(
			GetWholeFrameCpuWindow(currentBefore),
			GetWholeFrameCpuWindow(comparison),
			GetWholeFrameCpuWindow(currentAfter),
			hasCpu);

		if (result.present.available) {
			const double currentMean =
				(currentBeforePresent.total.Mean() + currentAfterPresent.total.Mean()) * 0.5;
			const double comparisonMean = comparisonPresent.total.Mean();
			if (IsPositiveFinite(currentMean) && IsPositiveFinite(comparisonMean)) {
				result.hasFps = true;
				result.fpsDelta = 1000.0 / currentMean - 1000.0 / comparisonMean;

				const double currentVariance =
					CalculateCurrentMeanVariance(
						currentBeforePresent,
						currentAfterPresent);
				const double comparisonVariance =
					CalculateWindowMeanVariance(comparisonPresent);
				const double fpsVariance =
					(1000.0 / (currentMean * currentMean)) *
						(1000.0 / (currentMean * currentMean)) * currentVariance +
					(1000.0 / (comparisonMean * comparisonMean)) *
						(1000.0 / (comparisonMean * comparisonMean)) * comparisonVariance;
				result.fpsMargin95 = k95PercentConfidenceZScore *
				                     std::sqrt(std::max(0.0, fpsVariance));
				result.fpsStatisticallySignificant =
					std::abs(result.fpsDelta) > result.fpsMargin95;
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
