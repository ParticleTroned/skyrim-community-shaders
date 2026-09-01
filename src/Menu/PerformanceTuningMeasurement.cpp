#include "Menu/PerformanceTuningMeasurement.h"

#include <algorithm>
#include <cmath>

namespace PerformanceTuning
{
	namespace
	{
		constexpr double kDurationEpsilonMs = 1.0e-6;

		bool IsPositiveFinite(double value)
		{
			return std::isfinite(value) && value > 0.0;
		}

		const Moments& GetWindowMoments(
			const SampleWindow& window,
			MetricKind metric)
		{
			switch (metric) {
			case MetricKind::Present:
				return window.present;
			case MetricKind::WholeFrameGpu:
				return window.wholeFrameGpu;
			case MetricKind::WholeFrameCpu:
			default:
				return window.wholeFrameCpu;
			}
		}

		const Moments& GetBlockMoments(
			const SampleBlock& block,
			MetricKind metric)
		{
			switch (metric) {
			case MetricKind::Present:
				return block.present;
			case MetricKind::WholeFrameGpu:
				return block.wholeFrameGpu;
			case MetricKind::WholeFrameCpu:
			default:
				return block.wholeFrameCpu;
			}
		}

		MetricCoverageDiagnostics MakeCoverageDiagnostics(
			uint32_t expectedSampleCount,
			uint32_t validSampleCount,
			double expectedSampleWeight,
			double validSampleWeight,
			bool continuous)
		{
			MetricCoverageDiagnostics result;
			result.expectedSampleCount = expectedSampleCount;
			result.validSampleCount = validSampleCount;
			result.expectedSampleWeight = expectedSampleWeight;
			result.validSampleWeight = validSampleWeight;
			result.continuous = continuous;
			if (expectedSampleCount > 0) {
				result.sampleCoverage = std::clamp(
					static_cast<double>(validSampleCount) /
						static_cast<double>(expectedSampleCount),
					0.0,
					1.0);
			}
			if (expectedSampleWeight > 0.0) {
				result.weightCoverage = std::clamp(
					validSampleWeight / expectedSampleWeight,
					0.0,
					1.0);
			}
			return result;
		}

		bool IsMetricContinuous(
			const SampleWindow& window,
			MetricKind metric)
		{
			if (window.presentSourceDiscontinuous)
				return false;

			switch (metric) {
			case MetricKind::Present:
				return true;
			case MetricKind::WholeFrameGpu:
				return !window.wholeFrameSourceDiscontinuous &&
				       !window.wholeFrameGpuCoverageDiscontinuous;
			case MetricKind::WholeFrameCpu:
			default:
				return !window.wholeFrameSourceDiscontinuous &&
				       !window.wholeFrameCpuCoverageDiscontinuous;
			}
		}

		double BetaContinuedFraction(double a, double b, double x)
		{
			constexpr int kMaximumIterations = 200;
			constexpr double kConvergenceEpsilon = 3.0e-14;
			constexpr double kMinimumMagnitude = 1.0e-300;
			const double qab = a + b;
			const double qap = a + 1.0;
			const double qam = a - 1.0;
			double c = 1.0;
			double d = 1.0 - qab * x / qap;
			if (std::abs(d) < kMinimumMagnitude)
				d = kMinimumMagnitude;
			d = 1.0 / d;
			double result = d;

			for (int iteration = 1;
				iteration <= kMaximumIterations;
				++iteration) {
				const int doubledIteration = 2 * iteration;
				double coefficient =
					static_cast<double>(iteration) *
					(b - static_cast<double>(iteration)) * x /
					((qam + doubledIteration) *
						(a + doubledIteration));
				d = 1.0 + coefficient * d;
				if (std::abs(d) < kMinimumMagnitude)
					d = kMinimumMagnitude;
				c = 1.0 + coefficient / c;
				if (std::abs(c) < kMinimumMagnitude)
					c = kMinimumMagnitude;
				d = 1.0 / d;
				result *= d * c;

				coefficient =
					-(a + static_cast<double>(iteration)) *
					(qab + static_cast<double>(iteration)) * x /
					((a + doubledIteration) *
						(qap + doubledIteration));
				d = 1.0 + coefficient * d;
				if (std::abs(d) < kMinimumMagnitude)
					d = kMinimumMagnitude;
				c = 1.0 + coefficient / c;
				if (std::abs(c) < kMinimumMagnitude)
					c = kMinimumMagnitude;
				d = 1.0 / d;
				const double delta = d * c;
				result *= delta;
				if (std::abs(delta - 1.0) <= kConvergenceEpsilon)
					break;
			}
			return result;
		}

