#include "MonoDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "MonoRenderer.h"
#	include "MonoSettingsSerialization.h"
#	include "MonoPrewarmPolicy.h"
#	include "Runtime.h"
#	include "ExposureCapture.h"
#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "Features/NeuralRenderingFeature.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "Menu/PerformanceTuningRenderer.h"
#	include "State.h"
#	include "Utils/CryptoHash.h"

#	include <DevBenchAPI.h>
#	include <atomic>
#	include <exception>
#	include <string>
#	include <stdexcept>

namespace NeuralRendering::MonoDevBenchBridge
{
	namespace
	{
		using Json = nlohmann::json;
		std::atomic_bool installed{ false };

		Json Configuration()
		{
			const auto& upscaling = globals::features::upscaling;
			const auto profile = upscaling.GetMonoConfiguredProfile();
			return { { "neural", Json(upscaling.neuralSettings) },
				{ "upscaling", { { "method", static_cast<std::uint32_t>(profile.method) }, { "qualityMode", profile.qualityMode },
					{ "dlssPreset", profile.dlssPreset }, { "fsrAutomatic", profile.fsrAutomatic } } } };
		}
		std::string Fingerprint(const Json& value) { return Util::CryptoHash::Sha256Hex(value.dump()); }

		Json Status()
		{
			const auto& upscaling = globals::features::upscaling;
			const auto configuration = Configuration();
			const auto renderer = MonoRenderer::Instance().Snapshot();
			auto& runtime = Runtime::Instance();
			const auto exposure = Color::ExposureCapture::Instance().GetStatus();
			const auto dimensions = upscaling.GetRuntimeResolutionPlan();
			const auto limits = runtime.GetTuningLimits(upscaling.neuralSettings.network);
			const auto* prewarmBlocker = MonoPrewarmConfigurationBlocker(upscaling.neuralSettings);
			return { { "schemaVersion", 1 }, { "viewCount", 1 },
				{ "requestedConfiguration", configuration }, { "configurationFingerprint", Fingerprint(configuration) },
				{ "eligible", upscaling.IsNeuralRenderingRequested() },
				{ "frameGenerationBlocked", upscaling.IsFrameGenerationConfigured() || upscaling.IsFrameGenerationDx12PathActive() },
				{ "saveLoadPaused", globals::state && globals::state->IsSaveLoadSafeModeActive() },
				{ "geometry", { { "engineWidth", dimensions.engineRenderSize.x }, { "engineHeight", dimensions.engineRenderSize.y },
					{ "outputWidth", dimensions.finalOutputSize.x }, { "outputHeight", dimensions.finalOutputSize.y },
					{ "modelWidth", renderer.modelWidth }, { "modelHeight", renderer.modelHeight }, { "modelResolutionPercent", renderer.effectivePercent } } },
				{ "renderer", { { "status", renderer.status }, { "detail", renderer.detail }, { "quarantined", renderer.quarantined },
					{ "evaluationFrame", renderer.evaluationFrame }, { "presentationFrame", renderer.presentationFrame },
					{ "evaluations", renderer.evaluations }, { "committed", renderer.presented }, { "historyResets", renderer.historyResets },
					{ "packedRegions", renderer.packedRegions }, { "packedCoverageProven", renderer.packedCoverageProven },
					{ "packedCoverageRejected", renderer.packedCoverageRejected }, { "compression", renderer.compressionStatus },
					{ "prewarm", renderer.prewarmStatus }, { "prewarms", renderer.prewarms },
					{ "prewarmConfigurationBlocker", prewarmBlocker ? Json(prewarmBlocker) : Json(nullptr) },
					{ "recoveryPending", renderer.recoveryPending }, { "stallAttempts", renderer.stallAttempts }, { "stallRecoveries", renderer.stallRecoveries } } },
				{ "runtime", { { "installed", runtime.IsInstalled() }, { "status", ToString(runtime.Status()) }, { "stage", ToString(runtime.FailureStage()) },
					{ "version", runtime.Version() }, { "sha256", runtime.Hash() }, { "detail", runtime.Detail() }, { "ngxResult", runtime.NgxResult() },
					{ "networkSelectionSupported", runtime.SupportsNetworkSelection() }, { "actualNetworkPreset", runtime.GetNetworkPreset(upscaling.neuralSettings.network) },
					{ "parameterCoreTrust", ToString(runtime.CoreTrust()) }, { "parameterCoreSource", ToString(runtime.CoreSource()) },
					{ "tuningLimits", { { "intensity", limits.intensity }, { "localTone", limits.localToneStrength },
						{ "localStructure", limits.localStructureStrength }, { "skinStructure", limits.skinStructureStrength } } } } },
				{ "exposure", { { "producers", exposure.producersRegistered }, { "captures", exposure.captures },
					{ "rejected", exposure.rejected }, { "readbacksDropped", exposure.dropped }, { "reason", exposure.lastReason } } },
				{ "diagnostics", MonoRenderer::Instance().Diagnostics() } };
		}

