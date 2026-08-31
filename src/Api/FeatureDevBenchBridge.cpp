#include "Api/FeatureDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/FeatureService.h"
#	include "Api/RuntimeThreadAffinity.h"
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "FeatureIssues.h"
#	include "PresetCompatibility.h"
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>
#	include <atomic>
#	include <chrono>
#	include <functional>
#	include <future>
#	include <memory>
#	include <mutex>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using namespace CSX::FeatureAPI;
	constexpr auto kTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation value({ ServiceName, 1, 1, 2 });
		static std::once_flag once;
		std::call_once(once, [&] { value.SetServerMetadataProvider([] { auto p = BuildProvenance::GetProducer(); p["serviceSessionId"] = Foundation().SessionId(); return p; }); });
		return value;
	}

	const char* StatusName(Status value)
	{
		switch (value) {
		case Status::kSuccess: return "success"; case Status::kInvalidArgument: return "invalid_argument";
		case Status::kStructureTooSmall: return "structure_too_small"; case Status::kUnavailable: return "unavailable";
		case Status::kWrongThread: return "wrong_thread"; case Status::kRevisionConflict: return "revision_conflict";
		case Status::kPreflightRequired: return "preflight_required"; case Status::kPreflightExpired: return "preflight_expired";
		case Status::kPreflightMismatch: return "preflight_mismatch"; case Status::kFeatureNotFound: return "feature_not_found";
		case Status::kBlocked: return "blocked"; case Status::kPersistenceFailed: return "persistence_failed";
		default: return "internal_error";
		}
	}

	json OnMain(std::function<json()> run)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) return { { "error", "SKSE task interface unavailable" } };
		auto promise = std::make_shared<std::promise<json>>(); auto cancelled = std::make_shared<std::atomic_bool>(false); auto future = promise->get_future();
		tasks->AddTask([promise, cancelled, run = std::move(run)]() mutable {
			CSX::Api::EnterRuntimeMainThreadTask();
			if (cancelled->load(std::memory_order_acquire)) return;
			try { promise->set_value(run()); } catch (const std::exception& e) { promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } }); }
			catch (...) { promise->set_value(json{ { "error", "main-thread task failed" } }); }
		});
		if (future.wait_for(kTimeout) != std::future_status::ready) { cancelled->store(true, std::memory_order_release); return { { "error", "main thread did not run within 5000ms" } }; }
		return future.get();
	}

	json SnapshotJson(const Snapshot001& v)
	{
		return { { "available", v.available != 0 }, { "persistentMutationBlocked", v.persistentMutationBlocked != 0 },
			{ "saveLoadSafeModeActive", v.saveLoadSafeModeActive != 0 }, { "featureCount", v.featureCount },
			{ "loadedFeatureCount", v.loadedFeatureCount }, { "activeConstraintCount", v.activeConstraintCount },
			{ "stateRevision", v.stateRevision }, { "capabilities", v.capabilities }, { "buildId", v.buildId ? v.buildId : "" } };
	}

	json FeatureIssueJson(const FeatureIssues::FeatureIssueInfo& issue)
	{
		return {
			{ "shortName", issue.shortName },
			{ "displayName", issue.displayName },
			{ "version", issue.version },
			{ "issueType", magic_enum::enum_name(issue.issueType) },
			{ "rejectionReason", issue.rejectionReason },
			{ "replacementFeature", issue.replacementFeature },
			{ "replacementFeatureDisplayName", issue.replacementFeatureDisplayName },
			{ "replacementFeatureInstalled", issue.replacementFeatureInstalled },
			{ "replacementFeatureModLink", issue.replacementFeatureModLink },
			{ "userMessage", issue.userMessage },
			{ "minimumVersionRequired", issue.minimumVersionRequired },
			{ "modifiedShaderDirectory", issue.modifiedShaderDirectory },
			{ "iniPath", issue.iniPath },
			{ "removedInVersion", issue.removedInVersion.string(".") },
		};
	}

	MutationRequest001 Mutation(const json& args, std::string& feature, std::string& token)
	{
		if (!args.contains("mutation") || !args["mutation"].is_object()) throw std::runtime_error("mutation object is required");
		const auto& m = args["mutation"];
		if (m.value("action", std::string{}) != "set_disabled_at_boot") throw std::runtime_error("mutation.action is unknown");
		if (!m.contains("expectedStateRevision") || !m["expectedStateRevision"].is_number_unsigned()) throw std::runtime_error("mutation.expectedStateRevision is required and must be unsigned");
		feature = m.value("featureShortName", std::string{}); token = m.value("preflightToken", std::string{});
		std::uint64_t flags = 0; if (m.value("persist", false)) flags |= kMutationPersist; if (m.value("allowDisruptive", false)) flags |= kMutationAllowDisruptive;
		return { .structSize = sizeof(MutationRequest001), .action = MutationAction::kSetDisabledAtBoot,
			.expectedStateRevision = m["expectedStateRevision"].get<std::uint64_t>(), .flags = flags,
			.featureShortName = feature.empty() ? nullptr : feature.c_str(), .disabled = m.value("disabled", false) ? 1u : 0u,
			.preflightToken = token.empty() ? nullptr : token.c_str() };
	}

	json BuildResult(const json& args)
	{
		const auto action = args.value("action", std::string{});
		const bool known = action == "registry" || action == "snapshot" || action == "features" || action == "issues" || action == "preset_compatibility" || action == "settings" || action == "constraints" || action == "preflight" || action == "execute";
		if (!known) return Foundation().MakeError(args, "unknown_action", "action is not supported", "validation", false, "action");
		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(args, true);
			response["result"] = { { "service", ServiceName }, { "major", 1 }, { "minor", 1 }, { "schemaRevision", 2 },
				{ "capabilities", ServiceCapabilities }, { "mainThreadAffine", true }, { "registryMainThreadAffine", false },
				{ "preflightTokenLifetimeMs", 30000 }, { "actions", json::array({ "registry", "snapshot", "features", "issues", "preset_compatibility", "settings", "constraints", "preflight", "execute" }) },
				{ "mutations", json::array({ "set_disabled_at_boot" }) }, { "legacyInterfacesPreserved", true } };
			return response;
		}
		if (action == "preflight" || action == "execute") try { std::string f, t; (void)Mutation(args, f, t); } catch (const std::exception& e) { return Foundation().MakeError(args, "invalid_mutation", e.what(), "validation", false, "mutation"); }
		auto result = OnMain([action, args] {
			const auto* api = CSX::Api::GetFeatureService001(); if (!api) return json{ { "error", "feature API unavailable" } };
			if (action == "snapshot") { Snapshot001 v; const auto s = api->GetSnapshot(api->context, &v); return json{ { "status", StatusName(s) }, { "snapshot", SnapshotJson(v) } }; }
			if (action == "issues") {
				json issues = json::array();
				for (const auto& issue : FeatureIssues::GetFeatureIssues()) issues.push_back(FeatureIssueJson(issue));
				return json{ { "featureIssues", std::move(issues) }, { "hasIssues", FeatureIssues::HasFeatureIssues() },
					{ "hasObsoleteShaderModifyingFeatures", FeatureIssues::HasObsoleteShaderModifyingFeatures() },
					{ "hasPotentialShaderModifyingFeatures", FeatureIssues::HasPotentialShaderModifyingFeatures() } };
			}
			if (action == "preset_compatibility")
				return json{ { "presetCompatibility", PresetCompatibility::ToJson(PresetCompatibility::GetPublished()) } };
			if (action == "features") {
				json values = json::array(); const auto count = api->GetFeatureCount(api->context);
				for (std::uint32_t i = 0; i < count; ++i) { FeatureDescriptor001 v; if (api->GetFeatureDescriptor(api->context, i, &v) != Status::kSuccess) continue;
					values.push_back({ { "shortName", v.shortName ? v.shortName : "" }, { "name", v.name ? v.name : "" }, { "displayName", v.displayName ? v.displayName : "" },
						{ "category", v.category ? v.category : "" }, { "installedVersion", v.installedVersion ? v.installedVersion : "" }, { "requiredVersion", v.requiredVersion ? v.requiredVersion : "" },
						{ "shaderDefineName", v.shaderDefineName ? v.shaderDefineName : "" }, { "failureMessage", v.failureMessage ? v.failureMessage : "" }, { "summary", v.summary ? v.summary : "" },
						{ "summaryItems", json::parse(v.summaryItemsJson ? v.summaryItemsJson : "[]") }, { "loaded", v.loaded != 0 }, { "core", v.core != 0 }, { "inMenu", v.inMenu != 0 },
						{ "supportsVR", v.supportsVR != 0 }, { "disabledAtBoot", v.disabledAtBoot != 0 }, { "runtimeDisabledByMissingDependency", v.runtimeDisabledByMissingDependency != 0 },
						{ "hiddenFromUserView", v.hiddenFromUserView != 0 }, { "hiddenInEssentialsMode", v.hiddenInEssentialsMode != 0 }, { "hasFeatureSettings", v.hasFeatureSettings != 0 },
						{ "hasShaderDefine", v.hasShaderDefine != 0 }, { "activeConstraintCount", v.activeConstraintCount } }); }
				return json{ { "count", count }, { "features", std::move(values) } };
			}
			if (action == "settings") { const auto name = args.value("featureShortName", std::string{}); SettingsSnapshot001 v; const auto s = api->GetFeatureSettings(api->context, name.c_str(), &v);
				return json{ { "status", StatusName(s) }, { "featureShortName", name }, { "settings", s == Status::kSuccess ? json::parse(v.settingsJson ? v.settingsJson : "{}") : json(nullptr) } }; }
			if (action == "constraints") { json values = json::array(); const auto count = api->GetConstraintCount(api->context);
				for (std::uint32_t i = 0; i < count; ++i) { ConstraintDescriptor001 v; if (api->GetConstraintDescriptor(api->context, i, &v) != Status::kSuccess) continue;
					values.push_back({ { "sourceFeatureShortName", v.sourceFeatureShortName ? v.sourceFeatureShortName : "" }, { "targetFeatureShortName", v.targetFeatureShortName ? v.targetFeatureShortName : "" },
						{ "targetSettingPath", v.targetSettingPath ? v.targetSettingPath : "" }, { "forcedValue", json::parse(v.forcedValueJson ? v.forcedValueJson : "null") },
						{ "reason", v.reason ? v.reason : "" }, { "recommendDisableAtBoot", v.recommendDisableAtBoot != 0 } }); }
				return json{ { "count", count }, { "constraints", std::move(values) } }; }
			std::string feature, token; auto mutation = Mutation(args, feature, token);
			if (action == "preflight") { Preflight001 v; const auto s = api->Preflight(api->context, &mutation, &v); return json{ { "status", StatusName(s) }, { "allowed", v.allowed != 0 }, { "disruptive", v.disruptive != 0 },
				{ "willPersist", v.willPersist != 0 }, { "stateRevision", v.stateRevision }, { "requiredFlags", v.requiredFlags }, { "preflightToken", v.token ? v.token : "" },
				{ "reasonCode", v.reasonCode ? v.reasonCode : "" }, { "message", v.message ? v.message : "" } }; }
			MutationReceipt001 v; const auto s = api->Execute(api->context, &mutation, &v); Snapshot001 current; (void)api->GetSnapshot(api->context, &current);
			return json{ { "status", StatusName(s) }, { "applied", v.applied != 0 }, { "changed", v.changed != 0 }, { "persisted", v.persisted != 0 },
				{ "previousStateRevision", v.previousStateRevision }, { "stateRevision", v.stateRevision }, { "message", v.message ? v.message : "" }, { "current", SnapshotJson(current) } };
		});
		if (result.contains("error")) return Foundation().MakeError(args, "main_thread_dispatch_failed", result.value("detail", result.value("error", std::string("feature API dispatch failed"))), "dispatch", true);
		auto response = Foundation().MakeEnvelope(args, true); response["result"] = std::move(result); return response;
	}

	void Handler(void*, const char* text, void* sink, DevBenchAPI::WriteFn write) noexcept
	{
		json output;
		try { json args = text && *text ? json::parse(text) : json::object(); if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) output = Foundation().MakeError(args, mismatch->value("code", std::string("producer_mismatch")), mismatch->value("error", std::string("loaded build mismatch")), "validation", false, "expectedBuildId"); else output = Foundation().Dispatch(args, &BuildResult); }
		catch (const std::exception& e) { output = Foundation().MakeError(json::object(), "invalid_request", e.what()); } catch (...) { output = Foundation().MakeError(json::object(), "internal_error", "unknown feature API error", "dispatch", true); }
		try { const auto serialized = output.dump(); write(sink, serialized.c_str()); } catch (...) { write(sink, R"({"ok":false,"error":{"code":"serialization_failed"}})"); }
	}
}