		double RegularizedIncompleteBeta(double x, double a, double b)
		{
			if (x <= 0.0)
				return 0.0;
			if (x >= 1.0)
				return 1.0;

			const double scale = std::exp(
				std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
				a * std::log(x) + b * std::log1p(-x));
			if (x < (a + 1.0) / (a + b + 2.0)) {
				return scale * BetaContinuedFraction(a, b, x) / a;
			}
			return 1.0 -
			       scale * BetaContinuedFraction(b, a, 1.0 - x) / b;
		}

		struct SampleStatistics
		{
			std::optional<double> mean;
			std::optional<double> standardError;
			double variance = 0.0;
			uint32_t sampleCount = 0;
		};

		template <class ValueGetter>
		SampleStatistics CalculateSampleStatistics(ValueGetter&& getValue)
		{
			std::array<double, kMeasurementBlockCount> values{};
			SampleStatistics result;
			double sum = 0.0;
			for (std::size_t index = 0;
				index < kMeasurementBlockCount;
				++index) {
				const auto value = getValue(index);
				if (!value || !std::isfinite(*value))
					continue;
				values[result.sampleCount++] = *value;
				sum += *value;
			}
			if (result.sampleCount == 0)
				return result;

			result.mean = sum / result.sampleCount;
			if (result.sampleCount < 2)
				return result;

			double squaredDeviationSum = 0.0;
			for (uint32_t index = 0; index < result.sampleCount; ++index) {
				const double deviation = values[index] - *result.mean;
				squaredDeviationSum += deviation * deviation;
			}
			result.variance = squaredDeviationSum /
			                  (static_cast<double>(result.sampleCount) - 1.0);
			result.standardError = std::sqrt(
				result.variance /
				static_cast<double>(result.sampleCount));
			return result;
		}

		std::optional<double> CalculateWelchPValue(
			const SampleStatistics& current,
			const SampleStatistics& comparison)
		{
			if (!current.mean || !comparison.mean ||
				current.sampleCount < 2 || comparison.sampleCount < 2) {
				return std::nullopt;
			}

			const double currentVarianceOfMean =
				current.variance / current.sampleCount;
			const double comparisonVarianceOfMean =
				comparison.variance / comparison.sampleCount;
			const double varianceOfDifference =
				currentVarianceOfMean + comparisonVarianceOfMean;
			const double meanDifference =
				*current.mean - *comparison.mean;
			if (varianceOfDifference <= 0.0)
				return meanDifference == 0.0 ? 1.0 : 0.0;

			const double degreesOfFreedomNumerator =
				varianceOfDifference * varianceOfDifference;
			const double degreesOfFreedomDenominator =
				currentVarianceOfMean * currentVarianceOfMean /
					(current.sampleCount - 1.0) +
				comparisonVarianceOfMean * comparisonVarianceOfMean /
					(comparison.sampleCount - 1.0);
			if (degreesOfFreedomDenominator <= 0.0)
				return std::nullopt;

			const double degreesOfFreedom =
				degreesOfFreedomNumerator /
				degreesOfFreedomDenominator;
			const double t = std::abs(meanDifference) /
			                 std::sqrt(varianceOfDifference);
			const double x = degreesOfFreedom /
			                 (degreesOfFreedom + t * t);
			return std::clamp(
				RegularizedIncompleteBeta(
					x,
					degreesOfFreedom * 0.5,
					0.5),
				0.0,
				1.0);
		}

