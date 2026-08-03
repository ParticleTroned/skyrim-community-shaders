#include "Menu/PerformanceTuningMeasurement.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
	using PerformanceTuning::AddSampleResult;
	using PerformanceTuning::SampleWindow;

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
	REQUIRE(result.wholeFrameGpu.available);
	REQUIRE(result.wholeFrameGpu.valueMs == Catch::Approx(-28.0));
	REQUIRE(result.wholeFrameCpu.available);
	REQUIRE(result.wholeFrameCpu.valueMs == Catch::Approx(-31.0));
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
	REQUIRE_FALSE(result.present.significant);
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

	REQUIRE(
		PerformanceTuning::AreCurrentWindowsEquivalent(
			currentBefore,
			currentAfter));

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.valueMs == Catch::Approx(0.1));
	REQUIRE(result.present.margin95Ms == Catch::Approx(0.196));
	REQUIRE_FALSE(result.present.significant);
	REQUIRE(result.fpsMargin95 > 0.0);
	REQUIRE_FALSE(result.fpsSignificant);
}

TEST_CASE(
	"Drift within a measurement window is rejected",
	"[performance-tuning][stability]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0);

	uint64_t sampleId = 1;
	for (; window.sampledDurationMs < 1500.0; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(window, sampleId, 10.0, false) ==
			AddSampleResult::Added);
	}
	for (;; ++sampleId) {
		const auto result =
			PerformanceTuning::AddPresentSample(window, sampleId, 20.0, false);
		if (result == AddSampleResult::Complete)
			break;
		REQUIRE(result == AddSampleResult::Added);
	}

	REQUIRE_FALSE(PerformanceTuning::IsInternallyStable(window));
}

TEST_CASE(
	"Restored-current drift is rejected",
	"[performance-tuning][stability]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 8.0, 5.0, 1);
	const auto matchingAfter = MakeConstantWindow(16.0, 8.0, 5.0, 1000);
	const auto driftedAfter = MakeConstantWindow(20.0, 12.0, 7.0, 2000);

	REQUIRE(
		PerformanceTuning::AreCurrentWindowsEquivalent(
			currentBefore,
			matchingAfter));
	REQUIRE_FALSE(
		PerformanceTuning::AreCurrentWindowsEquivalent(
			currentBefore,
			driftedAfter));
}

TEST_CASE(
	"Missing optional query coverage does not invalidate stable Present windows",
	"[performance-tuning][stability][coverage]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 8.0, 5.0, 1);
	const auto currentAfter =
		MakeConstantWindow(16.0, std::nullopt, 5.0, 1000);

	REQUIRE(
		PerformanceTuning::AreCurrentWindowsEquivalent(
			currentBefore,
			currentAfter));
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
