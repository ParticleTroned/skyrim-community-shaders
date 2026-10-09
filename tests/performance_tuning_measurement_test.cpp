#include "Menu/PerformanceTuningController.h"
#include "Menu/PerformanceTuningStatistics.h"
#include "Utils/FlatFrameTiming.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace RE
{
	using FormID = std::uint32_t;
}

struct Feature
{
	std::string shortName;
	bool ready = true;
	bool rejectRestore = false;
	double settleSeconds = 2.0;
	json settings{ { "enabled", true }, { "quality", 3 } };

	std::string GetShortName() { return shortName; }
	virtual double GetPerformanceCostMeasurementSettleSeconds(bool) const { return settleSeconds; }
	bool IsPerformanceCostMeasurementReady() const { return ready; }
	json CapturePerformanceCostMeasurementState() const { return settings; }
	void SetPerformanceCostMeasurementEnabled(bool enabled) { settings["enabled"] = enabled; }
	void RestorePerformanceCostMeasurementState(const json& original)
	{
		if (!rejectRestore)
			settings = original;
	}
};

double g_costMeasurementRestartAllowedTime = 0.0;
#include "performance_tuning_measurement_under_test.h"

namespace
{
	using Phase = FeatureCostMeasurementPhase;
	using SampleResult = FeatureCostSampleResult;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Near(double actual, double expected, const char* message)
	{
		Require(std::abs(actual - expected) < 1.0e-6, message);
	}

	ProfilingRenderer::PerformanceTimingSummary Timing(std::uint32_t frame, float frameMs = 100.0f)
	{
		ProfilingRenderer::PerformanceTimingSummary timing;
		timing.frameCount = frame;
		timing.frameSampleMs = frameMs;
		timing.valid = true;
		timing.hasFrameSample = true;
		timing.gameGpuSampleMs = 6.0f;
		timing.gameCpuSampleMs = 4.0f;
		timing.hasGameGpuSample = true;
		timing.hasGameCpuSample = true;
		return timing;
	}

	FeatureCostMeasurementState Begin(Feature& feature, double now = 0.0)
	{
		FeatureCostMeasurementState state;
		state.originalState = feature.settings;
		state.runStartTime = now;
		PrepareFeatureCostPhase(Phase::PreparingCurrent, state, now, kFeatureCostInitialWaitSeconds);
		return state;
	}

	void MeasureWindow(Feature& feature, FeatureCostMeasurementState& state,
		std::uint32_t& frame, double startTime)
	{
		for (std::uint32_t index = 1; index <= 30; ++index)
			UpdateFeatureCostMeasurement(&feature, state, Timing(++frame), startTime + index * 0.1);
	}

