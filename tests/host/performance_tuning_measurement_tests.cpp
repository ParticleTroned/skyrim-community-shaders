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

	using BlockValues =
		std::array<double, PerformanceTuning::kMeasurementBlockCount>;

	SampleWindow MakeWindow(
		const BlockValues& presentIntervalsMs,
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
			firstSampleId - 1);

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
		std::optional<double> wholeFrameGpuMs = 8.0,
		std::optional<double> wholeFrameCpuMs = 5.0,
		uint32_t omitEveryGpuSample = 0,
		uint32_t omitEveryCpuSample = 0,
		uint32_t omitInitialGpuSampleCount = 0,
		uint64_t firstSampleId = 1)
	{
		BlockValues presentIntervalsMs{};
		presentIntervalsMs.fill(presentIntervalMs);
		return MakeWindow(
			presentIntervalsMs,
			wholeFrameGpuMs,
			wholeFrameCpuMs,
			omitEveryGpuSample,
			omitEveryCpuSample,
			omitInitialGpuSampleCount,
			firstSampleId);
	}

	void AddConstantOutputCadence(
		SampleWindow& window,
		uint32_t framesPerSample,
		double sampleDurationMs = 100.0,
		uint64_t firstSampleId = 1,
		uint64_t discontinuityEpoch = 0)
	{
		for (uint64_t sampleId = firstSampleId;
			window.outputPresentDurationMs <
			PerformanceTuning::kMeasurementDurationMs;
			++sampleId) {
			const auto result =
				PerformanceTuning::AddOutputPresentSample(
					window,
					sampleId,
					discontinuityEpoch,
					sampleDurationMs,
					framesPerSample);
			REQUIRE(
				result == AddSampleResult::Added ||
				result == AddSampleResult::Complete);
		}
	}
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
	"A complete window contains five one-second samples and diagnostics",
	"[performance-tuning][blocks][diagnostics]")
{
	const auto window = MakeConstantWindow(10.0);
	REQUIRE(window.complete);
	REQUIRE(window.sampledDurationMs == Catch::Approx(5000.0));
	REQUIRE(window.presentSampleCount == 500);
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
	PerformanceTuning::BeginSampleWindow(window, 0, 0);

	for (uint64_t sampleId = 1; sampleId <= 24; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 100.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(window.sampledDurationMs == Catch::Approx(2400.0));
	REQUIRE_FALSE(window.complete);

	for (uint64_t sampleId = 25; sampleId <= 49; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 100.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 50, 100.0, false) ==
		AddSampleResult::Complete);

	REQUIRE(window.sampledDurationMs == Catch::Approx(5000.0));
	REQUIRE(window.presentSampleCount == 50);
	REQUIRE(window.blocks[4].sampledDurationMs == Catch::Approx(1000.0));
	REQUIRE(PerformanceTuning::GetWindowFps(window) == Catch::Approx(10.0));
}

TEST_CASE(
	"The Profiler interruption cutoff rejects over one second only",
	"[performance-tuning][present][interruption]")
{
	SampleWindow accepted;
	PerformanceTuning::BeginSampleWindow(accepted, 0, 0);
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
	PerformanceTuning::BeginSampleWindow(interrupted, 0, 0);
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
	PerformanceTuning::BeginSampleWindow(window, 0, 0);
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 1, 750.0, false) ==
		AddSampleResult::Added);
	REQUIRE_FALSE(window.complete);

	for (uint64_t sampleId = 2; sampleId <= 425; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 10.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 426, 10.0, false) ==
		AddSampleResult::Complete);

	REQUIRE(*window.present.Mean() == Catch::Approx(5000.0 / 426.0));
	REQUIRE(
		PerformanceTuning::GetWindowFps(window) ==
		Catch::Approx(426000.0 / 5000.0));
}

TEST_CASE(
	"A completing hitch is retained in full and extends actual capture time",
	"[performance-tuning][present][hitch][boundary]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0);

	for (uint64_t sampleId = 1; sampleId <= 499; ++sampleId) {
		REQUIRE(
			PerformanceTuning::AddPresentSample(
				window, sampleId, 10.0, false) ==
			AddSampleResult::Added);
	}
	REQUIRE(
		PerformanceTuning::AddPresentSample(
			window, 500, 900.0, false) ==
		AddSampleResult::Complete);

	REQUIRE(window.sampledDurationMs == Catch::Approx(5890.0));
	REQUIRE(window.blocks[4].sampledDurationMs == Catch::Approx(1890.0));
	REQUIRE(*window.present.Mean() == Catch::Approx(5890.0 / 500.0));
	REQUIRE(
		PerformanceTuning::GetWindowFps(window) ==
		Catch::Approx(500000.0 / 5890.0));
}

