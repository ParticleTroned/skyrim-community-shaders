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

	inline void AddMoment(Moments& moments, double value, double sampleWeight)
	{
		if (!IsValidTiming(value) || !std::isfinite(sampleWeight) || sampleWeight <= 0.0)
			return;

		moments.sum += value * sampleWeight;
		moments.sampleWeight += sampleWeight;
	}

	[[nodiscard]] inline double GetMean(const Moments& moments)
	{
		return moments.sampleWeight > 0.0 ? moments.sum / moments.sampleWeight : 0.0;
	}

	[[nodiscard]] inline Moments CombineMoments(std::span<const Moments> blocks)
	{
		Moments combined;
		for (const auto& block : blocks) {
			combined.sum += block.sum;
			combined.sampleWeight += block.sampleWeight;
		}
		return combined;
	}

	[[nodiscard]] inline bool TryGetBlockMeanVariance(std::span<const Moments> blocks, double& meanVariance)
	{
		meanVariance = 0.0;
		const auto combined = CombineMoments(blocks);
		std::size_t blockCount = 0;
		for (const auto& block : blocks) {
			if (block.sampleWeight <= 0.0)
				continue;

			blockCount++;
		}

		if (blockCount <= 1 || combined.sampleWeight <= 0.0)
			return false;

		const double weightedMean = GetMean(combined);
		double weightedDeviation = 0.0;
		for (const auto& block : blocks) {
			if (block.sampleWeight <= 0.0)
				continue;

			const double normalizedWeight = block.sampleWeight / combined.sampleWeight;
			const double difference = GetMean(block) - weightedMean;
			weightedDeviation += normalizedWeight * normalizedWeight * difference * difference;
		}

		// The finite-block correction reduces to sample variance / N for
		// equally weighted blocks without treating their frames as independent.
		const double finiteBlockCorrection =
			static_cast<double>(blockCount) / static_cast<double>(blockCount - 1);
		meanVariance = finiteBlockCorrection * weightedDeviation;
		return std::isfinite(meanVariance);
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