		MetricReliability ClassifyDelta(
			double value,
			double practicalFloor,
			const std::optional<double>& pValue)
		{
			if (std::abs(value) <= practicalFloor)
				return MetricReliability::BelowPracticalFloor;
			if (!pValue || *pValue > kStatisticalSignificanceLevel)
				return MetricReliability::NotStatisticallySignificant;
			return MetricReliability::Reliable;
		}

		MetricDelta CalculateMetricDelta(
			const SampleWindow& current,
			const SampleWindow& comparison,
			MetricKind metric,
			double minimumMetricCoverage)
		{
			MetricDelta result;
			const auto currentStatistics = CalculateSampleStatistics(
				[&](std::size_t blockIndex) {
					return GetBlockMeanMs(
						current,
						blockIndex,
						metric,
						minimumMetricCoverage);
				});
			const auto comparisonStatistics = CalculateSampleStatistics(
				[&](std::size_t blockIndex) {
					return GetBlockMeanMs(
						comparison,
						blockIndex,
						metric,
						minimumMetricCoverage);
				});

			result.currentMeanMs = currentStatistics.mean;
			result.currentStandardErrorMs =
				currentStatistics.standardError;
			result.comparisonMeanMs = comparisonStatistics.mean;
			result.comparisonStandardErrorMs =
				comparisonStatistics.standardError;
			if (currentStatistics.sampleCount < kMeasurementBlockCount ||
				comparisonStatistics.sampleCount < kMeasurementBlockCount ||
				!result.currentMeanMs || !result.comparisonMeanMs) {
				result.reliability =
					MetricReliability::InsufficientSampleCoverage;
				return result;
			}

			result.valueMs =
				*result.currentMeanMs - *result.comparisonMeanMs;
			result.standardErrorMs = std::sqrt(
				currentStatistics.variance / currentStatistics.sampleCount +
				comparisonStatistics.variance /
					comparisonStatistics.sampleCount);
			result.pValue = CalculateWelchPValue(
				currentStatistics,
				comparisonStatistics);
			result.practicalFloorMs = std::max(
				kPracticalFloorAbsoluteMs,
				std::abs(*result.comparisonMeanMs) *
					kPracticalFloorRelative);
			result.direction = *result.valueMs > 0.0 ?
			                       1 :
			                       (*result.valueMs < 0.0 ? -1 : 0);
			result.reliability = ClassifyDelta(
				*result.valueMs,
				*result.practicalFloorMs,
				result.pValue);
			return result;
		}

		using BlockFpsGetter = std::optional<double> (*)(
			const SampleWindow&,
			std::size_t);

		FpsDelta CalculateFpsDelta(
			const SampleWindow& current,
			const SampleWindow& comparison,
			BlockFpsGetter getBlockFps)
		{
			FpsDelta result;
			const auto currentStatistics = CalculateSampleStatistics(
				[&](std::size_t blockIndex) {
					return getBlockFps(current, blockIndex);
				});
			const auto comparisonStatistics = CalculateSampleStatistics(
				[&](std::size_t blockIndex) {
					return getBlockFps(comparison, blockIndex);
				});

			result.current = currentStatistics.mean;
			result.currentStandardError =
				currentStatistics.standardError;
			result.comparison = comparisonStatistics.mean;
			result.comparisonStandardError =
				comparisonStatistics.standardError;
			if (currentStatistics.sampleCount < kMeasurementBlockCount ||
				comparisonStatistics.sampleCount < kMeasurementBlockCount ||
				!result.current || !result.comparison) {
				result.reliability =
					MetricReliability::InsufficientSampleCoverage;
				return result;
			}

			result.value = *result.current - *result.comparison;
			result.standardError = std::sqrt(
				currentStatistics.variance / currentStatistics.sampleCount +
				comparisonStatistics.variance /
					comparisonStatistics.sampleCount);
			result.pValue = CalculateWelchPValue(
				currentStatistics,
				comparisonStatistics);
			result.practicalFloor = std::max(
				kPracticalFloorAbsoluteFps,
				std::abs(*result.comparison) * kPracticalFloorRelative);
			result.direction = *result.value > 0.0 ?
			                       1 :
			                       (*result.value < 0.0 ? -1 : 0);
			result.reliability = ClassifyDelta(
				*result.value,
				*result.practicalFloor,
				result.pValue);
			return result;
		}
	}