TEST_CASE(
	"Zero whole-frame values remain unavailable while valid channels survive",
	"[performance-tuning][missing][zero]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 0, 0);

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
		8.0,
		5.0,
		20);
	const auto gpuCoverage =
		PerformanceTuning::GetMetricCoverage(
			window, MetricKind::WholeFrameGpu);

	REQUIRE(gpuCoverage.expectedSampleCount == 500);
	REQUIRE(gpuCoverage.validSampleCount == 475);
	REQUIRE(gpuCoverage.expectedSampleWeight == Catch::Approx(500.0));
	REQUIRE(gpuCoverage.validSampleWeight == Catch::Approx(475.0));
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
	const auto current = MakeConstantWindow(16.0, 8.0, 5.0);
	const auto comparison95 =
		MakeConstantWindow(14.0, 6.0, 4.0, 20);
	const auto mostlyCovered = PerformanceTuning::CalculateCostResult(
		current,
		comparison95);

	REQUIRE(mostlyCovered.wholeFrameGpu.IsAvailable());
	REQUIRE(
		*mostlyCovered.wholeFrameGpu.valueMs == Catch::Approx(2.0));
	REQUIRE(mostlyCovered.wholeFrameCpu.IsAvailable());

	const auto comparison80 =
		MakeConstantWindow(14.0, 6.0, 4.0, 5);
	const auto insufficient = PerformanceTuning::CalculateCostResult(
		current,
		comparison80);
	REQUIRE_FALSE(insufficient.wholeFrameGpu.IsAvailable());
	REQUIRE(
		insufficient.wholeFrameGpu.reliability ==
		MetricReliability::InsufficientSampleCoverage);
	REQUIRE(insufficient.wholeFrameCpu.IsAvailable());
}

TEST_CASE(
	"A locally sparse block prevents a reliable label despite good total coverage",
	"[performance-tuning][coverage][blocks]")
{
	const auto current = MakeConstantWindow(16.0, 8.0, 5.0);
	const auto comparison = MakeConstantWindow(
		14.0,
		6.0,
		4.0,
		0,
		0,
		11);

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
		current,
		comparison);
	REQUIRE_FALSE(result.wholeFrameGpu.IsAvailable());
	REQUIRE(
		result.wholeFrameGpu.reliability ==
		MetricReliability::InsufficientSampleCoverage);
}

TEST_CASE(
	"Five one-second samples produce means, SE, and a Welch p-value",
	"[performance-tuning][statistics]")
{
	const auto current = MakeWindow({ 20.0, 20.0, 20.0, 20.0, 30.0 });
	const auto comparison = MakeConstantWindow(10.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);

	REQUIRE(result.present.currentMeanMs == Catch::Approx(22.0));
	REQUIRE(
		result.present.currentStandardErrorMs ==
		Catch::Approx(2.0));
	REQUIRE(result.present.comparisonMeanMs == Catch::Approx(10.0));
	REQUIRE(
		result.present.comparisonStandardErrorMs ==
		Catch::Approx(0.0));
	REQUIRE(result.present.valueMs == Catch::Approx(12.0));
	REQUIRE(result.present.standardErrorMs == Catch::Approx(2.0));
	REQUIRE(result.present.pValue);
	REQUIRE(
		*result.present.pValue ==
		Catch::Approx(0.003882537046961).margin(1e-12));
	REQUIRE(result.present.reliability == MetricReliability::Reliable);
}

TEST_CASE(
	"Constant samples with a material delta report zero sampling uncertainty",
	"[performance-tuning][statistics][constant]")
{
	const auto current = MakeConstantWindow(50.0);
	const auto comparison = MakeConstantWindow(25.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);

	REQUIRE(result.present.valueMs == Catch::Approx(25.0));
	REQUIRE(result.present.standardErrorMs == Catch::Approx(0.0));
	REQUIRE(result.present.pValue == Catch::Approx(0.0));
	REQUIRE(result.present.reliability == MetricReliability::Reliable);
}

