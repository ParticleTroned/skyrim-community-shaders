#include "Diagnostics/FrameGenerationDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "Globals.h"

#	include <DevBenchAPI.h>

#	include <atomic>
#	include <chrono>
#	include <cmath>
#	include <format>
#	include <functional>
#	include <future>
#	include <memory>
#	include <optional>
#	include <string>
#	include <thread>

namespace
{
	using json = nlohmann::json;
	using namespace std::chrono_literals;

	constexpr auto kMainThreadTimeout = 5s;
	constexpr uint32_t kMinimumMeasurementMs = 500;
	constexpr uint32_t kMaximumMeasurementMs = 30000;
	constexpr std::string_view kDevBenchRepository =
		"ParticleTroned/devbench";
	constexpr std::string_view kDevBenchCommit =
		"4b8e6f05ab6cd30f545f203eccf557c51f1b8499";
	constexpr std::string_view kDevBenchVersion = "1.15.2";
	constexpr std::string_view kAutomationRepository =
		"ParticleTroned/skyrim-vr-automation";
	constexpr std::string_view kAutomationCommit =
		"0b283b65c6404f41c5da839d5c29da6803d7b443";
	constexpr std::string_view kAutomationProtocol = "2026-08-26";

	std::atomic_bool g_registered{ false };
	std::atomic_uint g_hostBuild{ 0 };

	enum class MainThreadDispatchState
	{
		kPending,
		kRunning,
		kCancelled,
	};

	std::string StringField(const json& a_value, std::string_view a_name)
	{
		if (!a_value.is_object() || !a_value.contains(a_name) ||
			!a_value[a_name].is_string()) {
			return {};
		}
		return a_value[a_name].get<std::string>();
	}

	json RequestIdentity(const json& a_args)
	{
		return {
			{ "clientId", StringField(a_args, "clientId") },
			{ "commandId", StringField(a_args, "commandId") },
			{ "action", StringField(a_args, "action") },
		};
	}

	json Success(const json& a_args, json a_result)
	{
		json output{
			{ "ok", true },
			{ "contract", { { "name", "communityshaders.frame_generation" }, { "major", 1 }, { "minor", 0 } } },
			{ "request", RequestIdentity(a_args) },
			{ "result", std::move(a_result) },
		};
		BuildProvenance::AttachProducer(output);
		return output;
	}

	json Failure(
		const json& a_args,
		std::string_view a_code,
		std::string_view a_message,
		bool a_retryable = false)
	{
		json output{
			{ "ok", false },
			{ "contract", { { "name", "communityshaders.frame_generation" }, { "major", 1 }, { "minor", 0 } } },
			{ "request", RequestIdentity(a_args) },
			{ "error", { { "code", a_code }, { "message", a_message }, { "retryable", a_retryable } } },
		};
		BuildProvenance::AttachProducer(output);
		return output;
	}

	json MainThreadError(
		std::string_view a_code,
		std::string_view a_message,
		bool a_retryable)
	{
		return {
			{ "dispatchError",
				{
					{ "code", a_code },
					{ "message", a_message },
					{ "retryable", a_retryable },
				} },
		};
	}

	std::optional<json> TranslateMainThreadFailure(
		const json& a_args,
		const json& a_result)
	{
		if (!a_result.contains("dispatchError") ||
			!a_result["dispatchError"].is_object()) {
			return std::nullopt;
		}

		const auto& error = a_result["dispatchError"];
		return Failure(
			a_args,
			error.value("code", std::string("main_thread_failure")),
			error.value("message", std::string("main-thread operation failed")),
			error.value("retryable", false));
	}