	bool Moments::Add(double value, double weight)
	{
		if (!IsPositiveFinite(value) ||
			!std::isfinite(weight) ||
			weight <= 0.0) {
			return false;
		}

		const double updatedSampleWeight = sampleWeight + weight;
		const double delta = value - mean;
		mean += delta * (weight / updatedSampleWeight);
		sampleWeight = updatedSampleWeight;
		return true;
	}

	std::optional<double> Moments::Mean() const
	{
		return sampleWeight > 0.0 ?
		           std::optional<double>(mean) :
		           std::nullopt;
	}

	void BeginSampleWindow(
		SampleWindow& window,
		uint64_t currentPresentSampleId,
		uint64_t currentWholeFrameSampleId,
		bool framePacingInferenceValid,
		uint64_t currentOutputPresentSampleId,
		uint64_t outputPresentDiscontinuityEpoch)
	{
		window = {};
		window.startPresentSampleId = currentPresentSampleId;
		window.lastPresentSampleId = currentPresentSampleId;
		window.lastWholeFrameSampleId = currentWholeFrameSampleId;
		window.lastOutputPresentSampleId =
			currentOutputPresentSampleId;
		window.outputPresentDiscontinuityEpoch =
			outputPresentDiscontinuityEpoch;
		window.framePacingInferenceValid =
			framePacingInferenceValid;
	}

	AddSampleResult AddPresentSample(
		SampleWindow& window,
		uint64_t sampleId,
		double presentIntervalMs,
		bool presentSynced)
	{
		if (sampleId == 0 ||
			sampleId == window.lastPresentSampleId) {
			return AddSampleResult::Ignored;
		}
		if (window.lastPresentSampleId != 0 &&
			sampleId < window.lastPresentSampleId) {
			return AddSampleResult::SourceReset;
		}
		if (window.lastPresentSampleId != 0 &&
			sampleId - window.lastPresentSampleId > 1) {
			window.presentSourceDiscontinuous = true;
			window.lastPresentSampleId = sampleId;
			return AddSampleResult::SourceGap;
		}

		window.lastPresentSampleId = sampleId;
		if (window.complete)
			return AddSampleResult::Complete;
		if (!IsPositiveFinite(presentIntervalMs)) {
			window.presentSourceDiscontinuous = true;
			return AddSampleResult::InvalidValue;
		}
		if (presentIntervalMs > kMaximumPresentIntervalMs) {
			window.presentSourceDiscontinuous = true;
			return AddSampleResult::IntervalTooLarge;
		}

		const uint32_t updatedPresentSampleCount =
			window.presentSampleCount + 1;
		// Keep the completing frame in full. Trimming it to the nominal window
		// boundary would almost erase a real end-of-window hitch, bias the
		// arithmetic mean, and make FPS describe time that was not actually
		// captured. The final block may therefore extend past one second,
		// bounded by kMaximumPresentIntervalMs and the renderer's capture deadline.
		const double contributedDurationMs = presentIntervalMs;

		const double totalWeight =
			contributedDurationMs / presentIntervalMs;
		PresentSampleContribution contribution;
		contribution.timingMs = presentIntervalMs;
		contribution.totalWeight = totalWeight;

		double offsetMs = window.sampledDurationMs;
		double remainingDurationMs = contributedDurationMs;
		while (remainingDurationMs > kDurationEpsilonMs) {
			const auto blockIndex = std::min(
				static_cast<std::size_t>(
					offsetMs / kMeasurementBlockDurationMs),
				kMeasurementBlockCount - 1);
			double segmentDurationMs = remainingDurationMs;
			if (blockIndex + 1 < kMeasurementBlockCount) {
				const double blockEndMs =
					static_cast<double>(blockIndex + 1) *
					kMeasurementBlockDurationMs;
				segmentDurationMs = std::min(
					segmentDurationMs,
					std::max(0.0, blockEndMs - offsetMs));
			}
			if (segmentDurationMs <= kDurationEpsilonMs) {
				offsetMs =
					static_cast<double>(blockIndex + 1) *
					kMeasurementBlockDurationMs;
				continue;
			}

			const double blockWeight =
				segmentDurationMs / presentIntervalMs;
			contribution.blockWeights[blockIndex] +=
				blockWeight;
			auto& block = window.blocks[blockIndex];
			block.sampledDurationMs += segmentDurationMs;
			block.present.Add(presentIntervalMs, blockWeight);
			block.presentSampleCount++;
			offsetMs += segmentDurationMs;
			remainingDurationMs -= segmentDurationMs;
		}

		window.present.Add(presentIntervalMs, totalWeight);
		window.presentSamples[sampleId] = contribution;
		window.presentSampleCount = updatedPresentSampleCount;
		window.sampledDurationMs += contributedDurationMs;
		if (presentSynced)
			window.presentSyncedSampleCount++;

		if (window.sampledDurationMs + kDurationEpsilonMs >=
				kMeasurementDurationMs &&
			window.presentSampleCount >=
				kMinimumPresentSampleCount) {
			window.complete = true;
			window.endPresentSampleId = sampleId;
			return AddSampleResult::Complete;
		}
		return AddSampleResult::Added;
	}