		Json Handle(const Json& args)
		{
			for (const auto& [key, value] : args.items()) {
				(void)value;
				if (key != "action" && key != "settings" && key != "persist" && key != "expectedConfigurationFingerprint" && key != "expectedBuildId")
					return { { "accepted", false }, { "error", "unknown parameter" }, { "parameter", key } };
			}
			const auto action = args.value("action", std::string("status"));
			if (action != "status" && action != "configure") return { { "accepted", false }, { "error", "action must be status or configure" } };
			if (action == "status" && (args.contains("settings") || args.contains("persist") || args.contains("expectedConfigurationFingerprint")))
				return { { "accepted", false }, { "error", "status does not accept mutation parameters" } };
			if (action == "configure" && (!args.contains("settings") || !args.contains("expectedConfigurationFingerprint") ||
				!args["expectedConfigurationFingerprint"].is_string() || (args.contains("persist") && !args["persist"].is_boolean())))
				return { { "accepted", false }, { "error", "configure requires settings and an exact configuration fingerprint; persist must be boolean" } };
			return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), [args, action]() -> Json {
				if (action == "status") return Status();
				if (!globals::state || !globals::menu || !globals::features::neuralRendering.loaded)
					return { { "accepted", false }, { "error", "neural settings are unavailable" } };
				auto& upscaling = globals::features::upscaling;
				if (globals::state->IsPersistentMutationBlocked() || PerformanceTuningRenderer::HasActiveMeasurements() || upscaling.IsMonoProfileMutationBlocked())
					return { { "accepted", false }, { "error", "settings mutation is blocked by a save/load, performance comparison, capture or backend retirement" } };
				if (args.at("expectedConfigurationFingerprint").get<std::string>() != Fingerprint(Configuration()))
					return { { "accepted", false }, { "error", "configuration changed since the inspected snapshot" } };
				auto candidate = upscaling.neuralSettings;
				if (const auto error = ApplyMonoSettingsPatch(args.at("settings"), candidate); !error.empty())
					return { { "accepted", false }, { "error", error } };
				if (!globals::menu->PrepareSettingsMutation()) return { { "accepted", false }, { "error", "could not preserve the settings baseline" } };
				const bool changed = candidate != upscaling.neuralSettings;
				upscaling.neuralSettings = candidate;
				if (changed) upscaling.RequestHistoryReset();
				const bool persist = args.value("persist", false);
				const bool saved = persist && globals::state->Save();
				auto result = Status();
				result["accepted"] = true; result["changed"] = changed; result["persistRequested"] = persist; result["saved"] = saved;
				result["appliedToAFrame"] = false;
				if (persist && !saved) result["error"] = "settings changed in memory but saving failed";
				return result;
			});
		}

		void Tool(void*, const char* input, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			Json output;
			try {
				const auto args = input && *input ? Json::parse(input) : Json::object();
				if (!args.is_object()) throw std::invalid_argument("arguments must be an object");
				if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) output = std::move(*mismatch);
				else output = Handle(args);
			} catch (const std::exception& error) { output = { { "accepted", false }, { "error", error.what() } }; }
			catch (...) { output = { { "accepted", false }, { "error", "unknown mono NR request error" } }; }
			try { BuildProvenance::AttachProducer(output); const auto encoded = output.dump(); write(sink, encoded.c_str()); }
			catch (...) { write(sink, R"({"error":"response serialization failed"})"); }
		}
	}

	void Install()
	{
		auto* api = DevBenchAPI::GetDevBenchInterface001();
		if (!api || installed.exchange(true)) return;
		static const auto descriptor = [] {
			Json properties = Json::object();
			const Json defaults = MonoSettings{};
			for (const auto& [name, value] : defaults.items()) {
				if (value.is_boolean()) properties[name] = { { "type", "boolean" } };
				else if (value.is_number_unsigned()) properties[name] = { { "type", "integer" }, { "minimum", 0 }, { "maximum", UINT32_MAX } };
				else if (value.is_number_float()) properties[name] = { { "type", "number" } };
				else if (value.is_array()) properties[name] = { { "type", "array" }, { "minItems", value.size() }, { "maxItems", value.size() }, { "items", { { "type", "number" } } } };
			}
			return Json{ { "description", "Single-view SE/AE neural settings and actual execution evidence. status reports exact requested configuration, renderer/provider state, source/model dimensions, native create/evaluate results and CPU durations, retirement fences, memory recovery, prewarm and exposure counters. GPU inference timing is separately recorded only during an active profiler capture. configure atomically validates a settings patch against the current configuration fingerprint and stages it for the next eligible render boundary. Save/load, active measurement and backend capture/retirement block changes. An accepted settings request is not proof of inference or presentation; inspect the reported source frames and committed evidence. Persistence occurs only when persist is true. This tool does not enable simultaneous NR/frame generation." },
				{ "inputSchema", { { "type", "object" }, { "additionalProperties", false }, { "properties", {
					{ "action", { { "type", "string" }, { "enum", { "status", "configure" } }, { "default", "status" } } },
					{ "settings", { { "type", "object" }, { "additionalProperties", false }, { "minProperties", 1 }, { "properties", properties } } },
					{ "expectedConfigurationFingerprint", { { "type", "string" } } },
					{ "persist", { { "type", "boolean" }, { "default", false } } }, { "expectedBuildId", { { "type", "string" } } } } } } } }.dump();
		}();
		api->RegisterTool("communityshaders.neural_rendering", descriptor.c_str(), &Tool, nullptr);
		logger::info("Registered mono neural rendering controls and diagnostics");
	}
}

#else
void NeuralRendering::MonoDevBenchBridge::Install() {}
#endif
