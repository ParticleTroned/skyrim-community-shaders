#include "Menu/PerformanceTuningMeasurement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
	using PerformanceTuning::AddSampleResult;
	using PerformanceTuning::MetricKind;
	using PerformanceTuning::MetricReliability;
	using PerformanceTuning::Moments;
	using PerformanceTuning::SampleWindow;
	using PerformanceTuning::TransitionGateResult;
	using PerformanceTuning::TransitionGateState;

	using BlockValues =
		std::array<double, PerformanceTuning::kMeasurementBlockCount>;

	SampleWindow MakeWindow(
		const BlockValues& presentIntervalsMs,
		double captureStartTimeSeconds,
		std::optional<double> wholeFrameGpuMs = 8.0,
		std::optional<double> wholeFrameCpuMs = 5.0,
		uint32_t omitEveryGpuSample = 0,
		uint32_t omitEveryCpuSample = 0,
		uint32_t omitInitialGpuSampleCount = 0,
		uint64_t firstSampleId = 1)
	{
		SampleWindow window;
		PerformanceTuning::BeginSampleWindow(
			window,
			firstSampleId - 1,
			firstSampleId - 1,
			captureStartTimeSeconds);

		bool complete = false;
		for (uint32_t sampleIndex = 1;
			 sampleIndex <= 10000;
			 ++sampleIndex) {
			const auto blockIndex = std::min(
				static_cast<std::size_t>(
					window.sampledDurationMs /
					PerformanceTuning::kMeasurementBlockDurationMs),
				PerformanceTuning::kMeasurementBlockCount - 1);
			const uint64_t sampleId =
				firstSampleId + sampleIndex - 1;
			const auto presentResult =
				PerformanceTuning::AddPresentSample(
					window,
					sampleId,
					presentIntervalsMs[blockIndex],
					false);
			REQUIRE(
				(presentResult == AddSampleResult::Added ||
				 presentResult == AddSampleResult::Complete));

			const bool omitGpu =
				sampleIndex <= omitInitialGpuSampleCount ||
				(omitEveryGpuSample != 0 &&
				 sampleIndex % omitEveryGpuSample == 0);
			const bool omitCpu =
				omitEveryCpuSample != 0 &&
				sampleIndex % omitEveryCpuSample == 0;
			const auto wholeFrameResult =
				PerformanceTuning::AddWholeFrameSample(
					window,
					sampleId,
					sampleId,
					omitGpu ? std::nullopt : wholeFrameGpuMs,
					omitCpu ? std::nullopt : wholeFrameCpuMs);
			REQUIRE(wholeFrameResult == AddSampleResult::Added);

			if (presentResult == AddSampleResult::Complete) {
				complete = true;
				break;
			}
		}

		REQUIRE(complete);
		return window;
	}

	SampleWindow MakeConstantWindow(
		double presentIntervalMs,
		double captureStartTimeSeconds,
		std::optional<double> wholeFrameGpuMs = 8.0,
		std::optional<double> wholeFrameCpuMs = 5.0,
		uint32_t omitEveryGpuSample = 0,
		uint32_t omitEveryCpuSample = 0,
		uint32_t omitInitialGpuSampleCount = 0,
		uint64_t firstSampleId = 1)
	{
		return MakeWindow(
			{ presentIntervalMs,
				presentIntervalMs,
				presentIntervalMs,
				presentIntervalMs },
			captureStartTimeSeconds,
			wholeFrameGpuMs,
			wholeFrameCpuMs,
			omitEveryGpuSample,
			omitEveryCpuSample,
			omitInitialGpuSampleCount,
			firstSampleId);
	}
}

TEST_CASE(
	"Transition gate waits for readiness, fresh Presents, and post-flush soak",
	"[performance-tuning][transition]")
{
	TransitionGateState state;
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, false, 0.0, 100, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE_FALSE(state.continuouslyReady);

	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 1.0, 100, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 3.0, 159, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE_FALSE(state.soakStarted);
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 3.0, 160, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE(state.soakStarted);
	REQUIRE(state.soakStartTime == Catch::Approx(3.0));
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 7.0, 201, 2.0, 60, 4.0) ==
		TransitionGateResult::Ready);
}

