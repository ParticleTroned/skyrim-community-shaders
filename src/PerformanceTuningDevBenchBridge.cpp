#include "PerformanceTuningDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Menu/PerformanceTuningRenderer.h"

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
		if (action != "status" && action != "start_quick_scan" && action != "start_feature_cost" &&
			action != "start_feature_costs" && action != "set_feature_enabled" &&
			action != "start_upscaling_sweep" && action != "cancel") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "start_quick_scan", "start_feature_cost", "start_feature_costs", "set_feature_enabled", "start_upscaling_sweep", "cancel" }) },
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
			const std::string serialized = output.dump();
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
			R"json({"description":"Run and inspect nonblocking SE/AE closed-menu performance measurements. Status is read-only and reports available feature capabilities, requested enabled state, effective measurement readiness, ownership, completed results and paged 100 ms traces with accepted-Present and whole-frame sample identities. Game frame intervals come from accepted QPC Present-to-Present samples; independent D3D11 whole-frame GPU and QPC render-to-Present CPU timings preserve delayed sample identity. Missing samples remain null. The SE on/off protocol is preserved: ten-second initial settling, five one-second current windows, at least ten-second comparison settling, five one-second comparison windows, and at least one second for exact restoration. Readiness and coverage gates may extend this nominal 31-second run; comparisons use duration-weighted block results, practical floors and repeatability diagnostics. Frame generation is temporarily disabled for on/off tests and its exact settings are restored afterward. Frame-generation GPU/CPU costs are not inferred. start_quick_scan leaves feature and frame-generation settings unchanged, requires five uninterrupted ready seconds, captures 300 named CPU/GPU frames with a thirty-second deadline, and restores profiler/menu ownership. Its self-time means are instrumentation coverage, not on/off savings. Invalid queries, capture-capacity loss, interrupted ownership, scene changes or settings changes invalidate the scan. start_feature_cost measures one surfaced feature against Off, or None for Upscaling. start_feature_costs runs eligible enabled features sequentially, restores each one before advancing, and uses the existing ten-second inter-feature cooldown. set_feature_enabled retains the enabled configuration across off/on while preserving later user edits. Runtime toggles are blocked during measurements and by scene/native ownership. Cancellation restores exact feature and frame-generation settings before releasing ownership; failed restoration remains pending and blocks new configuration mutations. start_upscaling_sweep measures TAA plus seven quality levels for each supported provider using the same SE protocol and mono engine-resolution scaling. NVIDIA measures DLSS with a caller-selected J/K/L/M/F/E preset and FSR3; omitted presets request a choice before mutation. AMD measures automatic FSR and FSR3, and rejects provider fallback as a result for the requested runtime. Original upscaling and frame-generation settings are restored after the sweep. Every response identifies the producing DLL; expectedBuildId rejects stale binaries.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["status","start_quick_scan","start_feature_cost","start_feature_costs","set_feature_enabled","start_upscaling_sweep","cancel"],"default":"status"},"featureShortName":{"type":"string","minLength":1,"description":"Feature short name from status.availableFeatureCosts; required by start_feature_cost and set_feature_enabled."},"matrix":{"type":"string","enum":["auto","nvidia","amd"],"default":"auto","description":"Hardware matrix. auto selects from the active adapter and runtime capabilities."},"dlssPreset":{"type":"string","enum":["J","K","L","M","F","E"],"description":"Required to start the NVIDIA matrix; omitted starts return a prompt without changing runtime state."},"traceAfterSequence":{"type":"integer","minimum":0,"description":"Return trace samples after this sequence. Each row carries presentIntervalSampleId, wholeFrameSampleId and wholeFramePresentIntervalSampleId plus timing discontinuity and skipped-capture diagnostics."},"maximumTraceSamples":{"type":"integer","minimum":1,"maximum":512,"default":128,"description":"Maximum raw timing samples returned by status."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."},"enabled":{"type":"boolean","description":"Required by set_feature_enabled. True enables the feature; false disables it in game."}}}})json";
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