	AddSampleResult AddOutputPresentSample(
		SampleWindow& window,
		uint64_t sampleId,
		uint64_t discontinuityEpoch,
		double sampledDurationMs,
		uint32_t presentedFrameCount)
	{
		if (window.outputPresentDurationMs + kDurationEpsilonMs >=
			kMeasurementDurationMs) {
			return AddSampleResult::Complete;
		}
		if (sampleId == 0 ||
			sampleId == window.lastOutputPresentSampleId) {
			return AddSampleResult::Ignored;
		}
		if (discontinuityEpoch !=
			window.outputPresentDiscontinuityEpoch) {
			window.outputPresentSourceDiscontinuous = true;
			window.outputPresentDiscontinuityEpoch =
				discontinuityEpoch;
			window.lastOutputPresentSampleId = sampleId;
			return AddSampleResult::SourceReset;
		}
		if (window.lastOutputPresentSampleId != 0 &&
			sampleId < window.lastOutputPresentSampleId) {
			window.outputPresentSourceDiscontinuous = true;
			return AddSampleResult::SourceReset;
		}
		if (window.lastOutputPresentSampleId != 0 &&
			sampleId - window.lastOutputPresentSampleId > 1) {
			window.outputPresentSourceDiscontinuous = true;
			window.lastOutputPresentSampleId = sampleId;
			return AddSampleResult::SourceGap;
		}

		window.lastOutputPresentSampleId = sampleId;
		if (!IsPositiveFinite(sampledDurationMs) ||
			presentedFrameCount == 0) {
			window.outputPresentSourceDiscontinuous = true;
			return AddSampleResult::InvalidValue;
		}
		if (sampledDurationMs > kMaximumPresentIntervalMs) {
			window.outputPresentSourceDiscontinuous = true;
			return AddSampleResult::IntervalTooLarge;
		}
		const double contributedDurationMs = std::min(
			sampledDurationMs,
			kMeasurementDurationMs -
				window.outputPresentDurationMs);
		const double contributedFrameCount =
			static_cast<double>(presentedFrameCount) *
			(contributedDurationMs / sampledDurationMs);
		double offsetMs = window.outputPresentDurationMs;
		double remainingDurationMs = contributedDurationMs;
		while (remainingDurationMs > kDurationEpsilonMs) {
			const auto blockIndex = std::min(
				static_cast<std::size_t>(
					offsetMs / kMeasurementBlockDurationMs),
				kMeasurementBlockCount - 1);
			const double blockEndMs =
				static_cast<double>(blockIndex + 1) *
				kMeasurementBlockDurationMs;
			const double segmentDurationMs = std::min(
				remainingDurationMs,
				std::max(0.0, blockEndMs - offsetMs));
			if (segmentDurationMs <= kDurationEpsilonMs) {
				offsetMs = blockEndMs;
				continue;
			}

			auto& block = window.blocks[blockIndex];
			block.outputPresentDurationMs += segmentDurationMs;
			block.outputPresentedFrameCount +=
				contributedFrameCount *
				(segmentDurationMs / contributedDurationMs);
			offsetMs += segmentDurationMs;
			remainingDurationMs -= segmentDurationMs;
		}

		window.outputPresentDurationMs += contributedDurationMs;
		return window.outputPresentDurationMs + kDurationEpsilonMs >=
		               kMeasurementDurationMs ?
		           AddSampleResult::Complete :
		           AddSampleResult::Added;
	}