TEST_CASE(
	"Transition gate resets on readiness loss and Present rollback",
	"[performance-tuning][transition][reset]")
{
	TransitionGateState state;
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 10.0, 500, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 12.0, 499, 2.0, 60, 4.0) ==
		TransitionGateResult::TimingReset);
	REQUIRE_FALSE(state.continuouslyReady);

	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, true, 13.0, 600, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE(
		PerformanceTuning::UpdateTransitionGate(
			state, false, 14.0, 601, 2.0, 60, 4.0) ==
		TransitionGateResult::Pending);
	REQUIRE_FALSE(state.continuouslyReady);
}

TEST_CASE(
	"Moments reject unavailable values instead of turning them into zero",
	"[performance-tuning][moments][missing]")
{
	Moments moments;
	REQUIRE_FALSE(moments.Add(0.0, 1.0));
	REQUIRE_FALSE(moments.Add(-1.0, 1.0));
	REQUIRE_FALSE(
		moments.Add(std::numeric_limits<double>::quiet_NaN(), 1.0));
	REQUIRE_FALSE(
		moments.Add(1.0, std::numeric_limits<double>::infinity()));
	REQUIRE_FALSE(moments.Mean().has_value());

	REQUIRE(moments.Add(15.95, 1.0));
	REQUIRE(moments.Add(15.95, 0.25));
	REQUIRE(*moments.Mean() == Catch::Approx(15.95));
}

TEST_CASE(
	"A complete window contains four elapsed-time blocks and diagnostics",
	"[performance-tuning][blocks][diagnostics]")
{
	const auto window = MakeConstantWindow(10.0, 2.0);
	REQUIRE(window.complete);
	REQUIRE(window.sampledDurationMs == Catch::Approx(2000.0));
	REQUIRE(window.presentSampleCount == 200);
	REQUIRE(
		PerformanceTuning::GetWindowMidpointTimeSeconds(window) ==
		Catch::Approx(3.0));
	REQUIRE(PerformanceTuning::GetWindowFps(window) == Catch::Approx(100.0));

	for (std::size_t blockIndex = 0;
		 blockIndex < PerformanceTuning::kMeasurementBlockCount;
		 ++blockIndex) {
		REQUIRE(
			PerformanceTuning::GetBlockFps(window, blockIndex) ==
			Catch::Approx(100.0));
		REQUIRE(
			PerformanceTuning::GetMetricCoverage(
				window, blockIndex, MetricKind::Present)
				.Meets(1.0));
	}
}

TEST_CASE(
	"Elapsed-time target cannot complete before 24 real Presents",
	"[performance-tuning][minimum-samples][fps]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0, 0.0);

	for (uint64_t sampleId = 1; sampleId <= 20; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 100.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(window.sampledDurationMs == Catch::Approx(2000.0));
	REQUIRE_FALSE(window.complete);

	for (uint64_t sampleId = 21; sampleId <= 23; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 100.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 24, 100.0, false) ==
		AddSampleResult::Complete);

	REQUIRE(window.sampledDurationMs == Catch::Approx(2400.0));
	REQUIRE(window.presentSampleCount == 24);
	REQUIRE(window.blocks[3].sampledDurationMs == Catch::Approx(900.0));
	REQUIRE(PerformanceTuning::GetWindowFps(window) == Catch::Approx(10.0));
}

TEST_CASE(
	"The Profiler interruption cutoff rejects over one second only",
	"[performance-tuning][present][interruption]")
{
	SampleWindow accepted;
	PerformanceTuning::BeginSampleWindow(accepted, 0, 0, 0.0);
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			accepted, 1, 1000.0, false) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			accepted, 2, 1000.0, false) ==
		AddSampleResult::Added);
	REQUIRE_FALSE(accepted.complete);
	REQUIRE(accepted.presentSampleCount == 2);

	SampleWindow interrupted;
	PerformanceTuning::BeginSampleWindow(interrupted, 0, 0, 0.0);
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			interrupted, 1, 1000.001, false) ==
		AddSampleResult::IntervalTooLarge);
	REQUIRE(interrupted.presentSampleCount == 0);
	REQUIRE(interrupted.sampledDurationMs == Catch::Approx(0.0));
	REQUIRE(interrupted.presentSourceDiscontinuous);
}

TEST_CASE(
	"A valid sub-second hitch remains in the arithmetic window mean",
	"[performance-tuning][present][hitch]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0, 0.0);
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 1, 750.0, false) ==
		AddSampleResult::Added);
	REQUIRE_FALSE(window.complete);

	for (uint64_t sampleId = 2; sampleId <= 125; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 10.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 126, 10.0, false) ==
		AddSampleResult::Complete);

	REQUIRE(*window.present.Mean() == Catch::Approx(2000.0 / 126.0));
	REQUIRE(PerformanceTuning::GetWindowFps(window) == Catch::Approx(63.0));
}

