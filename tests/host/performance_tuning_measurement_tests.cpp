#include "Menu/PerformanceTuningMeasurement.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
	using PerformanceTuning::AddSampleResult;
	using PerformanceTuning::Moments;
	using PerformanceTuning::SampleWindow;

	void SetSplitMoments(
		Moments& total,
		Moments& firstHalf,
		Moments& secondHalf,
		double firstHalfValue,
		double secondHalfValue,
		int halfSampleCount = 90)
	{
		for (int i = 0; i < halfSampleCount; ++i) {
			total.Add(firstHalfValue, 1.0);
			firstHalf.Add(firstHalfValue, 1.0);
		}
		for (int i = 0; i < halfSampleCount; ++i) {
			total.Add(secondHalfValue, 1.0);
			secondHalf.Add(secondHalfValue, 1.0);
		}
	}

	SampleWindow MakeSplitPresentWindow(
		double firstHalfPresentMs,
		double secondHalfPresentMs,
		int halfSampleCount = 90)
	{
		SampleWindow window;
		SetSplitMoments(
			window.present,
			window.firstHalfPresent,
			window.secondHalfPresent,
			firstHalfPresentMs,
			secondHalfPresentMs,
			halfSampleCount);
		return window;
	}

	void AddAlternatingHalf(
		Moments& total,
		Moments& half,
		double firstValue,
		double secondValue,
		int pairCount)
	{
		for (int i = 0; i < pairCount; ++i) {
			total.Add(firstValue, 1.0);
			total.Add(secondValue, 1.0);
			half.Add(firstValue, 1.0);
			half.Add(secondValue, 1.0);
		}
	}

	SampleWindow MakeStationaryPresentWindow(
		double firstValue,
		double secondValue,
		int halfPairCount = 45)
	{
		SampleWindow window;
		AddAlternatingHalf(
			window.present,
			window.firstHalfPresent,
			firstValue,
			secondValue,
			halfPairCount);
		AddAlternatingHalf(
			window.present,
			window.secondHalfPresent,
			firstValue,
			secondValue,
			halfPairCount);
		return window;
	}

	SampleWindow MakeConstantWindow(
		double presentIntervalMs,
		std::optional<double> wholeFrameGpuMs = std::nullopt,
		std::optional<double> wholeFrameCpuMs = std::nullopt,
		uint64_t firstSampleId = 1)
	{
		SampleWindow window;
		PerformanceTuning::BeginSampleWindow(
			window,
			firstSampleId - 1,
			firstSampleId - 1);

		for (uint64_t sampleId = firstSampleId;; ++sampleId) {
			const auto result = PerformanceTuning::AddPresentSample(
				window,
				sampleId,
				presentIntervalMs,
				false);
			REQUIRE(result != AddSampleResult::Ignored);
			REQUIRE(result != AddSampleResult::SourceReset);

			PerformanceTuning::AddWholeFrameSample(
				window,
				sampleId,
				sampleId,
				wholeFrameGpuMs,
				wholeFrameCpuMs);

			if (result == AddSampleResult::Complete)
				break;
		}

		return window;
	}
}

TEST_CASE(
	"Present sampling clips the final interval to an exact wall-clock window",
	"[performance-tuning][present]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0);

	for (uint64_t sampleId = 1; sampleId <= 299; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(window, sampleId, 10.0, false) ==
			AddSampleResult::Added);
	}

	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 300, 20.0, false) ==
		AddSampleResult::Complete);
	REQUIRE(window.sampledDurationMs == Catch::Approx(3000.0));
	REQUIRE(window.present.sampleWeight == Catch::Approx(299.5));
	REQUIRE(window.present.Mean() == Catch::Approx(3000.0 / 299.5));
}

TEST_CASE(
	"Total cost is derived only from direct Present intervals",
	"[performance-tuning][present][invariant]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 2.0, 4.0, 1);
	const auto comparison = MakeConstantWindow(10.0, 30.0, 35.0, 1000);
	const auto currentAfter = MakeConstantWindow(16.0, 2.0, 4.0, 2000);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.available);
	REQUIRE(result.present.valueMs == Catch::Approx(6.0));
	REQUIRE(result.present.statisticallySignificant);
	REQUIRE(result.wholeFrameGpu.available);
	REQUIRE(result.wholeFrameGpu.valueMs == Catch::Approx(-28.0));
	REQUIRE(result.wholeFrameGpu.statisticallySignificant);
	REQUIRE(result.wholeFrameCpu.available);
	REQUIRE(result.wholeFrameCpu.valueMs == Catch::Approx(-31.0));
	REQUIRE(result.wholeFrameCpu.statisticallySignificant);
	REQUIRE(result.hasFps);
	REQUIRE(result.fpsDelta == Catch::Approx(1000.0 / 16.0 - 1000.0 / 10.0));
}

