#include "Api/EditorDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/EditorService.h"
#	include "Api/ServiceFoundation.h"
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
	using CSX::EditorAPI::MutationAction;
	using CSX::EditorAPI::MutationReceipt001;
	using CSX::EditorAPI::MutationRequest001;
	using CSX::EditorAPI::Preflight001;
	using CSX::EditorAPI::Snapshot001;
	using CSX::EditorAPI::Status;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ CSX::EditorAPI::ServiceName, 1, 0, 1 });
		static std::once_flag initialized;
		std::call_once(initialized, [&] {
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
		case Status::kBlocked: return "blocked";
		default: return "internal_error";
		}
	}

	const char* PreviewName(CSX::EditorAPI::PreviewMode a_mode)
	{
		switch (a_mode) {
		case CSX::EditorAPI::PreviewMode::kFreeCamera: return "free_camera";
		case CSX::EditorAPI::PreviewMode::kFreeCameraLocked: return "free_camera_locked";
		case CSX::EditorAPI::PreviewMode::kPlayMode: return "play_mode";
		default: return "none";
		}
	}

	std::optional<MutationAction> ParseAction(std::string_view a_action)
	{
		if (a_action == "open") return MutationAction::kOpen;
		if (a_action == "close") return MutationAction::kClose;
		if (a_action == "toggle") return MutationAction::kToggle;
		if (a_action == "reset_layout") return MutationAction::kResetLayout;
		if (a_action == "exit_preview") return MutationAction::kExitPreview;
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
			if (cancelled->load(std::memory_order_acquire)) return;
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

	json SnapshotJson(const Snapshot001& a_value)
	{
		return {
			{ "available", a_value.available != 0 }, { "dataAvailable", a_value.dataAvailable != 0 },
			{ "canOpen", a_value.canOpen != 0 }, { "resourcesInitialized", a_value.resourcesInitialized != 0 },
			{ "editorOpen", a_value.editorOpen != 0 }, { "menuSessionOpen", a_value.menuSessionOpen != 0 },
			{ "mainMenuOpen", a_value.mainMenuOpen != 0 }, { "loadingMenuOpen", a_value.loadingMenuOpen != 0 },
			{ "persistentMutationBlocked", a_value.persistentMutationBlocked != 0 },
			{ "saveLoadSafeModeActive", a_value.saveLoadSafeModeActive != 0 },
			{ "weatherLocked", a_value.weatherLocked != 0 }, { "timePaused", a_value.timePaused != 0 },
			{ "undoAvailable", a_value.undoAvailable != 0 }, { "previewMode", PreviewName(a_value.previewMode) },
			{ "stateRevision", a_value.stateRevision }, { "capabilities", a_value.capabilities },
			{ "unavailableReason", a_value.unavailableReason ? a_value.unavailableReason : "" },
			{ "buildId", a_value.buildId ? a_value.buildId : "" },
		};
	}

	MutationRequest001 ParseMutation(const json& a_args, std::string& a_token)
	{
		if (!a_args.contains("mutation") || !a_args["mutation"].is_object())
			throw std::runtime_error("mutation object is required");
		const auto& mutation = a_args["mutation"];
		const auto action = ParseAction(mutation.value("action", std::string{}));
		if (!action)
			throw std::runtime_error("mutation.action is unknown");
		if (!mutation.contains("expectedStateRevision") || !mutation["expectedStateRevision"].is_number_unsigned())
			throw std::runtime_error("mutation.expectedStateRevision is required and must be unsigned");
		a_token = mutation.value("preflightToken", std::string{});
		return { .structSize = sizeof(MutationRequest001), .action = *action,
			.expectedStateRevision = mutation["expectedStateRevision"].get<std::uint64_t>(),
			.flags = mutation.value("allowDisruptive", false) ? CSX::EditorAPI::kMutationAllowDisruptive : CSX::EditorAPI::kMutationNone,
			.preflightToken = a_token.empty() ? nullptr : a_token.c_str() };
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		if (action != "registry" && action != "snapshot" && action != "preflight" && action != "execute")
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");
		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", CSX::EditorAPI::ServiceName }, { "major", 1 }, { "minor", 0 }, { "schemaRevision", 1 },
				{ "capabilities", CSX::EditorAPI::ServiceCapabilities }, { "mainThreadAffine", true },
				{ "registryMainThreadAffine", false }, { "preflightTokenLifetimeMs", 30000 },
				{ "actions", json::array({ "registry", "snapshot", "preflight", "execute" }) },
				{ "mutations", json::array({ "open", "close", "toggle", "reset_layout", "exit_preview" }) },
				{ "legacyInterfacesPreserved", true },
			};
			return response;
		}
		if (action == "preflight" || action == "execute") {
			try { std::string token; (void)ParseMutation(a_args, token); }
			catch (const std::exception& e) { return Foundation().MakeError(a_args, "invalid_mutation", e.what(), "validation", false, "mutation"); }
		}

		auto result = RunOnMainThread([action, a_args] {
			const auto* api = CSX::Api::GetEditorService001();
			if (!api) return json{ { "error", "editor API unavailable" } };
			if (action == "snapshot") {
				Snapshot001 value;
				const auto status = api->GetSnapshot(api->context, &value);
				return json{ { "status", StatusName(status) }, { "snapshot", SnapshotJson(value) } };
			}
			std::string token;
			auto mutation = ParseMutation(a_args, token);
			if (action == "preflight") {
				Preflight001 value;
				const auto status = api->Preflight(api->context, &mutation, &value);
				return json{ { "status", StatusName(status) }, { "allowed", value.allowed != 0 },
					{ "disruptive", value.disruptive != 0 }, { "stateRevision", value.stateRevision },
					{ "requiredFlags", value.requiredFlags }, { "preflightToken", value.token ? value.token : "" },
					{ "reasonCode", value.reasonCode ? value.reasonCode : "" }, { "message", value.message ? value.message : "" } };
			}
			MutationReceipt001 receipt;
			const auto status = api->Execute(api->context, &mutation, &receipt);
			Snapshot001 current;
			(void)api->GetSnapshot(api->context, &current);
			return json{ { "status", StatusName(status) }, { "applied", receipt.applied != 0 }, { "changed", receipt.changed != 0 },
				{ "previousStateRevision", receipt.previousStateRevision }, { "stateRevision", receipt.stateRevision },
				{ "message", receipt.message ? receipt.message : "" }, { "current", SnapshotJson(current) } };
		});
		if (result.contains("error"))
			return Foundation().MakeError(a_args, "main_thread_dispatch_failed", result.value("detail", result.value("error", std::string("editor API dispatch failed"))), "dispatch", true);
		auto response = Foundation().MakeEnvelope(a_args, true);
		response["result"] = std::move(result);
		return response;
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = Foundation().MakeError(args, mismatch->value("code", std::string("producer_mismatch")), mismatch->value("error", std::string("loaded CSX build does not match the request")), "validation", false, "expectedBuildId");
			else
				output = Foundation().Dispatch(args, &BuildResult);
		} catch (const std::exception& e) { output = Foundation().MakeError(json::object(), "invalid_request", e.what()); }
		catch (...) { output = Foundation().MakeError(json::object(), "internal_error", "unknown editor API error", "dispatch", true); }
		try { const auto serialized = output.dump(); a_write(a_sink, serialized.c_str()); }
		catch (...) { a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})"); }
	}
}

namespace CSX::Api::EditorDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire)) return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) { logger::info("EditorDevBenchBridge: devbench host not present; editor API tool not registered"); return; }
		const char* descriptor = R"({
			"description":"Versioned CSX Editor state and bounded window lifecycle API. Mutations require preflight then execute with identical arguments and the returned token.",
			"inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{
				"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},
				"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},
				"action":{"type":"string","enum":["registry","snapshot","preflight","execute"]},
				"mutation":{"type":"object","required":["action","expectedStateRevision"],"properties":{
					"action":{"type":"string","enum":["open","close","toggle","reset_layout","exit_preview"]},
					"expectedStateRevision":{"type":"integer","minimum":0},"allowDisruptive":{"type":"boolean"},"preflightToken":{"type":"string"}
				}}
			}}
		})";
		devBench->RegisterTool("communityshaders.editor_api", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("EditorDevBenchBridge: registered communityshaders.editor_api with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace CSX::Api::EditorDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}

#endif