	AddSampleResult AddWholeFrameSample(
		SampleWindow& window,
		uint64_t wholeFrameSampleId,
		uint64_t associatedPresentSampleId,
		std::optional<double> gpuMs,
		std::optional<double> cpuMs,
		double framePacingEpsilonMs)
	{
		if (wholeFrameSampleId == 0 ||
			wholeFrameSampleId == window.lastWholeFrameSampleId) {
			return AddSampleResult::Ignored;
		}
		if (window.lastWholeFrameSampleId != 0 &&
			wholeFrameSampleId < window.lastWholeFrameSampleId) {
			return AddSampleResult::SourceReset;
		}

		const bool sourceGap =
			window.lastWholeFrameSampleId != 0 &&
			wholeFrameSampleId - window.lastWholeFrameSampleId > 1;
		window.wholeFrameSourceDiscontinuous |= sourceGap;
		window.wholeFrameGpuCoverageDiscontinuous |= sourceGap;
		window.wholeFrameCpuCoverageDiscontinuous |= sourceGap;
		window.lastWholeFrameSampleId = wholeFrameSampleId;
		window.latestWholeFramePresentSampleId = std::max(
			window.latestWholeFramePresentSampleId,
			associatedPresentSampleId);

		const auto sampleIt =
			window.presentSamples.find(associatedPresentSampleId);
		if (sampleIt == window.presentSamples.end()) {
			return sourceGap ?
			           AddSampleResult::SourceGap :
			           AddSampleResult::Ignored;
		}

		const bool invalidGpu =
			gpuMs.has_value() && !IsPositiveFinite(*gpuMs);
		const bool invalidCpu =
			cpuMs.has_value() && !IsPositiveFinite(*cpuMs);
		if (invalidGpu) {
			window.wholeFrameGpuCoverageDiscontinuous = true;
		}
		if (invalidCpu) {
			window.wholeFrameCpuCoverageDiscontinuous = true;
		}

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

		const auto& contribution = sampleIt->second;
		if (addGpu) {
			window.wholeFrameGpu.Add(
				*gpuMs,
				contribution.totalWeight);
		}
		if (addCpu) {
			window.wholeFrameCpu.Add(
				*cpuMs,
				contribution.totalWeight);
		}

		for (std::size_t blockIndex = 0;
			blockIndex < kMeasurementBlockCount;
			++blockIndex) {
			const double blockWeight =
				contribution.blockWeights[blockIndex];
			if (blockWeight <= 0.0)
				continue;

			auto& block = window.blocks[blockIndex];
			if (addGpu) {
				block.wholeFrameGpu.Add(*gpuMs, blockWeight);
				block.wholeFrameGpuSampleCount++;
			}
			if (addCpu) {
				block.wholeFrameCpu.Add(*cpuMs, blockWeight);
				block.wholeFrameCpuSampleCount++;
			}
		}

		if (addGpu && addCpu) {
			window.framePacingEligibleSampleWeight +=
				contribution.totalWeight;
			if (contribution.timingMs >
				std::max(*gpuMs, *cpuMs) +
					framePacingEpsilonMs) {
				window.framePacedSampleWeight +=
					contribution.totalWeight;
			}
		}

		if (sourceGap)
			return AddSampleResult::SourceGap;
		if (duplicateGpuAssociation || duplicateCpuAssociation)
			return AddSampleResult::DuplicateAssociation;
		if (invalidGpu || invalidCpu)
			return AddSampleResult::InvalidValue;
		return AddSampleResult::Added;
	}

