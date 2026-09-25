#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Utils/Statistics.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace std::chrono_literals;
	using Clock = ABTestAggregator::Clock;
	const auto start = Clock::time_point{ 100s };

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Frames(ABTestAggregator& aggregator, int count, float milliseconds)
	{
		DrawCallRow row{};
		row.label = "Total:";
		row.shaderType = -1;
		row.frameTime = milliseconds;
		for (int i = 0; i < count; ++i)
			aggregator.OnFrame({ row });
	}

	void InitialWarmupIsExcluded()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::B, start);
		Frames(aggregator, 30, 1.0f);
		aggregator.OnABSwitch(ABVariant::B, start + 1s);
		Check(aggregator.IsWarmingUp() && !aggregator.HasResults(), "initial B became a measured interval");
		Check(aggregator.GetTotalFrameCount() == 0 && aggregator.GetTotalTestDuration() == 0, "warm-up affected totals");
		Check(aggregator.GetTestStartTime() == Clock::time_point{}, "measurement started during warm-up");

		aggregator.OnABSwitch(ABVariant::A, start + 5s);
		Check(!aggregator.IsWarmingUp() && aggregator.GetTestStartTime() == start + 5s, "A did not start measured time");
		Frames(aggregator, 10, 10.0f);
		aggregator.OnABSwitch(ABVariant::A, start + 6s);
		Check(!aggregator.HasResults(), "duplicate switch split the measured interval");
		aggregator.OnABSwitch(ABVariant::B, start + 10s);
		Check(!aggregator.IsWarmingUp(), "later B interval was discarded");
		Check(aggregator.GetTotalFrameCount() == 10, "warm-up contaminated outlier history or frame counts");
		Frames(aggregator, 10, 20.0f);
		Frames(aggregator, 1, 200.0f);
		aggregator.OnTestEnd(start + 15s);
		Frames(aggregator, 1, 20.0f);
		aggregator.OnTestEnd(start + 20s);

		Check(aggregator.GetIntervals().size() == 2, "warm-up or duplicate end was retained");
		Check(aggregator.GetTotalFrameCount() == 20, "measured sample count is wrong");
		Check(aggregator.GetTotalTestDuration() == 10.0f, "warm-up contributed to test duration");
		Check(aggregator.GetTestEndTime() == start + 15s, "repeated stop changed the end timestamp");
		Check(aggregator.GetIntervals()[0].excludedFrames == 0 && aggregator.GetIntervals()[1].excludedFrames == 1,
			"measured outlier filtering changed");
		const auto results = aggregator.GetAggregatedResults();
		const auto total = std::ranges::find_if(results, [](const auto& row) { return row.shaderType == -1; });
		Check(total != results.end() && total->meanA == 10.0f && total->meanB == 20.0f &&
				  total->frameCountA == 10 && total->frameCountB == 10,
			"warm-up skewed A/B results");

		aggregator.Clear();
		Check(!aggregator.HasResults(), "clear retained results");
		Check(aggregator.GetTestStartTime() == Clock::time_point{} && aggregator.GetTestEndTime() == Clock::time_point{}, "clear retained timestamps");
		aggregator.OnABSwitch(ABVariant::B, start + 30s);
		Check(aggregator.IsWarmingUp(), "restart skipped warm-up");
	}

	void StopDuringWarmupProducesNoResults()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::B, start);
		Frames(aggregator, 30, 150.0f);
		aggregator.OnTestEnd(start + 2s);
		Check(!aggregator.IsWarmingUp() && !aggregator.HasResults(), "cancelled warm-up produced results");
		Check(aggregator.GetTotalFrameCount() == 0 && aggregator.GetTotalTestDuration() == 0, "cancelled warm-up contributed samples or duration");
		Check(aggregator.GetTestEndTime() == Clock::time_point{}, "unmeasured test has a measurement end time");
	}

	void StartingWithAIsMeasured()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::A, start);
		Frames(aggregator, 1, 10.0f);
		aggregator.OnABSwitch(ABVariant::B, start + 2s);
		Frames(aggregator, 1, 20.0f);
		aggregator.OnTestEnd(start + 4s);
		Check(aggregator.GetTotalFrameCount() == 2 && aggregator.GetTotalTestDuration() == 4.0f,
			"an A-first session discarded measured intervals");
	}

	void VariantsHaveIndependentOutlierBaselines()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::A, start);
		Frames(aggregator, 30, 5.0f);
		aggregator.OnABSwitch(ABVariant::B, start + 2s);
		Frames(aggregator, 30, 25.0f);
		aggregator.OnTestEnd(start + 4s);
		Check(aggregator.GetIntervals()[1].excludedFrames == 0 && aggregator.GetTotalFrameCount() == 60,
			"a legitimately slower B was rejected against A's baseline");
	}

	void InvalidFramesAreRejectedBeforeBuildingHistory()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::A, start);
		aggregator.OnFrame({});
		for (float value : { 0.0f, -1.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
			Frames(aggregator, 1, value);
		DrawCallRow total{};
		total.shaderType = -1;
		total.frameTime = 10.0f;
		DrawCallRow shader{};
		shader.shaderType = 1;
		shader.frameTime = std::numeric_limits<float>::quiet_NaN();
		aggregator.OnFrame({ total, shader });
		Frames(aggregator, 10, 10.0f);
		aggregator.OnTestEnd(start + 2s);
		Check(aggregator.GetTotalFrameCount() == 10 && aggregator.GetIntervals()[0].excludedFrames == 7,
			"unavailable, non-finite or startup outlier frames entered the results");
	}

	void CoverageRequiresBothVariants()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::B, start);
		Frames(aggregator, 200, 1.0f);
		aggregator.OnABSwitch(ABVariant::A, start + 5s);
		Frames(aggregator, 200, 10.0f);
		aggregator.OnABSwitch(ABVariant::B, start + 15s);
		Check(aggregator.GetCoverage() == ABTestCoverage::Insufficient, "A-only results were considered sufficient");
		for (const auto& row : aggregator.GetAggregatedResults())
			Check(!row.HasBothVariants(), "warm-up supplied a comparison baseline");
		Frames(aggregator, 1, 20.0f);
		aggregator.OnABSwitch(ABVariant::A, start + 25s);
		Check(aggregator.GetCoverage() == ABTestCoverage::Insufficient, "one B sample qualified against many A samples");
		aggregator.OnABSwitch(ABVariant::B, start + 26s);
		Frames(aggregator, 29, 20.0f);
		aggregator.OnABSwitch(ABVariant::A, start + 31s);
		Check(aggregator.GetCoverage() == ABTestCoverage::Marginal, "valid marginal coverage was lost");
		aggregator.OnABSwitch(ABVariant::B, start + 32s);
		Frames(aggregator, 70, 20.0f);
		aggregator.OnTestEnd(start + 37s);
		Check(aggregator.GetCoverage() == ABTestCoverage::Sufficient, "balanced measured coverage was not recognized");
		Check(aggregator.GetVariantStatistics(ABVariant::A).frames == 200 &&
				  aggregator.GetVariantStatistics(ABVariant::B).frames == 100,
			"per-variant counters included warm-up");
		for (const auto& row : aggregator.GetAggregatedResults())
			Check(row.HasBothVariants(), "measured pair was marked unavailable");
	}

	void PoorCoverageInOneVariantCannotBeHidden()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::A, start);
		Frames(aggregator, 1000, 10.0f);
		aggregator.OnABSwitch(ABVariant::B, start + 10s);
		Frames(aggregator, 100, 20.0f);
		Frames(aggregator, 100, 200.0f);
		aggregator.OnTestEnd(start + 20s);
		Check(aggregator.GetCoverage() == ABTestCoverage::Insufficient, "A masked B's failed valid-frame threshold");
	}

	void StatisticsUseTheExistingMedianContract()
	{
		Check(Util::Median({}) == 0 && Util::Median({ 3 }) == 3, "empty or single median changed");
		Check(Util::Median({ 8, 2, 6, 4 }) == 5 && Util::Median({ 8, 2, 6 }) == 6, "median does not use the middle samples");
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::A, start);
		Frames(aggregator, 1, 10.0f);
		Frames(aggregator, 1, 20.0f);
		aggregator.OnABSwitch(ABVariant::B, start + 2s);
		Frames(aggregator, 1, 30.0f);
		Frames(aggregator, 1, 40.0f);
		aggregator.OnTestEnd(start + 4s);
		const auto results = aggregator.GetAggregatedResults();
		Check(results.size() == 1 && results[0].medianA == 15 && results[0].medianB == 35,
			"even-sized A/B medians did not average their middle samples");
	}

	void SignedResidualsDoNotInvalidateMeasuredFrames()
	{
		ABTestAggregator aggregator;
		aggregator.OnABSwitch(ABVariant::A, start);
		DrawCallRow total{}, other{};
		total.shaderType = -1;
		total.frameTime = 10;
		other.shaderType = -2;
		other.frameTime = -2;
		aggregator.OnFrame({ total, other });
		aggregator.OnTestEnd(start + 1s);
		Check(aggregator.GetTotalFrameCount() == 1, "a finite signed Other residual invalidated the whole frame");
	}
}

int main()
{
	try {
		InitialWarmupIsExcluded();
		StopDuringWarmupProducesNoResults();
		StartingWithAIsMeasured();
		VariantsHaveIndependentOutlierBaselines();
		InvalidFramesAreRejectedBeforeBuildingHistory();
		CoverageRequiresBothVariants();
		PoorCoverageInOneVariantCannotBeHidden();
		StatisticsUseTheExistingMedianContract();
		SignedResidualsDoNotInvalidateMeasuredFrames();
		std::cout << "A/B warm-up, lifecycle, sampling, coverage and aggregation checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