namespace CSX::Api::FeatureDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire)) return;
		auto* host = DevBenchAPI::GetDevBenchInterface001(); if (!host) { logger::info("FeatureDevBenchBridge: devbench host not present; feature API tool not registered"); return; }
		const char* descriptor = R"({"description":"Versioned CSX feature catalog, settings/constraint inspection, detected feature issues, preset compatibility diagnostics, and guarded boot-configuration API. issues returns boot warning data; preset_compatibility reports whether marked SettingsUser content was accepted or rejected.","inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},"action":{"type":"string","enum":["registry","snapshot","features","issues","preset_compatibility","settings","constraints","preflight","execute"]},"featureShortName":{"type":"string"},"mutation":{"type":"object","required":["action","expectedStateRevision","featureShortName","disabled"],"properties":{"action":{"type":"string","const":"set_disabled_at_boot"},"expectedStateRevision":{"type":"integer","minimum":0},"featureShortName":{"type":"string"},"disabled":{"type":"boolean"},"persist":{"type":"boolean"},"allowDisruptive":{"type":"boolean"},"preflightToken":{"type":"string"}}}}}})";
		host->RegisterTool("communityshaders.feature_api", descriptor, &Handler, nullptr); g_registered.store(true, std::memory_order_release);
		logger::info("FeatureDevBenchBridge: registered communityshaders.feature_api with devbench build {}", host->GetBuildNumber());
	}
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else
namespace CSX::Api::FeatureDevBenchBridge { void Install() {} bool IsRegistered() { return false; } }
#endif
