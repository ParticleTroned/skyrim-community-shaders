#include "PerformanceTuningDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchJson.h"

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Menu/PerformanceTuningRenderer.h"
#	include "Features/PerformanceOverlay/ABTesting/ABComparisonRunner.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <cstddef>
#	include <cstdint>
#	include <exception>
#	include <functional>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	constexpr std::size_t kDefaultTracePageSize = 128;
	constexpr std::size_t kMaximumTracePageSize = 512;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run));
	}

	json BuildResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "comparison_status")
			return ABComparisonRunner::Get().Status();
		if (action == "cancel" && ABComparisonRunner::Get().IsRunning())
			return ABComparisonRunner::Get().Submit({ { "action", "cancel_comparison" } });
		if (action == "capture_comparison_preset" || action == "clear_comparison_presets" ||
			action == "start_setting_comparison" || action == "start_preset_comparison" || action == "cancel_comparison")
			return ABComparisonRunner::Get().Submit(a_args);
		if (action != "status" && action != "start_quick_scan" && action != "start_feature_cost" &&
			action != "start_feature_costs" && action != "set_feature_enabled" &&
			action != "start_upscaling_sweep" && action != "cancel") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "start_quick_scan", "start_feature_cost", "start_feature_costs", "set_feature_enabled", "start_upscaling_sweep", "cancel", "comparison_status", "capture_comparison_preset", "clear_comparison_presets", "start_setting_comparison", "start_preset_comparison", "cancel_comparison" }) },
			};
		}

		if (a_args.contains("traceAfterSequence") && !a_args.at("traceAfterSequence").is_number_unsigned()) {
			return { { "error", "traceAfterSequence must be a non-negative integer" } };
		}
		if (a_args.contains("maximumTraceSamples") && !a_args.at("maximumTraceSamples").is_number_unsigned()) {
			return { { "error", "maximumTraceSamples must be an integer from 1 to 512" } };
		}

		const std::uint64_t traceAfterSequence = a_args.value("traceAfterSequence", std::uint64_t{ 0 });
		const std::size_t maximumTraceSamples = a_args.value(
			"maximumTraceSamples",
			kDefaultTracePageSize);
		if (maximumTraceSamples < 1 || maximumTraceSamples > kMaximumTracePageSize) {
			return { { "error", "maximumTraceSamples must be an integer from 1 to 512" } };
		}

		const std::string matrix = a_args.value("matrix", std::string("auto"));
		if (matrix != "auto" && matrix != "nvidia" && matrix != "amd") {
			return {
				{ "error", "matrix must be auto, nvidia, or amd" },
				{ "matrix", matrix },
			};
		}
		if (a_args.contains("dlssPreset") && !a_args.at("dlssPreset").is_string())
			return { { "error", "dlssPreset must be J, K, L, M, F, or E" } };
		const std::string dlssPreset = a_args.value("dlssPreset", std::string());
		if (a_args.contains("featureShortName") && !a_args.at("featureShortName").is_string())
			return { { "error", "featureShortName must be a string" } };
		const std::string featureShortName = a_args.value("featureShortName", std::string());
		if (action == "set_feature_enabled" &&
			(!a_args.contains("enabled") || !a_args.at("enabled").is_boolean()))
			return { { "error", "set_feature_enabled requires a boolean enabled value" } };
		const bool enabled = a_args.value("enabled", false);

		return RunOnMainThread([action,
								   matrix,
								   dlssPreset,
								   featureShortName,
								   enabled,
								   traceAfterSequence,
								   maximumTraceSamples]() -> json {
			if (action == "start_quick_scan")
				return PerformanceTuningRenderer::StartDevBenchQuickScan();
			if (action == "start_feature_cost")
				return PerformanceTuningRenderer::StartDevBenchFeatureCostMeasurement(featureShortName);
			if (action == "start_feature_costs")
				return PerformanceTuningRenderer::StartDevBenchFeatureCostBatch();
			if (action == "set_feature_enabled")
				return PerformanceTuningRenderer::SetDevBenchFeatureEnabled(featureShortName, enabled);
			if (action == "start_upscaling_sweep")
				return PerformanceTuningRenderer::StartDevBenchUpscalingCostSweep(matrix, dlssPreset);
			if (action == "cancel")
				return PerformanceTuningRenderer::CancelDevBenchMeasurements();
			return PerformanceTuningRenderer::GetDevBenchMeasurementStatus(
				traceAfterSequence,
				maximumTraceSamples);
		});
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = BuildResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		BuildProvenance::AttachProducer(output);
		try {
			const std::string serialized = CSX::Api::DumpForDevBench(output);
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}
}