	bool MetricCoverageDiagnostics::Meets(
		double minimumCoverage) const
	{
		if (!continuous ||
			!sampleCoverage ||
			!weightCoverage ||
			!std::isfinite(minimumCoverage)) {
			return false;
		}
		const double threshold =
			std::clamp(minimumCoverage, 0.0, 1.0);
		return *sampleCoverage + kSampleWeightEpsilon >= threshold &&
		       *weightCoverage + kSampleWeightEpsilon >= threshold;
	}

	MetricCoverageDiagnostics GetMetricCoverage(
		const SampleWindow& window,
		MetricKind metric)
	{
		const bool continuous =
			IsMetricContinuous(window, metric);
		switch (metric) {
		case MetricKind::Present:
			return MakeCoverageDiagnostics(
				window.presentSampleCount,
				window.presentSampleCount,
				window.present.sampleWeight,
				window.present.sampleWeight,
				continuous);
		case MetricKind::WholeFrameGpu:
			return MakeCoverageDiagnostics(
				window.presentSampleCount,
				static_cast<uint32_t>(
					window.wholeFrameGpuPresentSampleIds.size()),
				window.present.sampleWeight,
				window.wholeFrameGpu.sampleWeight,
				continuous);
		case MetricKind::WholeFrameCpu:
		default:
			return MakeCoverageDiagnostics(
				window.presentSampleCount,
				static_cast<uint32_t>(
					window.wholeFrameCpuPresentSampleIds.size()),
				window.present.sampleWeight,
				window.wholeFrameCpu.sampleWeight,
				continuous);
		}
	}

	MetricCoverageDiagnostics GetMetricCoverage(
		const SampleWindow& window,
		std::size_t blockIndex,
		MetricKind metric)
	{
		if (blockIndex >= kMeasurementBlockCount)
			return {};

		const auto& block = window.blocks[blockIndex];
		const bool continuous =
			IsMetricContinuous(window, metric);
		switch (metric) {
		case MetricKind::Present:
			return MakeCoverageDiagnostics(
				block.presentSampleCount,
				block.presentSampleCount,
				block.present.sampleWeight,
				block.present.sampleWeight,
				continuous);
		case MetricKind::WholeFrameGpu:
			return MakeCoverageDiagnostics(
				block.presentSampleCount,
				block.wholeFrameGpuSampleCount,
				block.present.sampleWeight,
				block.wholeFrameGpu.sampleWeight,
				continuous);
		case MetricKind::WholeFrameCpu:
		default:
			return MakeCoverageDiagnostics(
				block.presentSampleCount,
				block.wholeFrameCpuSampleCount,
				block.present.sampleWeight,
				block.wholeFrameCpu.sampleWeight,
				continuous);
		}
	}

	WindowDiagnostics BuildWindowDiagnostics(
		const SampleWindow& window)
	{
		WindowDiagnostics result;
		result.sampledDurationMs = window.sampledDurationMs;
		result.presentSampleCount = window.presentSampleCount;
		result.wholeFrameGpu =
			GetMetricCoverage(window, MetricKind::WholeFrameGpu);
		result.wholeFrameCpu =
			GetMetricCoverage(window, MetricKind::WholeFrameCpu);

		for (std::size_t blockIndex = 0;
			blockIndex < kMeasurementBlockCount;
			++blockIndex) {
			auto& blockResult = result.blocks[blockIndex];
			blockResult.wholeFrameGpu = GetMetricCoverage(
				window,
				blockIndex,
				MetricKind::WholeFrameGpu);
			blockResult.wholeFrameCpu = GetMetricCoverage(
				window,
				blockIndex,
				MetricKind::WholeFrameCpu);
		}
		return result;
	}

	std::optional<double> GetWindowMeanMs(
		const SampleWindow& window,
		MetricKind metric,
		double minimumCoverage)
	{
		if (!window.complete ||
			!GetMetricCoverage(window, metric)
				.Meets(minimumCoverage)) {
			return std::nullopt;
		}
		return GetWindowMoments(window, metric).Mean();
	}

