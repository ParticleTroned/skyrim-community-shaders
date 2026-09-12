#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"

#include <algorithm>
#include <chrono>
#include <iostream>
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

		aggregator.SetSettingsA({ { "setting", 1 } });
		aggregator.SetSettingsB({ { "setting", 2 } });
		aggregator.Clear();
		Check(!aggregator.HasResults() && !aggregator.HasSettingsA() && !aggregator.HasSettingsB(), "clear retained results or settings");
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
}

int main()
{
	InitialWarmupIsExcluded();
	StopDuringWarmupProducesNoResults();
	StartingWithAIsMeasured();
	std::cout << "A/B warm-up, lifecycle, timing and aggregation checks passed\n";
}
