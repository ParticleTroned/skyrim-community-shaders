#include "Api/ShaderDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/ServiceFoundation.h"
#	include "Api/ShaderService.h"
#	include "BuildProvenance.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <cstdint>
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
	using CSX::ShaderAPI::MutationAction;
	using CSX::ShaderAPI::MutationReceipt001;
	using CSX::ShaderAPI::MutationRequest001;
	using CSX::ShaderAPI::Preflight001;
	using CSX::ShaderAPI::Snapshot001;
	using CSX::ShaderAPI::Status;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ CSX::ShaderAPI::ServiceName, 1, 0, 1 });
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
		case Status::kSuccess: return "success";
		case Status::kInvalidArgument: return "invalid_argument";
		case Status::kStructureTooSmall: return "structure_too_small";
		case Status::kUnavailable: return "unavailable";
		case Status::kWrongThread: return "wrong_thread";
		case Status::kRevisionConflict: return "revision_conflict";
		case Status::kPreflightRequired: return "preflight_required";
		case Status::kPreflightExpired: return "preflight_expired";
		case Status::kPreflightMismatch: return "preflight_mismatch";
		case Status::kFeatureNotFound: return "feature_not_found";
		case Status::kBusy: return "busy";
		case Status::kBlocked: return "blocked";
		case Status::kPersistenceFailed: return "persistence_failed";
		default: return "internal_error";
		}
	}

	std::optional<MutationAction> ParseAction(std::string_view a_action)
	{
		if (a_action == "set_custom_shaders") return MutationAction::kSetCustomShaders;
		if (a_action == "set_disk_cache") return MutationAction::kSetDiskCache;
		if (a_action == "set_async_compilation") return MutationAction::kSetAsyncCompilation;
		if (a_action == "set_skip_unchanged") return MutationAction::kSetSkipUnchangedShaders;
		if (a_action == "set_feature_disabled_at_boot") return MutationAction::kSetFeatureDisabledAtBoot;
		if (a_action == "clear_memory_cache") return MutationAction::kClearMemoryCache;
		if (a_action == "clear_disk_cache") return MutationAction::kClearDiskCache;
		if (a_action == "clear_all_caches") return MutationAction::kClearAllCaches;
		if (a_action == "restore_previous_disk_cache") return MutationAction::kRestorePreviousDiskCache;
		if (a_action == "accept_cache_rebuild") return MutationAction::kAcceptCacheRebuild;
		if (a_action == "stop_compilation") return MutationAction::kStopCompilation;
		if (a_action == "capture_active_shaders") return MutationAction::kCaptureActiveShaders;
		return std::nullopt;
	}

	json SnapshotJson(const Snapshot001& a_snapshot)
	{
		return {
			{ "available", a_snapshot.available != 0 },
			{ "stateRevision", a_snapshot.stateRevision },
			{ "capabilities", a_snapshot.capabilities },
			{ "customShaders", {
				{ "requested", a_snapshot.customShadersRequested != 0 },
				{ "effective", a_snapshot.customShadersEffective != 0 },
				{ "transitionPending", a_snapshot.customShaderTransitionPending != 0 },
			} },
			{ "diskCache", {
				{ "requested", a_snapshot.diskCacheRequested != 0 },
				{ "active", a_snapshot.diskCacheActive != 0 },
				{ "held", a_snapshot.diskCacheHeld != 0 },
				{ "previousAvailable", a_snapshot.previousCacheAvailable != 0 },
				{ "featureSetChanged", a_snapshot.featureSetChanged != 0 },
				{ "featureSetRevertPending", a_snapshot.featureSetRevertPending != 0 },
			} },
			{ "persistence", {
				{ "mutationBlocked", a_snapshot.persistentMutationBlocked != 0 },
				{ "saveLoadSafeModeActive", a_snapshot.saveLoadSafeModeActive != 0 },
			} },
			{ "compilation", {
				{ "active", a_snapshot.compiling != 0 },
				{ "async", a_snapshot.asyncCompilation != 0 },
				{ "skipUnchanged", a_snapshot.skipUnchangedShaders != 0 },
				{ "activeShaderCapture", a_snapshot.activeShaderCapture != 0 },
				{ "totalTasks", a_snapshot.totalTasks },
				{ "completedTasks", a_snapshot.completedTasks },
				{ "failedTasks", a_snapshot.failedTasks },
				{ "currentFailedShaders", a_snapshot.currentFailedShaders },
				{ "memoryCacheHits", a_snapshot.memoryCacheHits },
				{ "diskCacheHits", a_snapshot.diskCacheHits },
				{ "sourceCompiles", a_snapshot.sourceCompiles },
				{ "slowTasks", a_snapshot.slowTasks },
				{ "verySlowTasks", a_snapshot.verySlowTasks },
				{ "heavyTasksInFlight", a_snapshot.heavyTasksInFlight },
				{ "foregroundThreadCount", a_snapshot.foregroundThreadCount },
				{ "backgroundThreadCount", a_snapshot.backgroundThreadCount },
				{ "statisticsText", a_snapshot.statisticsText ? a_snapshot.statisticsText : "" },
			} },
			{ "provenance", {
				{ "buildId", a_snapshot.buildId ? a_snapshot.buildId : "" },
				{ "shaderCacheAbiId", a_snapshot.shaderCacheAbiId ? a_snapshot.shaderCacheAbiId : "" },
				{ "shaderCompilerIdentity", a_snapshot.shaderCompilerIdentity ? a_snapshot.shaderCompilerIdentity : "" },
			} },
		};
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
			try { promise->set_value(run()); }
			catch (const std::exception& e) { promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } }); }
			catch (...) { promise->set_value(json{ { "error", "main-thread task failed" } }); }
		});
		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json ReadSnapshot(const CSX::ShaderAPI::Interface001& a_api)
	{
		Snapshot001 snapshot;
		const auto status = a_api.GetSnapshot(a_api.context, &snapshot);
		return { { "status", StatusName(status) }, { "snapshot", SnapshotJson(snapshot) } };
	}

	MutationRequest001 ParseMutation(const json& a_args, std::string& a_feature, std::string& a_token)
	{
		if (!a_args.contains("mutation") || !a_args["mutation"].is_object())
			throw std::runtime_error("mutation object is required");
		const auto& mutation = a_args["mutation"];
		const auto actionName = mutation.value("action", std::string{});
		const auto action = ParseAction(actionName);
		if (!action)
			throw std::runtime_error("mutation.action is unknown");
		if (!mutation.contains("expectedStateRevision") || !mutation["expectedStateRevision"].is_number_unsigned())
			throw std::runtime_error("mutation.expectedStateRevision is required and must be unsigned");
		std::uint64_t flags = CSX::ShaderAPI::kMutationNone;
		if (mutation.value("persist", false)) flags |= CSX::ShaderAPI::kMutationPersist;
		if (mutation.value("allowDisruptive", false)) flags |= CSX::ShaderAPI::kMutationAllowDisruptive;
		if (mutation.value("allowDestructive", false)) flags |= CSX::ShaderAPI::kMutationAllowDestructive;
		a_feature = mutation.value("featureName", std::string{});
		a_token = mutation.value("preflightToken", std::string{});
		return {
			.structSize = sizeof(MutationRequest001),
			.action = *action,
			.expectedStateRevision = mutation["expectedStateRevision"].get<std::uint64_t>(),
			.flags = flags,
			.boolValue = mutation.value("value", false) ? 1u : 0u,
			.featureName = a_feature.empty() ? nullptr : a_feature.c_str(),
			.preflightToken = a_token.empty() ? nullptr : a_token.c_str(),
		};
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		const bool knownAction = action == "registry" || action == "snapshot" || action == "features" ||
			action == "preflight" || action == "execute";
		if (!knownAction)
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");
		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", CSX::ShaderAPI::ServiceName },
				{ "major", CSX::ShaderAPI::ServiceMajor },
				{ "minor", CSX::ShaderAPI::ServiceMinor },
				{ "schemaRevision", CSX::ShaderAPI::SchemaRevision },
				{ "capabilities", CSX::ShaderAPI::ServiceCapabilities },
				{ "mainThreadAffine", true },
				{ "registryMainThreadAffine", false },
				{ "preflightTokenLifetimeMs", 30000 },
				{ "actions", json::array({ "registry", "snapshot", "features", "preflight", "execute" }) },
				{ "statusCodes", json::array({
					"success", "invalid_argument", "structure_too_small", "unavailable", "wrong_thread",
					"revision_conflict", "preflight_required", "preflight_expired", "preflight_mismatch",
					"feature_not_found", "busy", "blocked", "persistence_failed", "internal_error",
				}) },
				{ "mutations", json::array({
					"set_custom_shaders", "set_disk_cache", "set_async_compilation", "set_skip_unchanged",
					"set_feature_disabled_at_boot", "clear_memory_cache", "clear_disk_cache", "clear_all_caches",
					"restore_previous_disk_cache", "accept_cache_rebuild", "stop_compilation", "capture_active_shaders",
				}) },
			};
			return response;
		}
		if (action == "preflight" || action == "execute") {
			// Validate the JSON contract before entering the game-thread dispatch.
			// The request is rebuilt in the task so its borrowed string pointers are local.
			try {
				std::string feature;
				std::string token;
				(void)ParseMutation(a_args, feature, token);
			} catch (const std::exception& e) {
				return Foundation().MakeError(a_args, "invalid_mutation", e.what(), "validation", false, "mutation");
			}
		}

		auto result = RunOnMainThread([action, a_args] {
			const auto* api = CSX::Api::GetShaderService001();
			if (!api)
				return json{ { "error", "shader API unavailable" } };
			if (action == "snapshot")
				return ReadSnapshot(*api);
			if (action == "features") {
				json features = json::array();
				const auto count = api->GetFeatureCount(api->context);
				for (std::uint32_t index = 0; index < count; ++index) {
					CSX::ShaderAPI::FeatureDescriptor001 feature;
					const auto status = api->GetFeatureDescriptor(api->context, index, &feature);
					if (status != Status::kSuccess)
						continue;
					features.push_back({
						{ "shortName", feature.shortName ? feature.shortName : "" },
						{ "displayName", feature.displayName ? feature.displayName : "" },
						{ "version", feature.version ? feature.version : "" },
						{ "category", feature.category ? feature.category : "" },
						{ "shaderDefine", feature.shaderDefine ? feature.shaderDefine : "" },
						{ "loadFailure", feature.loadFailure ? feature.loadFailure : "" },
						{ "loaded", feature.loaded != 0 }, { "disabledAtBoot", feature.disabledAtBoot != 0 },
						{ "runtimeDisabledByMissingDependency", feature.runtimeDisabledByMissingDependency != 0 },
						{ "core", feature.core != 0 }, { "visibleInMenu", feature.visibleInMenu != 0 },
						{ "hiddenFromUser", feature.hiddenFromUser != 0 }, { "supportsVR", feature.supportsVR != 0 },
						{ "contributesShaderDefines", feature.contributesShaderDefines != 0 },
					});
				}
				return json{ { "count", count }, { "features", std::move(features) } };
			}
			if (action == "preflight" || action == "execute") {
				std::string feature;
				std::string token;
				auto request = ParseMutation(a_args, feature, token);
				if (action == "preflight") {
					Preflight001 preflight;
					const auto status = api->Preflight(api->context, &request, &preflight);
					return json{
						{ "status", StatusName(status) }, { "allowed", preflight.allowed != 0 },
						{ "disruptive", preflight.disruptive != 0 }, { "destructive", preflight.destructive != 0 },
						{ "restartRequired", preflight.restartRequired != 0 },
						{ "shaderRecompileExpected", preflight.shaderRecompileExpected != 0 },
						{ "stateRevision", preflight.stateRevision }, { "requiredFlags", preflight.requiredFlags },
						{ "preflightToken", preflight.token ? preflight.token : "" },
						{ "reasonCode", preflight.reasonCode ? preflight.reasonCode : "" },
						{ "message", preflight.message ? preflight.message : "" },
					};
				}
				MutationReceipt001 receipt;
				const auto status = api->Execute(api->context, &request, &receipt);
				return json{
					{ "status", StatusName(status) }, { "applied", receipt.applied != 0 },
					{ "changed", receipt.changed != 0 }, { "pending", receipt.pending != 0 },
					{ "restartRequired", receipt.restartRequired != 0 },
					{ "shaderRecompileExpected", receipt.shaderRecompileExpected != 0 },
					{ "persistenceRequested", receipt.persistenceRequested != 0 },
					{ "persisted", receipt.persisted != 0 },
					{ "previousStateRevision", receipt.previousStateRevision }, { "stateRevision", receipt.stateRevision },
					{ "message", receipt.message ? receipt.message : "" }, { "current", ReadSnapshot(*api) },
				};
			}
			return json{ { "error", "validated action was not dispatched" } };
		});
		if (result.contains("error")) {
			const auto message = result.value("detail", result.value("error", std::string("shader API dispatch failed")));
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
				output = Foundation().MakeError(
					args,
					mismatch->value("code", std::string("producer_mismatch")),
					mismatch->value("error", std::string("loaded CSX build does not match the request")),
					"validation",
					false,
					"expectedBuildId");
			} else {
				output = Foundation().Dispatch(args, &BuildResult);
			}
		} catch (const std::exception& e) {
			output = Foundation().MakeError(json::object(), "invalid_request", e.what());
		} catch (...) {
			output = Foundation().MakeError(json::object(), "internal_error", "unknown shader API error", "dispatch", true);
		}
		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::Api::ShaderDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("ShaderDevBenchBridge: devbench host not present; shader API tool not registered");
			return;
		}
		const char* descriptor = R"({
			"description":"Versioned CSX shader, feature, compilation, and cache lifecycle API. Every mutation uses preflight followed by execute with the exact same arguments and returned token.",
			"inputSchema":{
				"type":"object",
				"required":["contractMajor","clientId","commandId","action"],
				"properties":{
					"contractMajor":{"type":"integer","const":1},
					"clientId":{"type":"string","minLength":1,"maxLength":128},
					"commandId":{"type":"string","minLength":1,"maxLength":128},
					"expectedBuildId":{"type":"string"},
					"action":{"type":"string","enum":["registry","snapshot","features","preflight","execute"]},
					"mutation":{
						"type":"object",
						"required":["action","expectedStateRevision"],
						"properties":{
							"action":{"type":"string","enum":["set_custom_shaders","set_disk_cache","set_async_compilation","set_skip_unchanged","set_feature_disabled_at_boot","clear_memory_cache","clear_disk_cache","clear_all_caches","restore_previous_disk_cache","accept_cache_rebuild","stop_compilation","capture_active_shaders"]},
							"expectedStateRevision":{"type":"integer","minimum":0},
							"value":{"type":"boolean"},
							"featureName":{"type":"string"},
							"persist":{"type":"boolean"},
							"allowDisruptive":{"type":"boolean"},
							"allowDestructive":{"type":"boolean"},
							"preflightToken":{"type":"string"}
						}
					}
				}
			}
		})";
		devBench->RegisterTool("communityshaders.shader_api", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("ShaderDevBenchBridge: registered communityshaders.shader_api with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#else

namespace CSX::Api::ShaderDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}

#endif