	void TestProtocol(const char* name, double comparisonWait, double totalSeconds)
	{
		Feature ordinary;
		Upscaling upscaling;
		Feature& feature = std::string_view(name) == "Upscaling" ? upscaling : ordinary;
		feature.shortName = name;
		auto state = Begin(feature);
		std::uint32_t frame = 1;
		Near(GetFeatureCostExpectedRunSeconds(&feature), totalSeconds, "nominal duration must match the protocol");
		Near(GetFeatureCostRemainingSeconds(state, &feature, 0.0), totalSeconds, "initial countdown must include every phase");

		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), 1.999);
		Require(state.phase == Phase::PreparingCurrent, "current measurement must wait two seconds");
		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), 2.0);
		Require(state.phase == Phase::MeasuringCurrent, "current measurement must begin at two seconds");
		Require(state.currentSample.sampledDurationMs == 0.0, "the settling frame must not enter the measurement");
		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), 2.0);
		Require(state.currentSample.sampledDurationMs == 0.0, "repeated frame must not enter the measurement");
		MeasureWindow(feature, state, frame, 2.0);
		Require(state.phase == Phase::PreparingTest, "current measurement must finish at five seconds");
		Near(state.currentSample.sampledDurationMs, 3000.0, "current condition must contain exactly three measured seconds");
		Require(!feature.settings["enabled"].get<bool>(), "comparison must disable the feature");
		Near(state.phaseDeadlineTime, 5.0 + comparisonWait, "comparison settling must use the feature protocol");
		Near(GetFeatureCostRemainingSeconds(state, &feature, 5.0), comparisonWait + 4.0, "comparison countdown must use the actual wait");

		const double comparisonStart = 5.0 + comparisonWait;
		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), comparisonStart - 0.001);
		Require(state.phase == Phase::PreparingTest, "comparison measurement must not start early");
		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), comparisonStart);
		Require(state.phase == Phase::MeasuringTest, "comparison must start after settling");
		Require(state.testSample.sampledDurationMs == 0.0, "comparison settling frame must be excluded");
		MeasureWindow(feature, state, frame, comparisonStart);
		Require(state.phase == Phase::Restoring, "comparison completion must restore before reporting");
		Near(state.testSample.sampledDurationMs, 3000.0, "comparison condition must contain exactly three measured seconds");
		Require(feature.settings == state.originalState, "restoration must preserve the exact original settings");
		Require(!state.delta.frame.available, "results must not appear before restoration verification");
		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), totalSeconds - 0.001);
		Require(state.phase == Phase::Restoring, "restoration must wait a full second");
		UpdateFeatureCostMeasurement(&feature, state, Timing(frame), totalSeconds);
		Require(state.phase == Phase::Complete, "run must complete at the requested duration");
		Require(state.delta.frame.available && state.delta.gameGpu.available && state.delta.gameCpu.available,
			"all complete metric windows must yield results");
		Require(state.failureMessage.empty(), "normal completion must succeed");
		for (const auto& block : state.currentSample.frame.blocks)
			Near(block.sum, 500.0, "each current block must contain 500 ms");
		for (const auto& block : state.testSample.frame.blocks)
			Near(block.sum, 500.0, "each comparison block must contain 500 ms");
		Near(GetFeatureCostRestartCooldownRemaining(totalSeconds), 5.0, "completion must start a five-second cooldown");
		using namespace PerformanceTuningController;
		Require(NextBatchAction(1, 2, false, true, false,
					GetFeatureCostRestartCooldownRemaining(totalSeconds + 4.999)) == BatchAction::Wait,
			"the next feature must wait the entire cooldown");
		Require(NextBatchAction(1, 2, false, true, false,
					GetFeatureCostRestartCooldownRemaining(totalSeconds + 5.0)) == BatchAction::StartNext,
			"the next feature must become eligible after five seconds");
		Near(GetFeatureCostRemainingSeconds(state, &feature, totalSeconds), 0.0, "completed run must have no measurement countdown");
	}

	void TestBlockBoundaries(bool flatTiming)
	{
		FeatureCostSample sample;
		auto timing = Timing(1);
		timing.flatTiming = flatTiming;
		timing.flatTimingEpoch = 7;
		const std::array frameTimes{ 17.0f, 33.0f, 731.0f, 19.0f, 41.0f };
		std::vector<Util::FlatFrameTiming::Sample> delayed;
		SampleResult result = SampleResult::Pending;
		std::size_t index = 0;
		while (result != SampleResult::Complete) {
			timing.frameCount = static_cast<std::uint32_t>(index + 1);
			timing.flatPresentId = index;
			timing.frameSampleMs = frameTimes[index % frameTimes.size()];
			result = AddFeatureCostSample(sample, timing);
			Require(result != SampleResult::Interrupted, "contiguous frames must not interrupt a measurement");
			delayed.push_back({ index + 1, timing.frameCount, 4.0f, 6.0f, true, true, true });
			++index;
		}
		Near(sample.sampledDurationMs, 3000.0, "crossing the last block must clamp to three seconds");
		Require(sample.frame.blocks.size() == 6, "measurement must contain six blocks");
		if (flatTiming) {
			timing.flatPresentId = index;
			timing.flatSamples = delayed;
			ResolveFlatFeatureCostSamples(sample, timing, true);
			Require(sample.pendingFlatSamples.empty(), "delayed query weights must all resolve");
		}
		for (std::size_t block = 0; block < sample.frame.blocks.size(); ++block) {
			Near(sample.frame.blocks[block].sum, 500.0, "variable frames must partition each block at 500 ms");
			Near(sample.gameGpu.blocks[block].sampleWeight, sample.frame.blocks[block].sampleWeight,
				"GPU samples must retain their original fractional block weights");
			Near(sample.gameCpu.blocks[block].sampleWeight, sample.frame.blocks[block].sampleWeight,
				"CPU samples must retain their original fractional block weights");
			Near(PerformanceTuningStatistics::GetMean(sample.gameGpu.blocks[block]), 6.0, "GPU block mean must use the real timing");
		}
		const auto duration = sample.sampledDurationMs;
		Require(AddFeatureCostSample(sample, timing) == SampleResult::Pending, "a duplicate completion frame must be ignored");
		Near(sample.sampledDurationMs, duration, "duplicate frame must not change accumulated time");
	}

	void TestReadinessAndInterruptions(const char* name, double comparisonWait)
	{
		Feature ordinary;
		Upscaling upscaling;
		Feature& feature = std::string_view(name) == "Upscaling" ? upscaling : ordinary;
		feature.shortName = name;
		feature.ready = false;
		auto state = Begin(feature);
		UpdateFeatureCostMeasurement(&feature, state, Timing(1), 2.0);
		Require(state.phase == Phase::PreparingCurrent, "readiness must gate current measurement after its deadline");
		feature.ready = true;
		UpdateFeatureCostMeasurement(&feature, state, Timing(1), 2.1);
		UpdateFeatureCostMeasurement(&feature, state, Timing(2), 2.2);
		Near(state.currentSample.sampledDurationMs, 100.0, "ready measurement must accept a fresh frame");
		UpdateFeatureCostMeasurement(&feature, state, Timing(4), 2.3);
		Require(state.phase == Phase::MeasuringCurrent && state.currentSample.sampledDurationMs == 0.0,
			"dropped frame must restart only its measurement window");
		Near(state.phaseStartTime, 2.3, "interrupted measurement must reset its countdown");
		Require(state.currentSample.lastFrameCount == 4, "interruption must rebase frame identity");
		feature.ready = false;
		UpdateFeatureCostMeasurement(&feature, state, Timing(5), 2.4);
		Require(state.phase == Phase::PreparingCurrent, "readiness loss must return to current settling");
		Near(state.phaseDeadlineTime, 4.4, "current retry must settle for two seconds");

		feature.ready = true;
		state.testStateApplied = true;
		feature.SetPerformanceCostMeasurementEnabled(false);
		BeginFeatureCostSampleWindow(state.testSample, Phase::MeasuringTest, state, Timing(5), 8.0);
		UpdateFeatureCostMeasurement(&feature, state, Timing(6), 8.1);
		feature.ready = false;
		UpdateFeatureCostMeasurement(&feature, state, Timing(7), 8.2);
		Require(state.phase == Phase::PreparingTest && state.testSample.sampledDurationMs == 0.0,
			"comparison readiness loss must discard only the comparison sample");
		Near(state.phaseDeadlineTime, 8.2 + comparisonWait, "comparison retry must keep the full Off or None wait");
		feature.ready = true;
		UpdateFeatureCostMeasurement(&feature, state, Timing(8), state.phaseDeadlineTime - 0.001);
		Require(state.phase == Phase::PreparingTest, "comparison retry must not under-settle");
		UpdateFeatureCostMeasurement(&feature, state, Timing(8), state.phaseDeadlineTime);
		Require(state.phase == Phase::MeasuringTest, "comparison retry must resume after its full wait");
		UpdateFeatureCostMeasurement(&feature, state, Timing(9), state.phaseStartTime + 0.1);
		auto invalid = Timing(10);
		invalid.hasFrameSample = false;
		UpdateFeatureCostMeasurement(&feature, state, invalid, state.phaseStartTime + 0.2);
		Require(state.phase == Phase::MeasuringTest && state.testSample.sampledDurationMs == 0.0,
			"invalid timing must restart only the comparison window");
	}

	void TestRestorationAndCancellation()
	{
		Feature feature;
		feature.shortName = "Upscaling";
		auto state = Begin(feature);
		ApplyFeatureCostMeasurementTestState(&feature, state);
		PrepareFeatureCostPhase(Phase::MeasuringTest, state, 5.0, 0.0);
		StopFeatureCostMeasurement(&feature, state, 6.0, "Cancelled");
		Require(feature.settings == state.originalState && state.phase == Phase::Restoring,
			"cancellation must restore exact settings before completion");
		feature.ready = false;
		UpdateFeatureCostMeasurement(&feature, state, Timing(1), 7.0);
		Require(state.phase == Phase::Restoring, "restoration readiness must remain enforced");
		feature.ready = true;
		UpdateFeatureCostMeasurement(&feature, state, Timing(1), 7.1);
		Require(state.phase == Phase::Complete && state.failureMessage == "Cancelled" && !state.delta.frame.available,
			"cancelled measurements must not publish results");
		Near(GetFeatureCostRestartCooldownRemaining(7.1), 5.0, "cancellation must also use the five-second cooldown");

		state = Begin(feature);
		ApplyFeatureCostMeasurementTestState(&feature, state);
		feature.rejectRestore = true;
		StopFeatureCostMeasurement(&feature, state, 8.0, "Stopped");
		UpdateFeatureCostMeasurement(&feature, state, Timing(2), 9.0);
		Require(state.phase == Phase::Complete && !state.delta.frame.available &&
					state.failureMessage.find("could not be restored") != std::string::npos,
			"failed exact restoration must reject results");
	}

	void TestDiagnosticStatus()
	{
		ResetFeatureCostTrace();
#ifdef DEVBENCH_BRIDGE_ENABLED
		Feature ordinary;
		Upscaling upscaling;
		for (const auto& [name, waitMs, totalMs] : {
				 std::tuple{ "TerrainBlending", 3000.0, 12000.0 },
				 std::tuple{ "Upscaling", 10000.0, 19000.0 } }) {
			Feature& feature = std::string_view(name) == "Upscaling" ? upscaling : ordinary;
			feature.shortName = name;
			auto state = Begin(feature, 100.0);
			const auto status = FeatureCostMeasurementStatusJson(name, state, &feature, 100.5);
			Near(status.at("comparisonWaitMs").get<double>(), waitMs, "active status must report the actual Off or None wait");
			Near(status.at("expectedRunMs").get<double>(), totalMs, "active status must report the actual feature duration");
			Near(status.at("estimatedRemainingMs").get<double>(), totalMs - 500.0, "active countdown must use the same protocol");
			Near(status.at("phaseElapsedMs").get<double>(), 500.0, "active status must retain phase elapsed time");
			Require(status.at("phase") == "initial_cooldown", "active status must retain phase identity");
		}
		auto timing = Timing(7);
		timing.flatTiming = true;
		timing.sampleFrameCount = 5;
		timing.samplePresentId = 23;
		RecordFeatureCostTrace(timing, 100.0, 100.0, 100.0, "measuring_current", "Upscaling");
		RecordFeatureCostTrace(timing, 100.05, 100.0, 100.0, "measuring_current", "Upscaling");
		Require(g_featureCostTrace.size() == 1, "DevBench trace must retain its bounded sampling interval");
		timing.hasGameCpuSample = false;
		RecordFeatureCostTrace(timing, 100.125, 100.0, 100.0, "measuring_current", "Upscaling");
		const auto& sample = g_featureCostTrace.back();
		Require(sample.flatTiming && sample.sampleFrameCount == 5 && sample.samplePresentId == 23,
			"DevBench trace must retain delayed CPU/GPU source identity");
		Require(sample.hasFrame && sample.hasGameGpu && !sample.hasGameCpu && sample.gameCpuMs == 0.0f,
			"DevBench trace must retain missing metric semantics");
		const auto lastSequence = sample.sequence;
		ResetFeatureCostTrace();
		Require(g_featureCostTrace.empty(), "diagnostic reset must release trace samples");
		RecordFeatureCostTrace(timing, 101.0, 101.0, 101.0, "preparing_current", "Upscaling");
		Require(g_featureCostTrace.size() == 1 && g_featureCostTrace.back().sequence > lastSequence,
			"diagnostic reset must preserve the monotonic trace cursor");
#endif
	}

	void TestMissingMetricsAndSettleOverrides()
	{
		FeatureCostSample current;
		FeatureCostSample comparison;
		for (std::uint32_t frame = 1; frame <= 30; ++frame) {
			auto timing = Timing(frame);
			timing.hasGameCpuSample = frame > 3;
			AddFeatureCostSample(current, timing);
			AddFeatureCostSample(comparison, Timing(frame));
		}
		FeatureCostMeasurementState state;
		state.currentSample = current;
		state.testSample = comparison;
		FinalizeFeatureCostMeasurement(state);
		Require(state.delta.frame.available && state.delta.fps.available && state.delta.gameGpu.available &&
					!state.delta.gameCpu.available,
			"missing CPU samples must not invalidate other complete rows");

		Feature feature;
		feature.shortName = "TerrainBlending";
		feature.settleSeconds = 4.0;
		Near(GetFeatureCostComparisonWaitSeconds(&feature), 4.0, "feature-specific minimum settling must remain respected");
		feature.settleSeconds = std::numeric_limits<double>::quiet_NaN();
		Near(GetFeatureCostComparisonWaitSeconds(&feature), 3.0, "invalid settle override must use the ordinary wait");
		Upscaling upscaling;
		Near(GetFeatureCostComparisonWaitSeconds(&upscaling), 10.0, "Upscaling must use its own None settling policy");
		Near(upscaling.GetPerformanceCostMeasurementSettleSeconds(true), 2.0, "enabled settling must retain the base feature policy");
		Near(GetFeatureCostComparisonWaitSeconds(nullptr), 3.0, "missing feature must use the ordinary wait");
	}
}

int main()
{
	try {
		TestProtocol("TerrainBlending", 3.0, 12.0);
		TestProtocol("Upscaling", 10.0, 19.0);
		TestBlockBoundaries(false);
		TestBlockBoundaries(true);
		TestReadinessAndInterruptions("TerrainBlending", 3.0);
		TestReadinessAndInterruptions("Upscaling", 10.0);
		TestRestorationAndCancellation();
		TestMissingMetricsAndSettleOverrides();
		TestDiagnosticStatus();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	std::cout << "Feature-cost measurement protocol tests passed\n";
}
