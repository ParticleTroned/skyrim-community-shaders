#include "FSRTemporalTuningDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "Globals.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <array>
#	include <atomic>
#	include <exception>
#	include <stdexcept>
#	include <string>
#	include <string_view>

namespace
{
	using json = nlohmann::json;
	using Settings = FSRTemporalTuningPolicy::Settings;
	std::atomic_bool installAttempted{ false };

	json SettingsJson(const Settings& settings)
	{
		return {
			{ "enabled", settings.enabled },
			{ "velocityFactor", settings.velocityFactor },
			{ "reactivenessScale", settings.reactivenessScale },
			{ "shadingChangeScale", settings.shadingChangeScale },
			{ "accumulationAddedPerFrame", settings.accumulationAddedPerFrame },
			{ "minimumDisocclusionAccumulation", settings.minimumDisocclusionAccumulation }
		};
	}

	json SnapshotJson()
	{
		const auto snapshot = Upscaling::fidelityFX.GetTemporalTuningSnapshot();
		return {
			{ "requested", SettingsJson(snapshot.requested) },
			{ "contextSettings", SettingsJson(snapshot.contextSettings) },
			{ "status", FSRTemporalTuningPolicy::StatusLabel(snapshot.status) },
			{ "providerId", snapshot.providerId },
			{ "providerVersionSupported", FSRTemporalTuningPolicy::SupportsProvider(snapshot.providerId) },
			{ "configuredContexts", snapshot.configuredContexts },
			{ "lastConfigureResult", snapshot.lastConfigureResult },
			{ "requestRevision", snapshot.requestRevision },
			{ "lastDispatchPath", static_cast<uint32_t>(snapshot.lastDispatchPath) }
		};
	}

	json BuildResult(const json& args)
	{
		for (const auto& [key, value] : args.items()) {
			if (key != "action" && key != "settings" && key != "persist" && key != "expectedBuildId")
				return { { "error", "unknown parameter" }, { "parameter", key } };
		}
		const auto action = args.value("action", std::string("status"));
		if (action != "status" && action != "set")
			return { { "error", "action must be status or set" } };
		if (action == "status") {
			if (args.contains("settings") || args.contains("persist"))
				return { { "error", "status does not accept mutation parameters" } };
			return SnapshotJson();
		}
		if (!args.contains("settings") || !args.at("settings").is_object() || args.at("settings").empty())
			return { { "error", "set requires a nonempty settings object" } };
		if (args.contains("persist") && !args.at("persist").is_boolean())
			return { { "error", "persist must be boolean" } };
		const auto patch = args.at("settings");
		const auto persist = args.value("persist", false);
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), [patch, persist]() -> json {
			auto& upscaling = globals::features::upscaling;
			auto settings = upscaling.settings.fsrTemporalTuning;
			for (const auto& [key, value] : patch.items()) {
				if (key == "enabled") {
					if (!value.is_boolean())
						return { { "error", "enabled must be boolean" } };
					settings.enabled = value.get<bool>();
					continue;
				}
				float* target = nullptr;
				if (key == "velocityFactor")
					target = &settings.velocityFactor;
				else if (key == "reactivenessScale")
					target = &settings.reactivenessScale;
				else if (key == "shadingChangeScale")
					target = &settings.shadingChangeScale;
				else if (key == "accumulationAddedPerFrame")
					target = &settings.accumulationAddedPerFrame;
				else if (key == "minimumDisocclusionAccumulation")
					target = &settings.minimumDisocclusionAccumulation;
				if (!target || !value.is_number())
					return { { "error", "unknown or nonnumeric tuning setting" }, { "setting", key } };
				*target = value.get<float>();
			}
			if (!upscaling.SetFSRTemporalTuningSettings(settings))
				return { { "error", "settings must be finite and within the declared ranges; no settings changed" } };
			const bool saved = persist && globals::state && globals::state->Save();
			auto result = SnapshotJson();
			result["accepted"] = true;
			result["persistRequested"] = persist;
			result["saved"] = saved;
			if (persist && !saved)
				result["error"] = "settings queued in memory but saving the user configuration failed";
			return result;
		});
	}

	void ToolHandler(void*, const char* argsJson, void* sink, DevBenchAPI::WriteFn write) noexcept
	{
		json output;
		try {
			const auto args = argsJson && *argsJson ? json::parse(argsJson) : json::object();
			if (!args.is_object())
				throw std::runtime_error("arguments must be an object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = BuildResult(args);
		} catch (const std::exception& error) {
			output = { { "error", "invalid request" }, { "detail", error.what() } };
		} catch (...) {
			output = { { "error", "unknown FSR temporal tuning handler error" } };
		}
		BuildProvenance::AttachProducer(output);
		try {
			const auto serialized = output.dump();
			write(sink, serialized.c_str());
		} catch (...) {
			write(sink, R"({"error":"response serialization failed"})");
		}
	}
}

void FSRTemporalTuningDevBenchBridge::Install()
{
	if (installAttempted.exchange(true))
		return;
	auto* devBench = DevBenchAPI::GetDevBenchInterface001();
	if (!devBench)
		return;
	static const std::string descriptor = [] {
		json settingsProperties = {
			{ "enabled", { { "type", "boolean" } } },
			{ "velocityFactor", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 } } },
			{ "reactivenessScale", { { "type", "number" }, { "minimum", 0 }, { "maximum", FSRTemporalTuningPolicy::kMaximumResponseScale } } },
			{ "shadingChangeScale", { { "type", "number" }, { "minimum", 0 }, { "maximum", FSRTemporalTuningPolicy::kMaximumResponseScale } } },
			{ "accumulationAddedPerFrame", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 } } },
			{ "minimumDisocclusionAccumulation", { { "type", "number" }, { "minimum", -1 }, { "maximum", 1 } } }
		};
		return json{
			{ "description", "Inspect or set optional FSR temporal reconstruction overrides on SE, AE and VR. Disabled by default. Only verified runtime FSR 3.1.4/3.1.5 providers accept this complete key set; host FSR and FSR4 retain vendor defaults. set atomically patches requested settings, and render-thread context recreation applies the complete profile to all eyes before dispatch. Rejection recreates untouched vendor defaults and latches that request; provider faults follow runtime quarantine. status reports requested versus applied settings, pending/unsupported/rejected/faulted state and last configure result. persist saves the user configuration only when explicitly true. No resolution, provider selection or public upscaling ABI changes." },
			{ "inputSchema", { { "type", "object" }, { "additionalProperties", false },
								 { "properties", { { "action", { { "type", "string" }, { "enum", { "status", "set" } }, { "default", "status" } } },
													 { "settings", { { "type", "object" }, { "additionalProperties", false }, { "minProperties", 1 }, { "properties", std::move(settingsProperties) } } },
													 { "persist", { { "type", "boolean" }, { "default", false } } },
													 { "expectedBuildId", { { "type", "string" } } } } } } }
		}.dump();
	}();
	devBench->RegisterTool("communityshaders.fsr_temporal_tuning", descriptor.c_str(), &ToolHandler, nullptr);
	logger::info("FSRTemporalTuningDevBenchBridge: registered reconstruction controls");
}

#else

void FSRTemporalTuningDevBenchBridge::Install() {}

#endif
