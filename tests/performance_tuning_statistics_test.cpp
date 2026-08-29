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
		constexpr std::array means{ 9.0, 10.0, 11.0, 9.0, 10.0, 11.0 };
		const auto sparseBlocks = MakeBlocks(means, 1);
		const auto denseBlocks = MakeBlocks(means, 90);
		std::array<Moments, 2> weightedBlocks{};
		AddMoment(weightedBlocks[0], 10.0, 1.0);
		AddMoment(weightedBlocks[1], 20.0, 3.0);
		double sparseVariance = 0.0;
		double denseVariance = 0.0;
		double weightedVariance = 0.0;

		return TryGetBlockMeanVariance(sparseBlocks, sparseVariance) &&
		       TryGetBlockMeanVariance(denseBlocks, denseVariance) &&
		       TryGetBlockMeanVariance(weightedBlocks, weightedVariance) &&
		       Near(GetMean(CombineMoments(weightedBlocks)), 17.5) &&
		       Near(sparseVariance, denseVariance) &&
		       sparseVariance > 0.0 &&
		       Near(weightedVariance, 14.0625);
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
		AddMoment(moments, 10.0, 1.0);
		return moments.sampleWeight == 1.0 && GetMean(moments) == 10.0;
	}

	bool CoversBaselineDiscontinuities()
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
	               CoversInvalidSampleExclusion() &&
	               CoversBaselineDiscontinuities() ?
	           0 :
	           1;
}
