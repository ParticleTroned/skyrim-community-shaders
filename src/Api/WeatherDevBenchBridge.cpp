#include "Api/WeatherDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/ServiceFoundation.h"
#	include "Api/WeatherService.h"
#	include "BuildProvenance.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <functional>
#	include <future>
#	include <memory>
#	include <mutex>
#	include <optional>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using CSX::WeatherAPI::MutationAction;
	using CSX::WeatherAPI::MutationReceipt001;
	using CSX::WeatherAPI::MutationRequest001;
	using CSX::WeatherAPI::Preflight001;
	using CSX::WeatherAPI::Snapshot001;
	using CSX::WeatherAPI::Status;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ CSX::WeatherAPI::ServiceName, 1, 0, 1 });
		static std::once_flag metadataInitialized;
		std::call_once(metadataInitialized, [&] {
			foundation.SetServerMetadataProvider([] {
				auto producer = BuildProvenance::GetProducer();
				producer["serviceSessionId"] = Foundation().SessionId();
				return producer;
			});
		});
		return foundation;
	}

	const char* StatusName(Status a_status)
	{
		switch (a_status) {
		case Status::kSuccess:
			return "success";
		case Status::kInvalidArgument:
			return "invalid_argument";
		case Status::kStructureTooSmall:
			return "structure_too_small";
		case Status::kUnavailable:
			return "unavailable";
		case Status::kWrongThread:
			return "wrong_thread";
		case Status::kRevisionConflict:
			return "revision_conflict";
		case Status::kPreflightRequired:
			return "preflight_required";
		case Status::kPreflightExpired:
			return "preflight_expired";
		case Status::kPreflightMismatch:
			return "preflight_mismatch";
		case Status::kWeatherNotFound:
			return "weather_not_found";
		case Status::kFeatureNotFound:
			return "feature_not_found";
		case Status::kVariableNotFound:
			return "variable_not_found";
		case Status::kBusy:
			return "busy";
		case Status::kBlocked:
			return "blocked";
		case Status::kPersistenceFailed:
			return "persistence_failed";
		case Status::kInvalidOverride:
			return "invalid_override";
		default:
			return "internal_error";
		}
	}

	std::optional<MutationAction> ParseMutationAction(std::string_view a_action)
	{
		if (a_action == "set_weather")
			return MutationAction::kSetWeather;
		if (a_action == "preview_weather")
			return MutationAction::kPreviewWeather;
		if (a_action == "reset_weather")
			return MutationAction::kResetWeather;
		if (a_action == "lock_weather")
			return MutationAction::kLockWeather;
		if (a_action == "unlock_weather")
			return MutationAction::kUnlockWeather;
		if (a_action == "set_feature_paused")
			return MutationAction::kSetFeaturePaused;
		if (a_action == "reload_overrides")
			return MutationAction::kReloadOverrides;
		if (a_action == "set_feature_override")
			return MutationAction::kSetFeatureOverride;
		if (a_action == "remove_feature_override")
			return MutationAction::kRemoveFeatureOverride;
		return std::nullopt;
	}

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

	json SnapshotJson(const Snapshot001& a_snapshot)
	{
		return {
			{ "available", a_snapshot.available != 0 },
			{ "skyAvailable", a_snapshot.skyAvailable != 0 },
			{ "stateRevision", a_snapshot.stateRevision },
			{ "capabilities", a_snapshot.capabilities },
			{ "transition", { { "active", a_snapshot.transitionActive != 0 }, { "factor", a_snapshot.transitionFactor },
								{ "currentWeatherKey", a_snapshot.currentWeatherKey ? a_snapshot.currentWeatherKey : "" },
								{ "lastWeatherKey", a_snapshot.lastWeatherKey ? a_snapshot.lastWeatherKey : "" } } },
			{ "selection", { { "defaultWeatherKey", a_snapshot.defaultWeatherKey ? a_snapshot.defaultWeatherKey : "" },
							   { "overrideWeatherKey", a_snapshot.overrideWeatherKey ? a_snapshot.overrideWeatherKey : "" } } },
			{ "lock", { { "active", a_snapshot.weatherLocked != 0 },
						  { "weatherKey", a_snapshot.lockedWeatherKey ? a_snapshot.lockedWeatherKey : "" },
						  { "hooksInstalled", a_snapshot.lockHooksInstalled != 0 } } },
			{ "persistence", { { "mutationBlocked", a_snapshot.persistentMutationBlocked != 0 },
								 { "saveLoadSafeModeActive", a_snapshot.saveLoadSafeModeActive != 0 } } },
			{ "weatherCount", a_snapshot.weatherCount },
			{ "weatherFeatureCount", a_snapshot.weatherFeatureCount },
			{ "buildId", a_snapshot.buildId ? a_snapshot.buildId : "" },
		};
	}

	json ReadSnapshot(const CSX::WeatherAPI::Interface001& a_api)
	{
		Snapshot001 snapshot;
		const auto status = a_api.GetSnapshot(a_api.context, &snapshot);
		return { { "status", StatusName(status) }, { "snapshot", SnapshotJson(snapshot) } };
	}

	MutationRequest001 ParseMutation(
		const json& a_args,
		std::string& a_weatherKey,
		std::string& a_featureName,
		std::string& a_valueJson,
		std::string& a_token)
	{
		if (!a_args.contains("mutation") || !a_args["mutation"].is_object())
			throw std::runtime_error("mutation object is required");
		const auto& mutation = a_args["mutation"];
		const auto action = ParseMutationAction(mutation.value("action", std::string{}));
		if (!action)
			throw std::runtime_error("mutation.action is unknown");
		if (!mutation.contains("expectedStateRevision") || !mutation["expectedStateRevision"].is_number_unsigned())
			throw std::runtime_error("mutation.expectedStateRevision is required and must be unsigned");
		std::uint64_t flags = CSX::WeatherAPI::kMutationNone;
		if (mutation.value("persist", false))
			flags |= CSX::WeatherAPI::kMutationPersist;
		if (mutation.value("applyLive", false))
			flags |= CSX::WeatherAPI::kMutationApplyLive;
		if (mutation.value("allowDisruptive", false))
			flags |= CSX::WeatherAPI::kMutationAllowDisruptive;
		if (mutation.value("allowDestructive", false))
			flags |= CSX::WeatherAPI::kMutationAllowDestructive;
		a_weatherKey = mutation.value("weatherKey", std::string{});
		a_featureName = mutation.value("featureName", std::string{});
		a_valueJson = mutation.contains("value") ? mutation["value"].dump() : std::string{};
		a_token = mutation.value("preflightToken", std::string{});
		const bool boolValue = mutation.contains("value") && mutation["value"].is_boolean() &&
		                       mutation["value"].get<bool>();
		return {
			.structSize = sizeof(MutationRequest001),
			.action = *action,
			.expectedStateRevision = mutation["expectedStateRevision"].get<std::uint64_t>(),
			.flags = flags,
			.boolValue = boolValue ? 1u : 0u,
			.accelerate = mutation.value("accelerate", false) ? 1u : 0u,
			.weatherKey = a_weatherKey.empty() ? nullptr : a_weatherKey.c_str(),
			.featureName = a_featureName.empty() ? nullptr : a_featureName.c_str(),
			.valueJson = a_valueJson.empty() ? nullptr : a_valueJson.c_str(),
			.preflightToken = a_token.empty() ? nullptr : a_token.c_str(),
		};
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		const bool known = action == "registry" || action == "snapshot" || action == "weathers" ||
		                   action == "features" || action == "variables" || action == "override" ||
		                   action == "preflight" || action == "execute";
		if (!known)
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");
		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", CSX::WeatherAPI::ServiceName },
				{ "major", CSX::WeatherAPI::ServiceMajor },
				{ "minor", CSX::WeatherAPI::ServiceMinor },
				{ "schemaRevision", CSX::WeatherAPI::SchemaRevision },
				{ "capabilities", CSX::WeatherAPI::ServiceCapabilities },
				{ "mainThreadAffine", true },
				{ "registryMainThreadAffine", false },
				{ "preflightTokenLifetimeMs", 30000 },
				{ "actions", json::array({ "registry", "snapshot", "weathers", "features", "variables", "override", "preflight", "execute" }) },
				{ "mutations", json::array({ "set_weather", "preview_weather", "reset_weather", "lock_weather", "unlock_weather", "set_feature_paused", "reload_overrides", "set_feature_override", "remove_feature_override" }) },
			};
			return response;
		}
		if (action == "preflight" || action == "execute") {
			try {
				std::string weather, feature, value, token;
				(void)ParseMutation(a_args, weather, feature, value, token);
			} catch (const std::exception& e) {
				return Foundation().MakeError(a_args, "invalid_mutation", e.what(), "validation", false, "mutation");
			}
		}

		auto result = RunOnMainThread([action, a_args] {
			const auto* api = CSX::Api::GetWeatherService001();
			if (!api)
				return json{ { "error", "weather API unavailable" } };
			if (action == "snapshot")
				return ReadSnapshot(*api);
			if (action == "weathers") {
				json values = json::array();
				const auto count = api->GetWeatherCount(api->context);
				for (std::uint32_t index = 0; index < count; ++index) {
					CSX::WeatherAPI::WeatherDescriptor001 value;
					if (api->GetWeatherDescriptor(api->context, index, &value) != Status::kSuccess)
						continue;
					values.push_back({ { "weatherKey", value.weatherKey ? value.weatherKey : "" },
						{ "editorId", value.editorId ? value.editorId : "" }, { "displayName", value.displayName ? value.displayName : "" },
						{ "pluginName", value.pluginName ? value.pluginName : "" }, { "runtimeFormId", value.runtimeFormId },
						{ "localFormId", value.localFormId }, { "weatherFlags", value.weatherFlags },
						{ "current", value.isCurrent != 0 }, { "last", value.isLast != 0 }, { "default", value.isDefault != 0 },
						{ "override", value.isOverride != 0 }, { "locked", value.isLocked != 0 } });
				}
				return json{ { "count", count }, { "weathers", std::move(values) } };
			}
			if (action == "features") {
				json values = json::array();
				const auto count = api->GetFeatureCount(api->context);
				for (std::uint32_t index = 0; index < count; ++index) {
					CSX::WeatherAPI::FeatureDescriptor001 value;
					if (api->GetFeatureDescriptor(api->context, index, &value) != Status::kSuccess)
						continue;
					values.push_back({ { "shortName", value.shortName ? value.shortName : "" },
						{ "displayName", value.displayName ? value.displayName : "" }, { "loaded", value.loaded != 0 },
						{ "paused", value.paused != 0 }, { "variableCount", value.variableCount },
						{ "activeOverrideCount", value.activeOverrideCount } });
				}
				return json{ { "count", count }, { "features", std::move(values) } };
			}
			if (action == "variables") {
				const auto featureName = a_args.value("featureName", std::string{});
				if (featureName.empty())
					return json{ { "error", "featureName is required" } };
				json values = json::array();
				const auto count = api->GetVariableCount(api->context, featureName.c_str());
				for (std::uint32_t index = 0; index < count; ++index) {
					CSX::WeatherAPI::VariableDescriptor001 value;
					if (api->GetVariableDescriptor(api->context, featureName.c_str(), index, &value) != Status::kSuccess)
						continue;
					json current = json::parse(value.currentValueJson ? value.currentValueJson : "null");
					json user = json::parse(value.userValueJson ? value.userValueJson : "null");
					values.push_back({ { "featureName", featureName }, { "variableName", value.variableName ? value.variableName : "" },
						{ "displayName", value.displayName ? value.displayName : "" }, { "tooltip", value.tooltip ? value.tooltip : "" },
						{ "valueKind", static_cast<std::uint32_t>(value.valueKind) }, { "activeOverride", value.activeOverride != 0 },
						{ "hasNumericRange", value.hasNumericRange != 0 }, { "minimumValue", value.minimumValue },
						{ "maximumValue", value.maximumValue }, { "currentValue", std::move(current) }, { "userValue", std::move(user) } });
				}
				return json{ { "featureName", featureName }, { "count", count }, { "variables", std::move(values) } };
			}
			if (action == "override") {
				const auto weatherKey = a_args.value("weatherKey", std::string{});
				const auto featureName = a_args.value("featureName", std::string{});
				if (weatherKey.empty() || featureName.empty())
					return json{ { "error", "weatherKey and featureName are required" } };
				CSX::WeatherAPI::OverrideSnapshot001 value;
				const auto status = api->GetOverride(api->context, weatherKey.c_str(), featureName.c_str(), &value);
				return json{ { "status", StatusName(status) }, { "weatherKey", weatherKey }, { "featureName", featureName },
					{ "effectivePresent", value.effectivePresent != 0 }, { "persistedPresent", value.persistedPresent != 0 },
					{ "effectiveValue", json::parse(value.effectiveValueJson ? value.effectiveValueJson : "null") },
					{ "persistedValue", json::parse(value.persistedValueJson ? value.persistedValueJson : "null") } };
			}
			if (action == "preflight" || action == "execute") {
				std::string weather, feature, value, token;
				auto request = ParseMutation(a_args, weather, feature, value, token);
				if (action == "preflight") {
					Preflight001 preflight;
					const auto status = api->Preflight(api->context, &request, &preflight);
					return json{ { "status", StatusName(status) }, { "allowed", preflight.allowed != 0 },
						{ "disruptive", preflight.disruptive != 0 }, { "destructive", preflight.destructive != 0 },
						{ "willPersist", preflight.willPersist != 0 }, { "willApplyLive", preflight.willApplyLive != 0 },
						{ "stateRevision", preflight.stateRevision }, { "requiredFlags", preflight.requiredFlags },
						{ "preflightToken", preflight.token ? preflight.token : "" },
						{ "reasonCode", preflight.reasonCode ? preflight.reasonCode : "" },
						{ "message", preflight.message ? preflight.message : "" } };
				}
				MutationReceipt001 receipt;
				const auto status = api->Execute(api->context, &request, &receipt);
				return json{ { "status", StatusName(status) }, { "applied", receipt.applied != 0 },
					{ "changed", receipt.changed != 0 }, { "persisted", receipt.persisted != 0 },
					{ "liveApplied", receipt.liveApplied != 0 }, { "previousStateRevision", receipt.previousStateRevision },
					{ "stateRevision", receipt.stateRevision }, { "message", receipt.message ? receipt.message : "" },
					{ "current", ReadSnapshot(*api) } };
			}
			return json{ { "error", "validated action was not dispatched" } };
		});
		if (result.contains("error")) {
			const auto message = result.value("detail", result.value("error", std::string("weather API dispatch failed")));
			return Foundation().MakeError(a_args, "main_thread_dispatch_failed", message, "dispatch", true);
		}
		auto response = Foundation().MakeEnvelope(a_args, true);
		response["result"] = std::move(result);
		return response;
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) {
				output = Foundation().MakeError(args, mismatch->value("code", std::string("producer_mismatch")),
					mismatch->value("error", std::string("loaded CSX build does not match the request")), "validation", false, "expectedBuildId");
			} else {
				output = Foundation().Dispatch(args, &BuildResult);
			}
		} catch (const std::exception& e) {
			output = Foundation().MakeError(json::object(), "invalid_request", e.what());
		} catch (...) {
			output = Foundation().MakeError(json::object(), "internal_error", "unknown weather API error", "dispatch", true);
		}
		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::Api::WeatherDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("WeatherDevBenchBridge: devbench host not present; weather API tool not registered");
			return;
		}
		const char* descriptor = R"({
			"description":"Versioned CSX weather selection, lock, registered-variable, and per-weather feature override API. Mutations require preflight then execute with identical arguments and the returned token.",
			"inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{
				"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},
				"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},
				"action":{"type":"string","enum":["registry","snapshot","weathers","features","variables","override","preflight","execute"]},
				"featureName":{"type":"string"},"weatherKey":{"type":"string"},
				"mutation":{"type":"object","required":["action","expectedStateRevision"],"properties":{
					"action":{"type":"string","enum":["set_weather","preview_weather","reset_weather","lock_weather","unlock_weather","set_feature_paused","reload_overrides","set_feature_override","remove_feature_override"]},
					"expectedStateRevision":{"type":"integer","minimum":0},"weatherKey":{"type":"string"},"featureName":{"type":"string"},
					"value":{},"accelerate":{"type":"boolean"},"persist":{"type":"boolean"},"applyLive":{"type":"boolean"},
					"allowDisruptive":{"type":"boolean"},"allowDestructive":{"type":"boolean"},"preflightToken":{"type":"string"}
				}}
			}}
		})";
		devBench->RegisterTool("communityshaders.weather_api", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("WeatherDevBenchBridge: registered communityshaders.weather_api with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace CSX::Api::WeatherDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}

#endif
