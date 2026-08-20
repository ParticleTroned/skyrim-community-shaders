#include "Menu/PerformanceTuningMeasurement.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

		std::optional<double> InterpolateAt(
			double firstTime,
			double firstValue,
			double secondTime,
			double secondValue,
			double targetTime)
		{
			if (!std::isfinite(firstTime) ||
				!std::isfinite(secondTime) ||
				!std::isfinite(targetTime) ||
				!std::isfinite(firstValue) ||
				!std::isfinite(secondValue) ||
				secondTime <= firstTime ||
				targetTime < firstTime ||
				targetTime > secondTime) {
				return std::nullopt;
			}

			const double secondWeight =
				(targetTime - firstTime) /
				(secondTime - firstTime);
			return firstValue +
			       (secondValue - firstValue) * secondWeight;
		}

		template <class ValueGetter>
		RepeatabilityBand CalculateRepeatabilityBand(
			const SampleWindow& currentBefore,
			const SampleWindow& comparison,
			const SampleWindow& currentAfter,
			ValueGetter&& getValue)
		{
			RepeatabilityBand result;
			std::vector<double> availableDeltas;
			availableDeltas.reserve(kMeasurementBlockCount);

			for (std::size_t blockIndex = 0;
				 blockIndex < kMeasurementBlockCount;
				 ++blockIndex) {
				const auto currentBeforeValue =
					getValue(currentBefore, blockIndex);
				const auto comparisonValue =
					getValue(comparison, blockIndex);
				const auto currentAfterValue =
					getValue(currentAfter, blockIndex);
				const auto currentBeforeTime =
					GetBlockMidpointTimeSeconds(
						currentBefore,
						blockIndex);
				const auto comparisonTime =
					GetBlockMidpointTimeSeconds(
						comparison,
						blockIndex);
				const auto currentAfterTime =
					GetBlockMidpointTimeSeconds(
						currentAfter,
						blockIndex);
				if (!currentBeforeValue ||
					!comparisonValue ||
					!currentAfterValue ||
					!currentBeforeTime ||
					!comparisonTime ||
					!currentAfterTime) {
					continue;
				}

				const auto interpolatedCurrent = InterpolateAt(
					*currentBeforeTime,
					*currentBeforeValue,
					*currentAfterTime,
					*currentAfterValue,
					*comparisonTime);
				if (!interpolatedCurrent)
					continue;

				const double delta =
					*interpolatedCurrent - *comparisonValue;
				result.blockDeltas[blockIndex] = delta;
				availableDeltas.push_back(delta);
			}

			result.availableBlockCount =
				static_cast<uint32_t>(availableDeltas.size());
			if (availableDeltas.empty())
				return result;

			std::ranges::sort(availableDeltas);
			result.minimum = availableDeltas.front();
			result.maximum = availableDeltas.back();
			const std::size_t middle = availableDeltas.size() / 2;
			if ((availableDeltas.size() & 1u) != 0u) {
				result.median = availableDeltas[middle];
			} else {
				result.median =
					(availableDeltas[middle - 1] +
						availableDeltas[middle]) *
					0.5;
			}
			return result;
		}

		void CountAgreeingBlocks(
			RepeatabilityBand& band,
			double delta)
		{
			if (delta == 0.0)
				return;

			for (const auto& blockDelta : band.blockDeltas) {
				if (blockDelta &&
					((*blockDelta > 0.0 && delta > 0.0) ||
						(*blockDelta < 0.0 && delta < 0.0))) {
					band.agreeingBlockCount++;
				}
			}
		}

		MetricDelta CalculateMetricDelta(
			const SampleWindow& currentBefore,
			const SampleWindow& comparison,
			const SampleWindow& currentAfter,
			MetricKind metric,
			double minimumMetricCoverage)
		{
			MetricDelta result;
			result.currentBeforeMeanMs = GetWindowMeanMs(
				currentBefore,
				metric,
				minimumMetricCoverage);
			result.comparisonMeanMs = GetWindowMeanMs(
				comparison,
				metric,
				minimumMetricCoverage);
			result.currentAfterMeanMs = GetWindowMeanMs(
				currentAfter,
				metric,
				minimumMetricCoverage);

			const auto currentBeforeTime =
				GetWindowMidpointTimeSeconds(currentBefore);
			const auto comparisonTime =
				GetWindowMidpointTimeSeconds(comparison);
			const auto currentAfterTime =
				GetWindowMidpointTimeSeconds(currentAfter);
			if (!result.currentBeforeMeanMs ||
				!result.comparisonMeanMs ||
				!result.currentAfterMeanMs ||
				!currentBeforeTime ||
				!comparisonTime ||
				!currentAfterTime) {
				return result;
			}

			result.interpolatedCurrentMeanMs = InterpolateAt(
				*currentBeforeTime,
				*result.currentBeforeMeanMs,
				*currentAfterTime,
				*result.currentAfterMeanMs,
				*comparisonTime);
			if (!result.interpolatedCurrentMeanMs)
				return result;

			result.valueMs =
				*result.interpolatedCurrentMeanMs -
				*result.comparisonMeanMs;
			result.currentDriftMs =
				*result.currentAfterMeanMs -
				*result.currentBeforeMeanMs;
			result.practicalFloorMs = std::max(
				kPracticalFloorAbsoluteMs,
				std::abs(*result.comparisonMeanMs) *
					kPracticalFloorRelative);
			result.direction = *result.valueMs > 0.0 ?
			                       1 :
			                       (*result.valueMs < 0.0 ? -1 : 0);

			result.repeatability = CalculateRepeatabilityBand(
				currentBefore,
				comparison,
				currentAfter,
				[metric, minimumMetricCoverage](
					const SampleWindow& window,
					std::size_t blockIndex) {
					return GetBlockMeanMs(
						window,
						blockIndex,
						metric,
						minimumMetricCoverage);
				});
			CountAgreeingBlocks(
				result.repeatability,
				*result.valueMs);

			if (result.repeatability.availableBlockCount <
				kMeasurementBlockCount) {
				result.reliability =
					MetricReliability::InsufficientBlockCoverage;
				return result;
			}
			if (std::abs(*result.valueMs) <=
				*result.practicalFloorMs) {
				result.reliability =
					MetricReliability::BelowPracticalFloor;
				return result;
			}
			if (std::abs(*result.currentDriftMs) >
				std::max(
					*result.practicalFloorMs,
					std::abs(*result.valueMs) *
						kDriftDominanceRatio)) {
				result.reliability =
					MetricReliability::DriftDominated;
				return result;
			}
			if (result.repeatability.agreeingBlockCount <
				kRequiredAgreeingBlockCount) {
				result.reliability =
					MetricReliability::MixedBlockDirections;
				return result;
			}

			result.reliability = MetricReliability::Reliable;
			return result;
		}

		FpsDelta CalculateFpsDelta(
			const SampleWindow& currentBefore,
			const SampleWindow& comparison,
			const SampleWindow& currentAfter)
		{
			FpsDelta result;
			result.currentBefore = GetWindowFps(currentBefore);
			result.comparison = GetWindowFps(comparison);
			result.currentAfter = GetWindowFps(currentAfter);

			const auto currentBeforeTime =
				GetWindowMidpointTimeSeconds(currentBefore);
			const auto comparisonTime =
				GetWindowMidpointTimeSeconds(comparison);
			const auto currentAfterTime =
				GetWindowMidpointTimeSeconds(currentAfter);
			if (!result.currentBefore ||
				!result.comparison ||
				!result.currentAfter ||
				!currentBeforeTime ||
				!comparisonTime ||
				!currentAfterTime) {
				return result;
			}

			result.interpolatedCurrent = InterpolateAt(
				*currentBeforeTime,
				*result.currentBefore,
				*currentAfterTime,
				*result.currentAfter,
				*comparisonTime);
			if (!result.interpolatedCurrent)
				return result;

			result.value =
				*result.interpolatedCurrent - *result.comparison;
			result.currentDrift =
				*result.currentAfter - *result.currentBefore;
			result.repeatability = CalculateRepeatabilityBand(
				currentBefore,
				comparison,
				currentAfter,
				[](const SampleWindow& window, std::size_t blockIndex) {
					return GetBlockFps(window, blockIndex);
				});
			CountAgreeingBlocks(
				result.repeatability,
				*result.value);
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
			state.readyStartPresentSampleId =
				currentPresentSampleId;
			return TransitionGateResult::Pending;
		}

		if (currentPresentSampleId <
			state.readyStartPresentSampleId) {
			state = {};
			return TransitionGateResult::TimingReset;
		}

		const bool readyLongEnough =
			currentTime - state.readyStartTime >=
			std::max(0.0, minimumReadySeconds);
		const bool hasFreshSamples =
			currentPresentSampleId -
				state.readyStartPresentSampleId >=
			minimumFreshPresentCount;
		if (!readyLongEnough || !hasFreshSamples)
			return TransitionGateResult::Pending;

		const double soakSeconds =
			std::max(0.0, postFreshSoakSeconds);
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
		double captureStartTimeSeconds,
		bool framePacingInferenceValid)
	{
		window = {};
		window.captureStartTimeSeconds = captureStartTimeSeconds;
		window.startPresentSampleId = currentPresentSampleId;
		window.lastPresentSampleId = currentPresentSampleId;
		window.lastWholeFrameSampleId = currentWholeFrameSampleId;
		window.framePacingInferenceValid =
			framePacingInferenceValid;
		if (!std::isfinite(captureStartTimeSeconds))
			window.presentSourceDiscontinuous = true;
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
		// arithmetic mean, and make the midpoint/FPS describe time that was not
		// actually captured. The final block may therefore extend past 500 ms,
		// bounded by kMaximumPresentIntervalMs and the renderer's 3 s deadline.
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

	std::optional<double> GetWindowMidpointTimeSeconds(
		const SampleWindow& window)
	{
		if (!std::isfinite(window.captureStartTimeSeconds) ||
			window.sampledDurationMs <= 0.0) {
			return std::nullopt;
		}
		return window.captureStartTimeSeconds +
		       window.sampledDurationMs / 2000.0;
	}

	std::optional<double> GetBlockMidpointTimeSeconds(
		const SampleWindow& window,
		std::size_t blockIndex)
	{
		if (!std::isfinite(window.captureStartTimeSeconds) ||
			blockIndex >= kMeasurementBlockCount ||
			window.blocks[blockIndex].sampledDurationMs <= 0.0) {
			return std::nullopt;
		}
		return window.captureStartTimeSeconds +
		       (static_cast<double>(blockIndex) *
				   kMeasurementBlockDurationMs +
			   window.blocks[blockIndex].sampledDurationMs * 0.5) /
			   1000.0;
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
		const SampleWindow& currentBefore,
		const SampleWindow& comparison,
		const SampleWindow& currentAfter,
		double minimumMetricCoverage)
	{
		CostResult result;
		result.currentBeforeDiagnostics =
			BuildWindowDiagnostics(currentBefore);
		result.comparisonDiagnostics =
			BuildWindowDiagnostics(comparison);
		result.currentAfterDiagnostics =
			BuildWindowDiagnostics(currentAfter);

		result.present = CalculateMetricDelta(
			currentBefore,
			comparison,
			currentAfter,
			MetricKind::Present,
			minimumMetricCoverage);
		result.wholeFrameGpu = CalculateMetricDelta(
			currentBefore,
			comparison,
			currentAfter,
			MetricKind::WholeFrameGpu,
			minimumMetricCoverage);
		result.wholeFrameCpu = CalculateMetricDelta(
			currentBefore,
			comparison,
			currentAfter,
			MetricKind::WholeFrameCpu,
			minimumMetricCoverage);
		result.fps = CalculateFpsDelta(
			currentBefore,
			comparison,
			currentAfter);

		result.presentSynced =
			currentBefore.presentSyncedSampleCount > 0 ||
			comparison.presentSyncedSampleCount > 0 ||
			currentAfter.presentSyncedSampleCount > 0;
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