TEST_CASE(
	"A completing hitch is retained in full and extends actual capture time",
	"[performance-tuning][present][hitch][boundary]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0, 5.0);

	for (uint64_t sampleId = 1; sampleId <= 199; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 10.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 200, 900.0, false) ==
		AddSampleResult::Complete);

	REQUIRE(window.sampledDurationMs == Catch::Approx(2890.0));
	REQUIRE(window.blocks[3].sampledDurationMs == Catch::Approx(1390.0));
	REQUIRE(*window.present.Mean() == Catch::Approx(14.45));
	REQUIRE(
		PerformanceTuning::GetWindowFps(window) ==
		Catch::Approx(200000.0 / 2890.0));
	REQUIRE(
		PerformanceTuning::GetWindowMidpointTimeSeconds(window) ==
		Catch::Approx(6.445));
}

TEST_CASE(
	"Zero whole-frame values remain unavailable while valid channels survive",
	"[performance-tuning][missing][zero]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0, 0.0);

	for (uint64_t sampleId = 1;; ++sampleId) {
		const auto presentResult =
			PerformanceTuning::AddPresentSample(
				window, sampleId, 10.0, false);
		const auto wholeFrameResult =
			PerformanceTuning::AddWholeFrameSample(
				window,
				sampleId,
				sampleId,
				sampleId == 1 ? std::optional<double>(0.0) :
				                std::nullopt,
				5.0);
		if (sampleId == 1)
			REQUIRE(wholeFrameResult == AddSampleResult::InvalidValue);
		else
			REQUIRE(wholeFrameResult == AddSampleResult::Added);
		if (presentResult == AddSampleResult::Complete)
			break;
	}

	REQUIRE_FALSE(window.wholeFrameGpu.Mean().has_value());
	REQUIRE(
		PerformanceTuning::GetWindowMeanMs(
			window, MetricKind::WholeFrameGpu) == std::nullopt);
	REQUIRE(
		*PerformanceTuning::GetWindowMeanMs(
			window, MetricKind::WholeFrameCpu) == Catch::Approx(5.0));
	REQUIRE_FALSE(
		PerformanceTuning::GetMetricCoverage(
			window, MetricKind::WholeFrameGpu)
			.continuous);
}

TEST_CASE(
	"Metric coverage reports count and elapsed-time coverage",
	"[performance-tuning][coverage]")
{
	const auto window = MakeConstantWindow(
		10.0,
		0.0,
		8.0,
		5.0,
		20);
	const auto gpuCoverage =
		PerformanceTuning::GetMetricCoverage(
			window, MetricKind::WholeFrameGpu);

	REQUIRE(gpuCoverage.expectedSampleCount == 200);
	REQUIRE(gpuCoverage.validSampleCount == 190);
	REQUIRE(gpuCoverage.expectedSampleWeight == Catch::Approx(200.0));
	REQUIRE(gpuCoverage.validSampleWeight == Catch::Approx(190.0));
	REQUIRE(gpuCoverage.sampleCoverage == Catch::Approx(0.95));
	REQUIRE(gpuCoverage.weightCoverage == Catch::Approx(0.95));
	REQUIRE(gpuCoverage.Meets(0.90));
	REQUIRE_FALSE(gpuCoverage.Meets(1.0));
	for (std::size_t blockIndex = 0;
		 blockIndex < PerformanceTuning::kMeasurementBlockCount;
		 ++blockIndex) {
		REQUIRE(
			PerformanceTuning::GetMetricCoverage(
				window, blockIndex, MetricKind::WholeFrameGpu)
				.Meets(0.90));
	}
}

TEST_CASE(
	"Default coverage accepts small loss and rejects materially missing data",
	"[performance-tuning][coverage][result]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 0.0, 8.0, 5.0);
	const auto comparison95 =
		MakeConstantWindow(14.0, 3.0, 6.0, 4.0, 20);
	const auto currentAfter = MakeConstantWindow(16.0, 6.0, 8.0, 5.0);
	const auto mostlyCovered = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison95,
		currentAfter);

	REQUIRE(mostlyCovered.wholeFrameGpu.IsAvailable());
	REQUIRE(
		*mostlyCovered.wholeFrameGpu.valueMs == Catch::Approx(2.0));
	REQUIRE(mostlyCovered.wholeFrameCpu.IsAvailable());

	const auto comparison80 =
		MakeConstantWindow(14.0, 3.0, 6.0, 4.0, 5);
	const auto insufficient = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison80,
		currentAfter);
	REQUIRE_FALSE(insufficient.wholeFrameGpu.IsAvailable());
	REQUIRE(
		insufficient.wholeFrameGpu.reliability ==
		MetricReliability::Unavailable);
	REQUIRE(insufficient.wholeFrameCpu.IsAvailable());
}