	json OnMainThread(std::function<json()> a_operation)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return MainThreadError(
				"main_thread_unavailable",
				"SKSE task interface is unavailable",
				true);
		}

		auto promise = std::make_shared<std::promise<json>>();
		auto state = std::make_shared<std::atomic<MainThreadDispatchState>>(
			MainThreadDispatchState::kPending);
		auto future = promise->get_future();
		tasks->AddTask(
			[promise, state, operation = std::move(a_operation)]() mutable {
				auto expected = MainThreadDispatchState::kPending;
				if (!state->compare_exchange_strong(
						expected,
						MainThreadDispatchState::kRunning,
						std::memory_order_acq_rel,
						std::memory_order_acquire)) {
					return;
				}
				try {
					promise->set_value(operation());
				} catch (const std::exception& exception) {
					promise->set_value(MainThreadError(
						"main_thread_exception", exception.what(), false));
				} catch (...) {
					promise->set_value(MainThreadError(
						"main_thread_exception",
						"unknown main-thread failure",
						false));
				}
			});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			auto expected = MainThreadDispatchState::kPending;
			if (state->compare_exchange_strong(
					expected,
					MainThreadDispatchState::kCancelled,
					std::memory_order_acq_rel,
					std::memory_order_acquire)) {
				return MainThreadError(
					"main_thread_timeout",
					"main thread did not run within 5000 ms",
					true);
			}

			// A running mutation must finish before its result is reported.
			future.wait();
		}
		return future.get();
	}

	std::string_view BackendName(DX12SwapChain::FrameGenerationBackend a_backend)
	{
		switch (a_backend) {
		case DX12SwapChain::FrameGenerationBackend::kDLSSG:
			return "nvidia_dlss_g";
		case DX12SwapChain::FrameGenerationBackend::kFidelityFX:
			return "amd_fsr_frame_generation";
		default:
			return "none";
		}
	}

	json RuntimeStatus()
	{
		const auto& upscaling = globals::features::upscaling;
		const auto& swapChain = Upscaling::dx12SwapChain;
		const auto& streamline = Upscaling::streamlineDX12;
		const auto& dlssg = streamline.dlssgState;
		const auto timing = upscaling.GetOutputPresentationTiming();
		const auto telemetry = swapChain.GetPresentationTelemetry();

		return {
			{ "sources",
				{
					{ "devbench", { { "repository", kDevBenchRepository }, { "commit", kDevBenchCommit }, { "version", kDevBenchVersion } } },
					{ "automation", { { "repository", kAutomationRepository }, { "commit", kAutomationCommit }, { "protocol", kAutomationProtocol } } },
				} },
			{ "devbenchHostBuild", g_hostBuild.load(std::memory_order_acquire) },
			{ "backend", BackendName(swapChain.frameGenerationBackend) },
			{ "dx12PathActive", upscaling.IsFrameGenerationDx12PathActive() },
			{ "streamlineProxyGraphActive", swapChain.streamlineProxyGraphActive },
			{ "interopUsable", swapChain.IsInteropUsable() },
			{ "interopFailure", std::format("0x{:08X}", static_cast<unsigned>(swapChain.presentInteropFailure)) },
			{ "interopGeneration", swapChain.interopGeneration.load(std::memory_order_acquire) },
			{ "renderingGameFrames", upscaling.IsRenderingGameFrames() },
			{ "generationRequestedThisFrame", upscaling.ShouldUseFrameGenerationThisFrame() },
			{ "frameGeneration",
				{
					{ "configured", upscaling.IsFrameGenerationConfigured() },
					{ "active", upscaling.IsFrameGenerationActive() },
					{ "forceEnabled", upscaling.settings.frameGenerationForceEnable != 0 },
					{ "requestedMultiplier", upscaling.settings.dlssgFramesToGenerate + 1u },
					{ "maximumMultiplier", dlssg.maximumFramesToGenerate == 0 ? 0u : dlssg.maximumFramesToGenerate + 1u },
					{ "sdkFramesActuallyPresented", dlssg.framesActuallyPresented },
					{ "sdkStatus", static_cast<unsigned>(dlssg.status) },
					{ "optionsApplied", dlssg.optionsApplied },
					{ "optionsEnabled", dlssg.optionsEnabled },
					{ "optionsTransitionPending", dlssg.optionsTransitionPending },
					{ "disablePending", dlssg.disablePending },
					{ "consecutiveMissingMarkerStartFrames", dlssg.consecutiveMissingMarkerStartFrames },
					{ "consecutiveNoGeneratedPresents", dlssg.consecutiveNoGeneratedPresents },
				} },
			{ "latestOutputTiming",
				{
					{ "valid", timing.valid },
					{ "fps", timing.fps },
					{ "averageFrameTimeMs", timing.averageFrameTimeMs },
					{ "presentedFrameCount", timing.presentedFrameCount },
					{ "sampledDurationMs", timing.sampledDurationMs },
					{ "sampleId", timing.sampleId },
					{ "discontinuityEpoch", timing.discontinuityEpoch },
				} },
			{ "counters",
				{
					{ "acceptedGamePresents", telemetry.acceptedGamePresentCount },
					{ "outputPresents", telemetry.outputPresentedFrameCount },
					{ "outputSampledDurationMs", telemetry.outputSampledDurationMs },
					{ "outputValid", telemetry.outputValid },
					{ "sampleId", telemetry.sampleId },
					{ "discontinuityEpoch", telemetry.discontinuityEpoch },
				} },
		};
	}

	json ConfigurationIdentity(const json& a_status)
	{
		const auto& frameGeneration = a_status.at("frameGeneration");
		return {
			{ "backend", a_status.at("backend") },
			{ "dx12PathActive", a_status.at("dx12PathActive") },
			{ "streamlineProxyGraphActive", a_status.at("streamlineProxyGraphActive") },
			{ "interopGeneration", a_status.at("interopGeneration") },
			{ "configured", frameGeneration.at("configured") },
			{ "requestedMultiplier", frameGeneration.at("requestedMultiplier") },
		};
	}

	json SetEnabled(const json& a_args)
	{
		if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean())
			return Failure(a_args, "invalid_enabled", "enabled must be a boolean");

		const bool enabled = a_args["enabled"].get<bool>();
		uint32_t multiplier = 0;
		if (a_args.contains("frameMultiplier")) {
			if (!a_args["frameMultiplier"].is_number_unsigned())
				return Failure(a_args, "invalid_multiplier", "frameMultiplier must be an unsigned integer");
			multiplier = a_args["frameMultiplier"].get<uint32_t>();
		}

		auto result = OnMainThread([enabled, multiplier] {
			auto& upscaling = globals::features::upscaling;
			auto& swapChain = Upscaling::dx12SwapChain;
			auto& dlssg = Upscaling::streamlineDX12.dlssgState;
			const uint32_t previousMultiplier =
				upscaling.settings.dlssgFramesToGenerate + 1u;
			uint32_t appliedMultiplier = previousMultiplier;
			if (enabled &&
				(!upscaling.IsFrameGenerationDx12PathActive() ||
					swapChain.frameGenerationBackend != DX12SwapChain::FrameGenerationBackend::kDLSSG ||
					!swapChain.streamlineProxyGraphActive)) {
				return json{
					{ "mutationError", "restart_required" },
					{ "message", "DLSS-G was not resident at boot; enable Frame Generation, save, and restart before runtime diagnostics" },
				};
			}
			if (enabled) {
				const uint32_t maximumMultiplier =
					dlssg.maximumFramesToGenerate == 0 ?
						0u :
						dlssg.maximumFramesToGenerate + 1u;
				const uint32_t requestedMultiplier =
					multiplier == 0 ?
						upscaling.settings.dlssgFramesToGenerate + 1u :
						multiplier;
				if (maximumMultiplier < 2u || requestedMultiplier < 2u ||
					requestedMultiplier > maximumMultiplier) {
					return json{
						{ "mutationError", "unsupported_multiplier" },
						{ "message", "frameMultiplier is outside the DLSS-G range reported by the SDK" },
						{ "requestedMultiplier", requestedMultiplier },
						{ "maximumMultiplier", maximumMultiplier },
					};
				}
				upscaling.settings.dlssgFramesToGenerate = requestedMultiplier - 1u;
				appliedMultiplier = requestedMultiplier;
			}
			const bool modeChanged =
				(upscaling.settings.frameGenerationMode != 0) != enabled;
			const bool multiplierChanged =
				enabled && appliedMultiplier != previousMultiplier;
			upscaling.settings.frameGenerationMode = enabled ? 1u : 0u;
			auto status = RuntimeStatus();
			status["changed"] = modeChanged || multiplierChanged;
			status["providerApplyPending"] = modeChanged || multiplierChanged;
			status["runtimeOnly"] = true;
			return status;
		});

		if (auto failure = TranslateMainThreadFailure(a_args, result))
			return std::move(*failure);
		if (result.contains("mutationError"))
			return Failure(a_args, result["mutationError"].get<std::string>(), result["message"].get<std::string>());
		return Success(a_args, std::move(result));
	}

	json Measure(const json& a_args)
	{
		if (a_args.contains("durationMs") &&
			!a_args["durationMs"].is_number_unsigned()) {
			return Failure(a_args, "invalid_duration", "durationMs must be an unsigned integer");
		}
		const uint32_t durationMs = a_args.value("durationMs", 3000u);
		if (durationMs < kMinimumMeasurementMs || durationMs > kMaximumMeasurementMs) {
			return Failure(
				a_args,
				"invalid_duration",
				"durationMs must be between 500 and 30000");
		}

		auto before = OnMainThread([] { return RuntimeStatus(); });
		if (auto failure = TranslateMainThreadFailure(a_args, before))
			return std::move(*failure);

		const auto started = std::chrono::steady_clock::now();
		const auto startTelemetry = Upscaling::dx12SwapChain.GetPresentationTelemetry();
		std::this_thread::sleep_until(started + std::chrono::milliseconds(durationMs));
		const auto endTelemetry = Upscaling::dx12SwapChain.GetPresentationTelemetry();
		const auto ended = std::chrono::steady_clock::now();

		auto after = OnMainThread([] { return RuntimeStatus(); });
		if (auto failure = TranslateMainThreadFailure(a_args, after))
			return std::move(*failure);

		const double actualDurationMs =
			std::chrono::duration<double, std::milli>(ended - started).count();
		const bool configurationStable =
			ConfigurationIdentity(before) == ConfigurationIdentity(after);
		const bool outputContinuous =
			startTelemetry.discontinuityEpoch == endTelemetry.discontinuityEpoch &&
			endTelemetry.outputPresentedFrameCount >= startTelemetry.outputPresentedFrameCount &&
			endTelemetry.outputSampledDurationMs >= startTelemetry.outputSampledDurationMs;
		const uint64_t gameFrames =
			endTelemetry.acceptedGamePresentCount - startTelemetry.acceptedGamePresentCount;
		const uint64_t outputFrames =
			outputContinuous ?
				endTelemetry.outputPresentedFrameCount - startTelemetry.outputPresentedFrameCount :
				0;
		const double outputDurationMs =
			outputContinuous ?
				endTelemetry.outputSampledDurationMs - startTelemetry.outputSampledDurationMs :
				0.0;
		const double gameFps =
			actualDurationMs > 0.0 ? 1000.0 * static_cast<double>(gameFrames) / actualDurationMs : 0.0;
		const double outputFps =
			outputDurationMs > 0.0 ? 1000.0 * static_cast<double>(outputFrames) / outputDurationMs : 0.0;
		const bool valid = configurationStable && outputContinuous &&
		                   gameFrames > 0 && outputFrames > 0 &&
		                   outputDurationMs > 0.0 &&
		                   std::isfinite(gameFps) && std::isfinite(outputFps);

		json result{
			{ "valid", valid },
			{ "durationMs", actualDurationMs },
			{ "configurationStable", configurationStable },
			{ "outputContinuous", outputContinuous },
			{ "preFgGameFps", gameFps },
			{ "gameFrames", gameFrames },
			{ "outputFps", outputFps },
			{ "outputFrames", outputFrames },
			{ "outputSampledDurationMs", outputDurationMs },
			{ "measuredOutputToGameRatio", gameFps > 0.0 ? outputFps / gameFps : 0.0 },
			{ "before", std::move(before) },
			{ "after", std::move(after) },
		};
		if (!valid) {
			result["invalidReason"] =
				!configurationStable ?
					"frame-generation configuration changed during measurement" :
				!outputContinuous ?
					"DXGI presentation statistics reset during measurement" :
					"measurement did not observe both game and output presents";
		}
		return Success(a_args, std::move(result));
	}

	json Dispatch(const json& a_args)
	{
		if (!a_args.is_object())
			return Failure(json::object(), "invalid_request", "arguments must be a JSON object");
		for (const auto* field : { "clientId", "commandId", "action" }) {
			if (!a_args.contains(field) || !a_args[field].is_string() ||
				a_args[field].get_ref<const std::string&>().empty()) {
				return Failure(a_args, "invalid_request", std::format("{} must be a non-empty string", field));
			}
		}

		const auto action = a_args["action"].get<std::string>();
		if (action == "status") {
			auto result = OnMainThread([] { return RuntimeStatus(); });
			if (auto failure = TranslateMainThreadFailure(a_args, result))
				return std::move(*failure);
			return Success(a_args, std::move(result));
		}
		if (action != "set_enabled" && action != "measure")
			return Failure(a_args, "unknown_action", "action must be status, set_enabled, or measure");
		if (a_args.contains("expectedBuildId")) {
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(a_args)) {
				return Failure(
					a_args,
					mismatch->value("code", std::string("producer_mismatch")),
					mismatch->value("error", std::string("loaded build mismatch")));
			}
		}
		return action == "set_enabled" ? SetEnabled(a_args) : Measure(a_args);
	}

	void ToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			const auto args =
				a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			output = Dispatch(args);
		} catch (const std::exception& exception) {
			output = Failure(json::object(), "invalid_request", exception.what());
		} catch (...) {
			output = Failure(json::object(), "internal_error", "unknown diagnostic handler error", true);
		}

		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::Diagnostics::FrameGenerationDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("FrameGenerationDevBenchBridge: DevBench host is not present yet");
			return;
		}

		static constexpr const char* descriptor =
			R"({"description":"Inspect and safely toggle the resident CSX NVIDIA DLSS-G graph, or collect bounded independent game-present and DXGI output-present FPS measurements. Runtime enable requires DLSS-G to have been enabled at boot. Measurement starts immediately without scene, menu, or build-provenance admission checks and never infers generated FPS.","inputSchema":{"type":"object","required":["clientId","commandId","action"],"properties":{"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"action":{"type":"string","enum":["status","set_enabled","measure"]},"expectedBuildId":{"type":"string","description":"Optional strict provenance assertion; omit for normal diagnostics."},"enabled":{"type":"boolean"},"frameMultiplier":{"type":"integer","minimum":2,"maximum":6},"durationMs":{"type":"integer","minimum":500,"maximum":30000}}},"readOnly":false})";
		const bool inserted = devBench->RegisterTool(
			"communityshaders.frame_generation",
			descriptor,
			&ToolHandler,
			nullptr);
		g_hostBuild.store(devBench->GetBuildNumber(), std::memory_order_release);
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"FrameGenerationDevBenchBridge: {} tool with DevBench build {}",
			inserted ? "registered" : "replaced existing",
			devBench->GetBuildNumber());
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#else

namespace CSX::Diagnostics::FrameGenerationDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}

#endif