TEST_CASE(
	"Sub-threshold Present differences remain inconclusive",
	"[performance-tuning][uncertainty]")
{
	const auto currentBefore = MakeConstantWindow(16.0, std::nullopt, std::nullopt, 1);
	const auto comparison = MakeConstantWindow(15.95, std::nullopt, std::nullopt, 1000);
	const auto currentAfter = MakeConstantWindow(16.0, std::nullopt, std::nullopt, 2000);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.available);
	REQUIRE(result.present.valueMs == Catch::Approx(0.05));
	REQUIRE(result.present.significanceThresholdMs == Catch::Approx(0.099));
	REQUIRE(result.present.statisticallySignificant);
	REQUIRE_FALSE(result.present.significant);
	REQUIRE(result.fpsStatisticallySignificant);
	REQUIRE_FALSE(result.fpsSignificant);
}

TEST_CASE(
	"Disagreement between current windows contributes to uncertainty",
	"[performance-tuning][uncertainty][aba]")
{
	const auto currentBefore =
		MakeConstantWindow(16.0, std::nullopt, std::nullopt, 1);
	const auto comparison =
		MakeConstantWindow(16.0, std::nullopt, std::nullopt, 1000);
	const auto currentAfter =
		MakeConstantWindow(16.2, std::nullopt, std::nullopt, 2000);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.valueMs == Catch::Approx(0.1));
	REQUIRE(result.present.margin95Ms == Catch::Approx(0.196));
	REQUIRE_FALSE(result.present.statisticallySignificant);
	REQUIRE_FALSE(result.present.significant);
	REQUIRE(result.fpsMargin95 > 0.0);
	REQUIRE_FALSE(result.fpsStatisticallySignificant);
	REQUIRE_FALSE(result.fpsSignificant);
}

TEST_CASE(
	"Stationary sampling error contributes to the confidence interval",
	"[performance-tuning][uncertainty][statistics]")
{
	const auto currentBefore = MakeStationaryPresentWindow(12.0, 20.0);
	const auto comparison = MakeStationaryPresentWindow(10.0, 18.0);
	const auto currentAfter = MakeStationaryPresentWindow(12.0, 20.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);
	const double windowMeanVariance = 16.0 / 180.0;

	REQUIRE(result.present.available);
	REQUIRE(result.present.valueMs == Catch::Approx(2.0));
	REQUIRE(
		result.present.margin95Ms ==
		Catch::Approx(
			1.96 * std::sqrt(windowMeanVariance * 1.5)));
	REQUIRE(result.present.statisticallySignificant);
}

TEST_CASE(
	"Comparison-window drift widens confidence without discarding the result",
	"[performance-tuning][uncertainty][drift][comparison]")
{
	const auto currentBefore = MakeSplitPresentWindow(16.0, 16.0);
	const auto comparison = MakeSplitPresentWindow(14.0, 16.0);
	const auto currentAfter = MakeSplitPresentWindow(16.0, 16.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.available);
	REQUIRE(result.present.valueMs == Catch::Approx(1.0));
	REQUIRE(result.present.margin95Ms == Catch::Approx(1.96));
	REQUIRE_FALSE(result.present.statisticallySignificant);
	REQUIRE_FALSE(result.present.significant);
	REQUIRE(result.hasFps);
	REQUIRE(
		result.fpsMargin95 ==
		Catch::Approx(1.96 * 1000.0 / (15.0 * 15.0)));
	REQUIRE_FALSE(result.fpsStatisticallySignificant);
	REQUIRE_FALSE(result.fpsSignificant);
}

TEST_CASE(
	"A-B-A drift widens confidence without discarding the result",
	"[performance-tuning][uncertainty][drift][aba]")
{
	const auto currentBefore = MakeSplitPresentWindow(15.0, 15.0);
	const auto comparison = MakeSplitPresentWindow(15.0, 15.0);
	const auto currentAfter = MakeSplitPresentWindow(17.0, 17.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.available);
	REQUIRE(result.present.valueMs == Catch::Approx(1.0));
	REQUIRE(result.present.margin95Ms == Catch::Approx(1.96));
	REQUIRE_FALSE(result.present.statisticallySignificant);
	REQUIRE_FALSE(result.present.significant);
}

TEST_CASE(
	"Large effects remain significant when confidence clears observed drift",
	"[performance-tuning][uncertainty][drift][significance]")
{
	const auto currentBefore = MakeSplitPresentWindow(16.0, 18.0);
	const auto comparison = MakeSplitPresentWindow(10.0, 10.0);
	const auto currentAfter = MakeSplitPresentWindow(16.0, 18.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.available);
	REQUIRE(result.present.valueMs == Catch::Approx(7.0));
	REQUIRE(result.present.margin95Ms == Catch::Approx(1.96 / std::sqrt(2.0)));
	REQUIRE(result.present.statisticallySignificant);
	REQUIRE(result.present.significant);
	REQUIRE(result.fpsStatisticallySignificant);
	REQUIRE(result.fpsSignificant);
}