namespace PerformanceTuningDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("PerformanceTuningDevBenchBridge: devbench host not present; tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"json({"description":"Reply and event strings replace invalid UTF-8 with U+FFFD; request validation remains strict. WeatherPicker, PerformanceOverlay, Screenshot, and UnifiedWater are excluded from Performance Tuning, including batches and set_feature_enabled. Their native feature controls remain available. Overview toggles report requested enabled state separately from measurementEnabled and readiness; toggleBlockReason explains native scene/weather ownership. comparisonDetails identifies the precise off-state, including partial feature groups. Batch comparisons skip inactive or controlled features and stop if the gameplay scene changes. Cancellation restores state before completion. Run and inspect nonblocking closed-menu performance measurements. status performs no setup mutation and reports surfaced feature-cost capabilities, completed results, active ownership, readiness, and a paged 100 ms Game/GPU/CPU trace. VR timingDefinitionVersion 2 uses completed unique compositor frames: frameMs is ClientFrameIntervalMs, gameGpuMs is TotalRenderGpuMs, and gameCpuMs is NewFrameReadyMs - NewPosesReadyMs + CompositorRenderCpuMs (excluding pose wait). metricDefinitions describes runtime, formulas and display statistics. VR live counters show means and peaks over one second; Current-Off results remain means with standard errors. VR traces expose compositorFrameIndex, compositorSystemTimeSeconds, timingEpoch, sceneGpuMs and poseWaitMs; no new GPU queries are used. SE/AE uses QPC and asynchronous D3D11 whole-frame queries with unchanged measurement phases. Flat D3D12 frame-generation CPU/GPU metrics are unavailable. start_quick_scan settles for five uninterrupted ready seconds, then captures 300 frames for all instrumented features together, with a thirty-second deadline. It changes no feature settings, preserves the original menu and profiler enabled state, and rejects concurrent A/B or bounded profiler captures. quickScan reports per-feature CPU/GPU self-time means in milliseconds, never on/off savings. Sparse scopes include preceding idle frames only when those frames are valid; shared-only, unobserved, and incomplete histories remain null. Shared shaders, uninstrumented work and other GPU queues are excluded, with coverage on each result. Disabled and scene-inapplicable features do not delay the scan. Cancellation, cell or settings changes, global shader enable/disable, shader definition or blocking changes, compilation, profiler capacity loss and invalid queries invalidate the scan; renderConfiguration records the observed shader controls; invalidGpuFrames and invalidCpuFrames distinguish resolved progress from usable data. start_feature_cost measures one surfaced feature relative to Off, or None for Upscaling, using its current settings and the same protocol as the UI: two-second initial settling, six 500 ms current windows, three-second Off settling (ten seconds for Upscaling None), six 500 ms comparison windows, and one-second exact restoration with verification. Nominal run times are twelve seconds, or nineteen for Upscaling; readiness waits and timing interruptions can extend them. status.timing reports measurementIntervalMs, measurementBlockCount, upscalingComparisonWaitMs, restoreWaitMs and postRunCooldownMs; timing.comparisonWaitMs is the default for ordinary features, while active measurement and availableFeatureCosts report the actual comparisonWaitMs and expectedRunMs for their feature. It accepts either an open CS menu, which is restored afterward, or an already closed menu, which remains closed. start_feature_costs measures every eligible enabled runtime feature sequentially with the same on/off protocol and five-second inter-feature cooldown, restoring each feature before continuing and reopening the original menu after the batch. set_feature_enabled changes the runtime toggle and preserves its enabled configuration across off/on; toggles are rejected during any measurement. status includes featureBatch progress and per-metric costPercent, current, comparison, and costPercentBasis. Timing percentages are (current - off) / current * 100; FPS loss is (off - current) / off * 100. Missing metrics remain unavailable. cancel cancels DevBench-owned quick scans, batches, single measurements, sweeps and captured-state comparisons. start_upscaling_sweep measures every target relative to None using the same two-second initial settling, six 500 ms target windows, ten-second None wait, six 500 ms None windows, one-second exact restoration with verification, and five-second inter-case cooldown. auto selects the NVIDIA matrix on a DLSS-capable NVIDIA system or the AMD matrix on a runtime-capable AMD system. NVIDIA requires a caller-selected DLSS profile (J, K, L, M, F, or E), applies it to every DLSS/DLAA case, and also measures FSR3. A start without dlssPreset returns promptRequired without changing runtime state. AMD measures FSR3 and automatic FSR runtime across Native AA plus every render-scale quality and fails closed instead of labeling a latched host fallback as runtime FSR. Every response identifies the exact producing DLL; expectedBuildId fails closed on a stale build. Captured-state A/B actions: capture_comparison_preset retains up to four named in-memory presets of eligible reversible runtime feature states; comparison_status lists their feature names and run status without mutation; a successful capture publishes capturedStates in lastRequest. start_setting_comparison compares the current state with one scalar at path from preset for featureShortName; start_preset_comparison compares the current state with that captured preset as one bundle. Values come only from captured runtime states, not arbitrary configuration patches. Upscaling and VR cross-feature controls use their dedicated tools. Scene-owned, constrained and restart-dependent changes are refused. The runner is available only in DevBench builds. Commands return queued/requestId; comparison_status.lastRequest reports render-thread acceptance or rejection. Runs alternate A-B-A, with repeats 1-5 (default 2), intervalSeconds 1-15 (default 4), settleSeconds 1-15 (default 5, increased for feature requirements), then restore the baseline. Complete bracketing, at least ten samples and 80 percent coverage per interval are required for a verdict. Missing metrics remain null. Uncertainty is a heuristic envelope, not a confidence interval. VR GPU uses CSX TotalRenderGpuMs including compositor work; flat uses whole-frame D3D11 timings. Original source-frame identity routes late results. Collection is bounded to 4096 samples per metric/interval. Settings are checked at 250 ms intervals and before each switch; detected settings, scene, timing-source or ownership changes stop the run; restoration preserves intervening edits. cancel_comparison restores owned settings; restoration waits safely through load activity, with a 30-second admission deadline and a 30-second readiness deadline. Failed restoration retains measurement ownership and cancel_comparison retries it; a replacement configuration supersedes restoration. clear_comparison_presets releases captured presets when idle. No settings files are written. Active profiling and setting transitions add unmeasured overhead.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["status","start_quick_scan","start_feature_cost","start_feature_costs","set_feature_enabled","start_upscaling_sweep","cancel","comparison_status","capture_comparison_preset","clear_comparison_presets","start_setting_comparison","start_preset_comparison","cancel_comparison"],"default":"status"},"featureShortName":{"type":"string","minLength":1,"description":"Eligible feature short name from status.availableFeatureCosts; required by start_feature_cost, set_feature_enabled and start_setting_comparison. WeatherPicker, PerformanceOverlay, Screenshot, and UnifiedWater are not supported.","not":{"enum":["WeatherPicker","PerformanceOverlay","Screenshot","UnifiedWater"]}},"matrix":{"type":"string","enum":["auto","nvidia","amd"],"default":"auto","description":"Hardware matrix. auto selects from the active adapter and runtime capabilities."},"dlssPreset":{"type":"string","enum":["J","K","L","M","F","E"],"description":"Required to start the NVIDIA matrix; omitted starts return a prompt without changing runtime state."},"traceAfterSequence":{"type":"integer","minimum":0,"description":"Return raw timing samples after this sequence. VR samples expose compositorFrameIndex, compositorSystemTimeSeconds and timingEpoch for their completed source, plus sceneGpuMs and poseWaitMs details. Flat samples also expose gpuCpuFrameCount and gpuCpuPresentId for delayed CPU/GPU source identity."},"maximumTraceSamples":{"type":"integer","minimum":1,"maximum":512,"default":128,"description":"Maximum raw timing samples returned by status."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."},"enabled":{"type":"boolean","description":"Required by set_feature_enabled. True enables the feature; false disables it in game."},"repeats":{"type":"integer","minimum":1,"maximum":5,"default":2},"intervalSeconds":{"type":"integer","minimum":1,"maximum":15,"default":4},"settleSeconds":{"type":"integer","minimum":1,"maximum":15,"default":5},"preset":{"type":"string","minLength":1,"maxLength":64},"path":{"type":"string","maxLength":256,"description":"JSON pointer within the captured reversible feature state; empty selects a scalar root."}}},"outputSchema":{"type":"object","description":"Invalid UTF-8 in output strings is replaced with U+FFFD; valid UTF-8 output is unchanged. Comparison commands return queued/requestId. comparison_status publishes lastRequest acceptance, phase, restoration, captured presets, interval source-frame ranges and counts, and frame/GPU/CPU baseline/change/delta/uncertainty in milliseconds; invalid statistics are null."}})json";
		devBench->RegisterTool(
			"communityshaders.performance_tuning",
			descriptor,
			&ToolHandler,
			nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"PerformanceTuningDevBenchBridge: registered communityshaders.performance_tuning with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace PerformanceTuningDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