TEST_CASE(
	"A locally sparse block prevents a reliable label despite good total coverage",
	"[performance-tuning][coverage][blocks]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 0.0, 8.0, 5.0);
	const auto comparison = MakeConstantWindow(
		14.0,
		3.0,
		6.0,
		4.0,
		0,
		0,
		6);
	const auto currentAfter = MakeConstantWindow(16.0, 6.0, 8.0, 5.0);

	const auto totalCoverage = PerformanceTuning::GetMetricCoverage(
		comparison,
		MetricKind::WholeFrameGpu);
	const auto firstBlockCoverage = PerformanceTuning::GetMetricCoverage(
		comparison,
		0,
		MetricKind::WholeFrameGpu);
	REQUIRE(totalCoverage.Meets(0.90));
	REQUIRE_FALSE(firstBlockCoverage.Meets(0.90));

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);
	REQUIRE(result.wholeFrameGpu.IsAvailable());
	REQUIRE(
		result.wholeFrameGpu.repeatability.availableBlockCount == 3);
	REQUIRE(
		result.wholeFrameGpu.reliability ==
		MetricReliability::InsufficientBlockCoverage);
}

TEST_CASE(
	"Asymmetric A-B-A timing interpolates the current state at B's midpoint",
	"[performance-tuning][drift][timing]")
{
	const auto currentBefore = MakeConstantWindow(10.0, 0.0);
	const auto comparison = MakeConstantWindow(12.0, 3.0);
	const auto currentAfter = MakeConstantWindow(20.0, 10.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.currentBeforeMeanMs == Catch::Approx(10.0));
	REQUIRE(result.present.comparisonMeanMs == Catch::Approx(12.0));
	REQUIRE(result.present.currentAfterMeanMs == Catch::Approx(20.0));
	const auto currentBeforeTime =
		PerformanceTuning::GetWindowMidpointTimeSeconds(currentBefore);
	const auto comparisonTime =
		PerformanceTuning::GetWindowMidpointTimeSeconds(comparison);
	const auto currentAfterTime =
		PerformanceTuning::GetWindowMidpointTimeSeconds(currentAfter);
	REQUIRE(currentBeforeTime);
	REQUIRE(comparisonTime);
	REQUIRE(currentAfterTime);
	const double expectedInterpolatedCurrent =
		10.0 + 10.0 *
			(*comparisonTime - *currentBeforeTime) /
			(*currentAfterTime - *currentBeforeTime);
	REQUIRE(
		result.present.interpolatedCurrentMeanMs ==
		Catch::Approx(expectedInterpolatedCurrent));
	REQUIRE(
		result.present.valueMs ==
		Catch::Approx(expectedInterpolatedCurrent - 12.0));
	REQUIRE(result.present.currentDriftMs == Catch::Approx(10.0));
	REQUIRE(
		result.present.reliability == MetricReliability::DriftDominated);
	for (std::size_t blockIndex = 0;
		 blockIndex < PerformanceTuning::kMeasurementBlockCount;
		 ++blockIndex) {
		const auto beforeBlockTime =
			PerformanceTuning::GetBlockMidpointTimeSeconds(
				currentBefore,
				blockIndex);
		const auto comparisonBlockTime =
			PerformanceTuning::GetBlockMidpointTimeSeconds(
				comparison,
				blockIndex);
		const auto afterBlockTime =
			PerformanceTuning::GetBlockMidpointTimeSeconds(
				currentAfter,
				blockIndex);
		REQUIRE(beforeBlockTime);
		REQUIRE(comparisonBlockTime);
		REQUIRE(afterBlockTime);
		const double expectedBlockDelta =
			10.0 + 10.0 *
				(*comparisonBlockTime - *beforeBlockTime) /
				(*afterBlockTime - *beforeBlockTime) -
			12.0;
		REQUIRE(
			result.present.repeatability.blockDeltas[blockIndex] ==
			Catch::Approx(expectedBlockDelta));
	}
}