TEST_CASE(
	"A constant-data micro-difference does not become false certainty",
	"[performance-tuning][practical-floor][constant]")
{
	const auto current = MakeConstantWindow(16.0);
	const auto comparison = MakeConstantWindow(15.95);

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);

	REQUIRE(result.present.valueMs == Catch::Approx(0.05));
	REQUIRE(result.present.practicalFloorMs == Catch::Approx(0.1595));
	REQUIRE(
		result.present.reliability ==
		MetricReliability::BelowPracticalFloor);
	REQUIRE_FALSE(result.present.IsReliable());
}

TEST_CASE(
	"Variable samples without significance are not reported as reliable",
	"[performance-tuning][statistics][significance]")
{
	const auto current = MakeWindow({ 20.0, 10.0, 20.0, 10.0, 20.0 });
	const auto comparison = MakeWindow(
		{ 10.0, 20.0, 10.0, 20.0, 10.0 });

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);

	REQUIRE(result.present.valueMs);
	REQUIRE(*result.present.valueMs > *result.present.practicalFloorMs);
	REQUIRE(result.present.pValue);
	REQUIRE(*result.present.pValue > 0.05);
	REQUIRE(
		result.present.reliability ==
		MetricReliability::NotStatisticallySignificant);
	REQUIRE_FALSE(result.present.IsReliable());
}

TEST_CASE(
	"FPS is secondary and uses real Presents over actual window duration",
	"[performance-tuning][fps][secondary]")
{
	const auto current = MakeConstantWindow(100.0);
	const auto comparison = MakeConstantWindow(125.0);

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);

	REQUIRE(
		result.currentDiagnostics.sampledDurationMs ==
		Catch::Approx(5000.0));
	REQUIRE(
		result.comparisonDiagnostics.sampledDurationMs ==
		Catch::Approx(5000.0));
	REQUIRE(result.fps.current == Catch::Approx(10.0));
	REQUIRE(result.fps.comparison == Catch::Approx(8.0));
	REQUIRE(result.fps.value == Catch::Approx(2.0));
	REQUIRE(result.fps.standardError == Catch::Approx(0.0));
	REQUIRE(result.fps.pValue == Catch::Approx(0.0));
	REQUIRE(result.present.valueMs == Catch::Approx(-25.0));
}

TEST_CASE(
	"Output FPS uses final presentation counts instead of game cadence",
	"[performance-tuning][fps][output]")
{
	auto current = MakeConstantWindow(20.0);
	auto comparison = MakeConstantWindow(20.0);
	AddConstantOutputCadence(current, 12);
	AddConstantOutputCadence(comparison, 6);

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);

	REQUIRE(result.fps.value == Catch::Approx(0.0));
	REQUIRE(result.outputFps.current == Catch::Approx(120.0));
	REQUIRE(result.outputFps.comparison == Catch::Approx(60.0));
	REQUIRE(result.outputFps.value == Catch::Approx(60.0));
	REQUIRE(result.outputFps.standardError == Catch::Approx(0.0));
	REQUIRE(result.outputFps.pValue == Catch::Approx(0.0));
}

TEST_CASE(
	"Discontinuous output statistics fail closed without invalidating game FPS",
	"[performance-tuning][fps][output][discontinuity]")
{
	auto current = MakeConstantWindow(20.0);
	auto comparison = MakeConstantWindow(20.0);
	AddConstantOutputCadence(current, 12);
	REQUIRE(
		PerformanceTuning::AddOutputPresentSample(
			comparison,
			1,
			0,
			100.0,
			6) == AddSampleResult::Added);
	REQUIRE(
		PerformanceTuning::AddOutputPresentSample(
			comparison,
			3,
			0,
			100.0,
			6) == AddSampleResult::SourceGap);

	const auto result = PerformanceTuning::CalculateCostResult(
		current,
		comparison);
	REQUIRE(result.fps.IsAvailable());
	REQUIRE_FALSE(result.outputFps.IsAvailable());
}

TEST_CASE(
	"Long output-statistic intervals are rejected instead of smeared across blocks",
	"[performance-tuning][fps][output][stale]")
{
	auto window = MakeConstantWindow(20.0);
	REQUIRE(
		PerformanceTuning::AddOutputPresentSample(
			window,
			1,
			0,
			1001.0,
			60) == AddSampleResult::IntervalTooLarge);
	REQUIRE(window.outputPresentSourceDiscontinuous);
	REQUIRE_FALSE(
		PerformanceTuning::GetBlockOutputFps(window, 0).has_value());
}

TEST_CASE(
	"Identifier gaps and duplicate associations invalidate affected coverage",
	"[performance-tuning][source][association]")
{
	SampleWindow window;
	PerformanceTuning::BeginSampleWindow(window, 100, 200);

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