	std::optional<double> GetBlockMeanMs(
		const SampleWindow& window,
		std::size_t blockIndex,
		MetricKind metric,
		double minimumCoverage)
	{
		if (!window.complete ||
			blockIndex >= kMeasurementBlockCount ||
			!GetMetricCoverage(window, blockIndex, metric)
				.Meets(minimumCoverage)) {
			return std::nullopt;
		}
		return GetBlockMoments(
			window.blocks[blockIndex],
			metric)
		    .Mean();
	}

	std::optional<double> GetWindowFps(
		const SampleWindow& window)
	{
		if (!window.complete ||
			window.presentSourceDiscontinuous ||
			window.sampledDurationMs <= 0.0 ||
			window.present.sampleWeight <= 0.0) {
			return std::nullopt;
		}
		return 1000.0 * window.present.sampleWeight /
		       window.sampledDurationMs;
	}

	std::optional<double> GetBlockFps(
		const SampleWindow& window,
		std::size_t blockIndex)
	{
		if (!window.complete ||
			window.presentSourceDiscontinuous ||
			blockIndex >= kMeasurementBlockCount) {
			return std::nullopt;
		}

		const auto& block = window.blocks[blockIndex];
		if (block.sampledDurationMs <= 0.0 ||
			block.present.sampleWeight <= 0.0) {
			return std::nullopt;
		}
		return 1000.0 * block.present.sampleWeight /
		       block.sampledDurationMs;
	}

	std::optional<double> GetBlockOutputFps(
		const SampleWindow& window,
		std::size_t blockIndex)
	{
		if (!window.complete ||
			window.outputPresentSourceDiscontinuous ||
			blockIndex >= kMeasurementBlockCount) {
			return std::nullopt;
		}

		const auto& block = window.blocks[blockIndex];
		if (block.outputPresentDurationMs + kDurationEpsilonMs <
				kMeasurementBlockDurationMs ||
			block.outputPresentedFrameCount <= 0.0) {
			return std::nullopt;
		}
		return 1000.0 * block.outputPresentedFrameCount /
		       block.outputPresentDurationMs;
	}

	bool IsFramePaced(const SampleWindow& window)
	{
		return window.framePacingInferenceValid &&
		       GetMetricCoverage(
				   window,
				   MetricKind::WholeFrameGpu)
		           .Meets(kDefaultMinimumMetricCoverage) &&
		       GetMetricCoverage(
				   window,
				   MetricKind::WholeFrameCpu)
		           .Meets(kDefaultMinimumMetricCoverage) &&
		       window.framePacingEligibleSampleWeight > 0.0 &&
		       window.framePacedSampleWeight * 2.0 >=
		           window.framePacingEligibleSampleWeight;
	}

	CostResult CalculateCostResult(
		const SampleWindow& current,
		const SampleWindow& comparison,
		double minimumMetricCoverage)
	{
		CostResult result;
		result.currentDiagnostics =
			BuildWindowDiagnostics(current);
		result.comparisonDiagnostics =
			BuildWindowDiagnostics(comparison);

		result.present = CalculateMetricDelta(
			current,
			comparison,
			MetricKind::Present,
			minimumMetricCoverage);
		result.wholeFrameGpu = CalculateMetricDelta(
			current,
			comparison,
			MetricKind::WholeFrameGpu,
			minimumMetricCoverage);
		result.wholeFrameCpu = CalculateMetricDelta(
			current,
			comparison,
			MetricKind::WholeFrameCpu,
			minimumMetricCoverage);
		result.fps = CalculateFpsDelta(
			current,
			comparison,
			GetBlockFps);
		result.outputFps = CalculateFpsDelta(
			current,
			comparison,
			GetBlockOutputFps);

		result.presentSynced =
			current.presentSyncedSampleCount > 0 ||
			comparison.presentSyncedSampleCount > 0;
		result.framePaced =
			current.framePacingInferenceValid &&
			comparison.framePacingInferenceValid &&
			(IsFramePaced(current) || IsFramePaced(comparison));
		return result;
	}
}