TEST_CASE(
	"FPS significance uses the FPS confidence interval",
	"[performance-tuning][uncertainty][fps][significance]")
{
	const auto currentBefore = MakeSplitPresentWindow(20.0, 20.0);
	const auto comparison = MakeSplitPresentWindow(6.0, 14.0);
	const auto currentAfter = MakeSplitPresentWindow(20.0, 20.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.valueMs == Catch::Approx(10.0));
	REQUIRE(result.present.margin95Ms == Catch::Approx(7.84));
	REQUIRE(result.present.statisticallySignificant);
	REQUIRE(result.hasFps);
	REQUIRE(result.fpsDelta == Catch::Approx(-50.0));
	REQUIRE(result.fpsMargin95 == Catch::Approx(78.4));
	REQUIRE_FALSE(result.fpsStatisticallySignificant);
	REQUIRE_FALSE(result.fpsSignificant);
}

TEST_CASE(
	"GPU and CPU confidence use their own half-window variation",
	"[performance-tuning][uncertainty][drift][coverage]")
{
	auto currentBefore = MakeSplitPresentWindow(16.0, 16.0);
	auto comparison = MakeSplitPresentWindow(15.0, 15.0);
	auto currentAfter = MakeSplitPresentWindow(16.0, 16.0);
	SetSplitMoments(
		currentBefore.wholeFrameGpu,
		currentBefore.firstHalfWholeFrameGpu,
		currentBefore.secondHalfWholeFrameGpu,
		8.0,
		10.0);
	SetSplitMoments(
		comparison.wholeFrameGpu,
		comparison.firstHalfWholeFrameGpu,
		comparison.secondHalfWholeFrameGpu,
		8.0,
		8.0);
	SetSplitMoments(
		currentAfter.wholeFrameGpu,
		currentAfter.firstHalfWholeFrameGpu,
		currentAfter.secondHalfWholeFrameGpu,
		8.0,
		10.0);
	SetSplitMoments(
		currentBefore.wholeFrameCpu,
		currentBefore.firstHalfWholeFrameCpu,
		currentBefore.secondHalfWholeFrameCpu,
		5.0,
		5.0);
	SetSplitMoments(
		comparison.wholeFrameCpu,
		comparison.firstHalfWholeFrameCpu,
		comparison.secondHalfWholeFrameCpu,
		4.0,
		4.0);
	SetSplitMoments(
		currentAfter.wholeFrameCpu,
		currentAfter.firstHalfWholeFrameCpu,
		currentAfter.secondHalfWholeFrameCpu,
		5.0,
		5.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.wholeFrameGpu.available);
	REQUIRE(result.wholeFrameGpu.valueMs == Catch::Approx(1.0));
	REQUIRE_FALSE(result.wholeFrameGpu.statisticallySignificant);
	REQUIRE(result.wholeFrameCpu.available);
	REQUIRE(result.wholeFrameCpu.valueMs == Catch::Approx(1.0));
	REQUIRE(result.wholeFrameCpu.statisticallySignificant);
}

TEST_CASE(
	"Profiler metrics require complete coverage in every window",
	"[performance-tuning][coverage]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 8.0, 5.0, 1);
	const auto comparison = MakeConstantWindow(14.0, std::nullopt, 4.0, 1000);
	const auto currentAfter = MakeConstantWindow(16.0, 8.0, 5.0, 2000);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.available);
	REQUIRE_FALSE(result.wholeFrameGpu.available);
	REQUIRE(result.wholeFrameCpu.available);
}

TEST_CASE(
	"Duplicate and reset sample identifiers cannot contaminate a window",
	"[performance-tuning][source-reset]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 100, 200);

	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 101, 16.0, false) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 101, 16.0, false) ==
		AddSampleResult::Ignored);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 103, 16.0, false) ==
		AddSampleResult::SourceGap);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 99, 16.0, false) ==
		AddSampleResult::SourceReset);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 102, 16.0, false) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 103, 16.0, false) ==
		AddSampleResult::Added);

	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			201,
			101,
			8.0,
			5.0) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			201,
			101,
			8.0,
			5.0) ==
		AddSampleResult::Ignored);
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			203,
			102,
			8.0,
			5.0) ==
		AddSampleResult::SourceGap);
	REQUIRE(window.wholeFrameSourceDiscontinuous);
	REQUIRE_FALSE(
		PerformanceTuning::HasCompleteMetricCoverage(
			window,
			window.wholeFrameGpu));
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			204,
			103,
			8.0,
			5.0) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			199,
			101,
			8.0,
			5.0) ==
		AddSampleResult::SourceReset);
}

TEST_CASE(
	"Whole-frame channels reject duplicate Present associations",
	"[performance-tuning][coverage][association]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0);

	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 1, 16.0, false) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 2, 16.0, false) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			1,
			1,
			8.0,
			5.0) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window,
			2,
			1,
			8.0,
			5.0) ==
		AddSampleResult::DuplicateAssociation);

	REQUIRE(
		window.wholeFrameGpu.sampleWeight ==
		Catch::Approx(1.0));
	REQUIRE(
		window.wholeFrameCpu.sampleWeight ==
		Catch::Approx(1.0));
	REQUIRE_FALSE(
		PerformanceTuning::HasCompleteMetricCoverage(
			window,
			window.wholeFrameGpu));
	REQUIRE_FALSE(
		PerformanceTuning::HasCompleteMetricCoverage(
			window,
			window.wholeFrameCpu));
}
