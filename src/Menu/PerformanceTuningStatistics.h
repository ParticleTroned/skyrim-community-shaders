#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace PerformanceTuningStatistics
{
	inline constexpr double kMaximumTimingSampleMs = 1000.0;
	inline constexpr double kSignificancePValue = 0.05;
	inline constexpr double kSqrtTwo = 1.4142135623730951;

	struct Moments
	{
		double sum = 0.0;
		double sampleWeight = 0.0;
	};

	struct Significance
	{
		double standardError = 0.0;
		double pValue = 1.0;
		bool hasStandardError = false;
		bool significant = false;
	};

	[[nodiscard]] inline bool IsValidTiming(double value)
	{
		return std::isfinite(value) && value > 0.0 && value <= kMaximumTimingSampleMs;
	}

	inline bool AddMoment(Moments& moments, double value, double sampleWeight)
	{
		if (!IsValidTiming(value) || !std::isfinite(sampleWeight) || sampleWeight <= 0.0)
			return false;

		const double candidateSum = moments.sum + value * sampleWeight;
		const double candidateWeight = moments.sampleWeight + sampleWeight;
		if (!std::isfinite(candidateSum) || !std::isfinite(candidateWeight))
			return false;

		moments.sum = candidateSum;
		moments.sampleWeight = candidateWeight;
		return true;
	}

	[[nodiscard]] inline double GetMean(const Moments& moments)
	{
		return moments.sampleWeight > 0.0 ? moments.sum / moments.sampleWeight : 0.0;
	}

	/** Check combined missing-sample coverage without overflowing the count. */
	[[nodiscard]] inline bool IsMissingSampleCountWithinLimit(
		std::size_t currentMissingSampleCount,
		std::size_t comparisonMissingSampleCount,
		std::size_t maximumMissingSampleCount)
	{
		return currentMissingSampleCount <= maximumMissingSampleCount &&
		       comparisonMissingSampleCount <= maximumMissingSampleCount - currentMissingSampleCount;
	}

	/** Calculate an equal-weight mean and its variance from complete intervals. */
	[[nodiscard]] inline bool TryGetBlockMeanStatistics(
		std::span<const Moments> blocks,
		double& mean,
		double& meanVariance)
	{
		mean = 0.0;
		meanVariance = 0.0;
		if (blocks.size() <= 1)
			return false;

		double blockMeanSum = 0.0;
		for (const auto& block : blocks) {
			if (!std::isfinite(block.sum) || !std::isfinite(block.sampleWeight) || block.sampleWeight <= 0.0)
				return false;

			const double blockMean = GetMean(block);
			if (!std::isfinite(blockMean))
				return false;
			blockMeanSum += blockMean;
		}
		const double candidateMean = blockMeanSum / static_cast<double>(blocks.size());
		if (!std::isfinite(candidateMean))
			return false;

		double squaredDeviation = 0.0;
		for (const auto& block : blocks) {
			const double difference = GetMean(block) - candidateMean;
			squaredDeviation += difference * difference;
		}

		const double blockCount = static_cast<double>(blocks.size());
		const double candidateMeanVariance = squaredDeviation / (blockCount * (blockCount - 1.0));
		if (!std::isfinite(candidateMeanVariance))
			return false;

		mean = candidateMean;
		meanVariance = candidateMeanVariance;
		return true;
	}

	[[nodiscard]] inline Significance EvaluateSignificance(double delta, double standardError)
	{
		Significance result;
		if (!std::isfinite(delta) || !std::isfinite(standardError) || standardError < 0.0)
			return result;

		result.standardError = standardError;
		result.hasStandardError = true;
		if (standardError == 0.0) {
			result.pValue = delta == 0.0 ? 1.0 : 0.0;
			result.significant = delta != 0.0;
			return result;
		}

		const double zScore = std::abs(delta) / standardError;
		result.pValue = std::erfc(zScore / kSqrtTwo);
		result.significant = result.pValue <= kSignificancePValue;
		return result;
	}

	[[nodiscard]] inline bool IsTimingSampleInterrupted(
		std::uint32_t previousFrameCount,
		std::uint32_t currentFrameCount,
		bool validTiming)
	{
		if (previousFrameCount == 0)
			return false;

		return currentFrameCount == 0 ||
		       currentFrameCount != previousFrameCount + 1 ||
		       !validTiming;
	}
}
