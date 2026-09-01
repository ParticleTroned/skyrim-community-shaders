#include "PerformanceTuningDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "BuildProvenance.h"
#	include "Menu/PerformanceTuningRenderer.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <cstddef>
#	include <cstdint>
#	include <functional>
#	include <future>
#	include <memory>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr std::size_t kDefaultTracePageSize = 128;
	constexpr std::size_t kMaximumTracePageSize = 512;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		tasks->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
		});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json BuildResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action != "status" && action != "start_feature_cost" &&
			action != "start_upscaling_sweep" && action != "cancel") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "start_feature_cost", "start_upscaling_sweep", "cancel" }) },
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

		return RunOnMainThread([action,
								   matrix,
								   dlssPreset,
								   featureShortName,
								   traceAfterSequence,
								   maximumTraceSamples]() -> json {
			if (action == "start_feature_cost")
				return PerformanceTuningRenderer::StartDevBenchFeatureCostMeasurement(featureShortName);
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
			R"json({"description":"Run and inspect nonblocking closed-menu performance measurements. status performs no setup mutation and reports surfaced feature-cost capabilities, completed results, active ownership, readiness, and a paged 100 ms Game/GPU/CPU trace. start_feature_cost measures one surfaced feature relative to Off, or None for Upscaling, using its current settings, exact restoration, and the same stabilized windows as the UI. It accepts either an open CS menu, which is restored afterward, or an already closed menu, which remains closed. start_upscaling_sweep measures every target relative to None using the same five-second initial cooldown, five one-second target windows, nine-second None wait, five one-second None windows, exact restoration, and ten-second inter-case cooldown. auto selects the NVIDIA matrix on a DLSS-capable NVIDIA system or the AMD matrix on an FSR4-capable AMD system. NVIDIA requires a caller-selected DLSS profile (J, K, L, M, F, or E), applies it to every DLSS/DLAA case, and also measures FSR3. A start without dlssPreset returns promptRequired without changing runtime state. AMD measures FSR3 and FSR4 across Native AA plus every render-scale quality and fails closed instead of labeling a latched provider fallback as FSR4. Every response identifies the exact producing DLL; expectedBuildId fails closed on a stale build.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["status","start_feature_cost","start_upscaling_sweep","cancel"],"default":"status"},"featureShortName":{"type":"string","minLength":1,"description":"Feature short name from status.availableFeatureCosts; required by start_feature_cost."},"matrix":{"type":"string","enum":["auto","nvidia","amd"],"default":"auto","description":"Hardware matrix. auto selects from the active adapter and runtime capabilities."},"dlssPreset":{"type":"string","enum":["J","K","L","M","F","E"],"description":"Required to start the NVIDIA matrix; omitted starts return a prompt without changing runtime state."},"traceAfterSequence":{"type":"integer","minimum":0,"description":"Return raw timing samples after this sequence."},"maximumTraceSamples":{"type":"integer","minimum":1,"maximum":512,"default":128,"description":"Maximum raw timing samples returned by status."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."}}}})json";
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