TEST_CASE(
	"Four position-matched blocks produce a median range and 3-of-4 decision",
	"[performance-tuning][blocks][repeatability]")
{
	const auto currentBefore = MakeConstantWindow(50.0, 0.0);
	const auto comparison = MakeWindow(
		{ 10.0, 20.0, 25.0, 50.0 },
		3.0);
	const auto currentAfter = MakeConstantWindow(50.0, 6.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);
	const auto& band = result.present.repeatability;

	REQUIRE(result.present.reliability == MetricReliability::Reliable);
	REQUIRE(band.availableBlockCount == 4);
	REQUIRE(band.agreeingBlockCount == 3);
	REQUIRE(band.blockDeltas[0] == Catch::Approx(40.0));
	REQUIRE(band.blockDeltas[1] == Catch::Approx(30.0));
	REQUIRE(band.blockDeltas[2] == Catch::Approx(25.0));
	REQUIRE(band.blockDeltas[3] == Catch::Approx(0.0));
	REQUIRE(band.median == Catch::Approx(27.5));
	REQUIRE(band.minimum == Catch::Approx(0.0));
	REQUIRE(band.maximum == Catch::Approx(40.0));
}

TEST_CASE(
	"A constant-data micro-difference does not become false certainty",
	"[performance-tuning][practical-floor][constant]")
{
	const auto currentBefore = MakeConstantWindow(16.0, 0.0);
	const auto comparison = MakeConstantWindow(15.95, 3.0);
	const auto currentAfter = MakeConstantWindow(16.0, 6.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.valueMs == Catch::Approx(0.05));
	REQUIRE(result.present.practicalFloorMs == Catch::Approx(0.1595));
	REQUIRE(
		result.present.reliability ==
		MetricReliability::BelowPracticalFloor);
	REQUIRE_FALSE(result.present.IsReliable());
}

TEST_CASE(
	"Autocorrelated block reversals are not reported as reliable",
	"[performance-tuning][blocks][autocorrelation]")
{
	const auto currentBefore = MakeWindow(
		{ 20.0, 10.0, 20.0, 10.0 },
		0.0);
	const auto comparison = MakeWindow(
		{ 10.0, 20.0, 10.0, 16.0 },
		3.0);
	const auto currentAfter = MakeWindow(
		{ 20.0, 10.0, 20.0, 10.0 },
		6.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(result.present.valueMs);
	REQUIRE(*result.present.valueMs > *result.present.practicalFloorMs);
	REQUIRE(result.present.repeatability.agreeingBlockCount == 2);
	REQUIRE(
		result.present.reliability ==
		MetricReliability::MixedBlockDirections);
	REQUIRE_FALSE(result.present.IsReliable());
}

TEST_CASE(
	"FPS is secondary and uses real Presents over actual window duration",
	"[performance-tuning][fps][secondary]")
{
	const auto currentBefore = MakeConstantWindow(100.0, 0.0);
	const auto comparison = MakeConstantWindow(125.0, 4.0);
	const auto currentAfter = MakeConstantWindow(100.0, 10.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		currentBefore,
		comparison,
		currentAfter);

	REQUIRE(
		result.currentBeforeDiagnostics.sampledDurationMs ==
		Catch::Approx(2400.0));
	REQUIRE(
		result.comparisonDiagnostics.sampledDurationMs ==
		Catch::Approx(3000.0));
	REQUIRE(result.fps.currentBefore == Catch::Approx(10.0));
	REQUIRE(result.fps.comparison == Catch::Approx(8.0));
	REQUIRE(result.fps.currentAfter == Catch::Approx(10.0));
	REQUIRE(result.fps.interpolatedCurrent == Catch::Approx(10.0));
	REQUIRE(result.fps.value == Catch::Approx(2.0));
	REQUIRE(result.present.valueMs == Catch::Approx(-25.0));
}

TEST_CASE(
	"Identifier gaps and duplicate associations invalidate affected coverage",
	"[performance-tuning][source][association]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 100, 200, 0.0);

	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 101, 16.0, false) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 103, 16.0, false) ==
		AddSampleResult::SourceGap);
	REQUIRE(window.presentSourceDiscontinuous);
	REQUIRE(
		PerformanceTuning::AddPresentSample(window, 102, 16.0, false) ==
		AddSampleResult::SourceReset);

	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window, 201, 101, 8.0, 5.0) ==
		AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddWholeFrameSample(
			window, 202, 101, 8.0, 5.0) ==
		AddSampleResult::DuplicateAssociation);
	REQUIRE_FALSE(
		PerformanceTuning::GetMetricCoverage(
			window, MetricKind::WholeFrameGpu)
			.continuous);
}
