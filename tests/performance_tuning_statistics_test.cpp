#include "Menu/PerformanceTuningStatistics.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace
{
	using namespace PerformanceTuningStatistics;

	bool Near(double left, double right, double epsilon = 1.0e-12)
	{
		return std::abs(left - right) <= epsilon;
	}

	template <std::size_t Size>
	std::array<Moments, Size> MakeBlocks(const std::array<double, Size>& means, int samplesPerBlock)
	{
		std::array<Moments, Size> blocks{};
		for (std::size_t index = 0; index < Size; ++index) {
			for (int sample = 0; sample < samplesPerBlock; ++sample)
				AddMoment(blocks[index], means[index], 1.0);
		}
		return blocks;
	}

	bool CoversBlockMeanStandardError()
	{
		constexpr std::array means{ 8.0, 9.0, 10.0, 11.0, 12.0 };
		const auto sparseBlocks = MakeBlocks(means, 1);
		const auto denseBlocks = MakeBlocks(means, 90);
		std::array<Moments, 2> weightedBlocks{};
		std::array<Moments, 5> incompleteBlocks{};
		AddMoment(weightedBlocks[0], 10.0, 1.0);
		AddMoment(weightedBlocks[1], 20.0, 3.0);
		for (std::size_t index = 0; index + 1 < incompleteBlocks.size(); ++index)
			AddMoment(incompleteBlocks[index], 10.0, 1.0);
		double sparseVariance = 0.0;
		double denseVariance = 0.0;
		double weightedVariance = 0.0;
		double incompleteVariance = 0.0;
		double sparseMean = 0.0;
		double denseMean = 0.0;
		double weightedMean = 0.0;
		double incompleteMean = 0.0;

		return TryGetBlockMeanStatistics(sparseBlocks, sparseMean, sparseVariance) &&
		       TryGetBlockMeanStatistics(denseBlocks, denseMean, denseVariance) &&
		       TryGetBlockMeanStatistics(weightedBlocks, weightedMean, weightedVariance) &&
		       !TryGetBlockMeanStatistics(incompleteBlocks, incompleteMean, incompleteVariance) &&
		       incompleteMean == 0.0 &&
		       incompleteVariance == 0.0 &&
		       Near(sparseMean, 10.0) &&
		       Near(sparseMean, denseMean) &&
		       Near(weightedMean, 15.0) &&
		       Near(sparseVariance, denseVariance) &&
		       sparseVariance > 0.0 &&
		       Near(weightedVariance, 25.0);
	}

	bool CoversSignificanceLimits()
	{
		const auto equalConstant = EvaluateSignificance(0.0, 0.0);
		const auto differentConstant = EvaluateSignificance(0.25, 0.0);
		const auto significant = EvaluateSignificance(1.96, 1.0);
		const auto insignificant = EvaluateSignificance(1.0, 1.0);
		const auto invalid = EvaluateSignificance(
			1.0,
			std::numeric_limits<double>::quiet_NaN());

		return equalConstant.hasStandardError &&
		       !equalConstant.significant &&
		       equalConstant.pValue == 1.0 &&
		       differentConstant.hasStandardError &&
		       differentConstant.significant &&
		       differentConstant.pValue == 0.0 &&
		       significant.significant &&
		       !insignificant.significant &&
		       !invalid.hasStandardError;
	}

	bool CoversMissingSampleTolerance()
	{
		return IsMissingSampleCountWithinLimit(0, 0, 2) &&
		       IsMissingSampleCountWithinLimit(1, 1, 2) &&
		       IsMissingSampleCountWithinLimit(2, 0, 2) &&
		       !IsMissingSampleCountWithinLimit(2, 1, 2) &&
		       !IsMissingSampleCountWithinLimit(
				   std::numeric_limits<std::size_t>::max(),
				   1,
				   2);
	}

	bool CoversInvalidSampleExclusion()
	{
		Moments moments;
		AddMoment(moments, 0.0, 1.0);
		AddMoment(moments, -1.0, 1.0);
		AddMoment(moments, std::numeric_limits<double>::quiet_NaN(), 1.0);
		AddMoment(moments, std::numeric_limits<double>::infinity(), 1.0);
		AddMoment(moments, kMaximumTimingSampleMs + 1.0, 1.0);
		AddMoment(moments, 10.0, 0.0);
		AddMoment(moments, 10.0, std::numeric_limits<double>::quiet_NaN());
		AddMoment(moments, 10.0, std::numeric_limits<double>::max());
		AddMoment(moments, 10.0, 1.0);
		return moments.sampleWeight == 1.0 && GetMean(moments) == 10.0;
	}

	bool CoversTimingDiscontinuities()
	{
		return !IsTimingSampleInterrupted(10, 11, true) &&
		       IsTimingSampleInterrupted(10, 11, false) &&
		       IsTimingSampleInterrupted(10, 12, true) &&
		       IsTimingSampleInterrupted(10, 0, false) &&
		       !IsTimingSampleInterrupted(0, 1, true);
	}
}

int main()
{
	return CoversBlockMeanStandardError() &&
	               CoversSignificanceLimits() &&
	               CoversMissingSampleTolerance() &&
	               CoversInvalidSampleExclusion() &&
	               CoversTimingDiscontinuities() ?
	           0 :
	           1;
}
