#include "FeatureControlDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/IBL.h"
#	include "Features/VolumetricShadows.h"
#	include "Globals.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <functional>
#	include <future>
#	include <memory>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr unsigned int kDevBenchToolExtensionRevision = 5;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		taskInterface->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
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

	json BuildPackageRecord(const Feature& a_feature, const char* a_featureName)
	{
		return {
			{ "feature", a_featureName },
			{ "control", "packageEnabled" },
			{ "valueType", "boolean" },
			{ "requestedValue", a_feature.loaded },
			{ "effectiveValue", a_feature.loaded },
			{ "runtimeActive", a_feature.loaded },
			{ "mutability", "restart" },
			{ "settle", { { "kind", "processRestart" }, { "minimumFrames", 0 }, { "requiresMenuClose", false }, { "resetsHistory", true } } },
			{ "cacheImpact", "feature-set-and-shader-permutations" },
			{ "resourceImpact", "load-or-unload-package-resources-and-hooks" },
			{ "canRestoreInSession", false },
			{ "writable", false },
			{ "available", true },
			{ "unavailableReason", nullptr },
		};
	}

	json BuildIBLRecord()
	{
		const auto& feature = globals::features::ibl;
		const bool value = feature.settings.EnableIBL != 0;
		return {
			{ "feature", "ImageBasedLighting" },
			{ "control", "EnableIBL" },
			{ "valueType", "boolean" },
			{ "requestedValue", value },
			{ "effectiveValue", value },
			{ "runtimeActive", feature.IsRuntimeEnabled() },
			{ "mutability", "live" },
			{ "settle", { { "kind", "frames" }, { "minimumFrames", 2 }, { "requiresMenuClose", false }, { "resetsHistory", false } } },
			{ "cacheImpact", "none" },
			{ "resourceImpact", "retained" },
			{ "canRestoreInSession", true },
			{ "writable", feature.loaded },
			{ "available", feature.loaded },
			{ "unavailableReason", feature.loaded ? json(nullptr) : json("feature package is not loaded") },
		};
	}

	json BuildVolumetricShadowsRecord()
	{
		const auto& feature = globals::features::volumetricShadows;
		const bool value = feature.settings.Enabled;
		const bool hasDirectionalShadows = globals::state && globals::state->HasDirectionalShadows();
		return {
			{ "feature", "VolumetricShadows" },
			{ "control", "Enabled" },
			{ "valueType", "boolean" },
			{ "requestedValue", value },
			{ "effectiveValue", value },
			{ "runtimeActive", feature.loaded && value && hasDirectionalShadows },
			{ "mutability", "live" },
			{ "settle", { { "kind", "frames" }, { "minimumFrames", 2 }, { "requiresMenuClose", false }, { "resetsHistory", false } } },
			{ "cacheImpact", "none" },
			{ "resourceImpact", "retained-lazy" },
			{ "canRestoreInSession", true },
			{ "writable", feature.loaded },
			{ "available", feature.loaded },
			{ "unavailableReason", feature.loaded ? json(nullptr) : json("feature package is not loaded") },
		};
	}

	json BuildControlList()
	{
		return json::array({
			BuildPackageRecord(globals::features::ibl, "ImageBasedLighting"),
			BuildIBLRecord(),
			BuildPackageRecord(globals::features::volumetricShadows, "VolumetricShadows"),
			BuildVolumetricShadowsRecord(),
		});
	}

	json FindControl(const std::string& a_feature, const std::string& a_control)
	{
		if (a_feature == "ImageBasedLighting" && a_control == "packageEnabled")
			return BuildPackageRecord(globals::features::ibl, "ImageBasedLighting");
		if (a_feature == "ImageBasedLighting" && a_control == "EnableIBL")
			return BuildIBLRecord();
		if (a_feature == "VolumetricShadows" && a_control == "packageEnabled")
			return BuildPackageRecord(globals::features::volumetricShadows, "VolumetricShadows");
		if (a_feature == "VolumetricShadows" && a_control == "Enabled")
			return BuildVolumetricShadowsRecord();
		return {
			{ "error", "unknown control" },
			{ "feature", a_feature },
			{ "control", a_control },
		};
	}

	json SetControl(const std::string& a_feature, const std::string& a_control, bool a_value)
	{
		if (a_feature == "ImageBasedLighting" && a_control == "EnableIBL") {
			if (!globals::features::ibl.loaded)
				return { { "error", "feature package is not loaded" }, { "control", BuildIBLRecord() } };
			globals::features::ibl.settings.EnableIBL = a_value ? 1u : 0u;
			return { { "action", "set" }, { "persisted", false }, { "control", BuildIBLRecord() } };
		}
		if (a_feature == "VolumetricShadows" && a_control == "Enabled") {
			if (!globals::features::volumetricShadows.loaded)
				return { { "error", "feature package is not loaded" }, { "control", BuildVolumetricShadowsRecord() } };
			globals::features::volumetricShadows.settings.Enabled = a_value;
			return { { "action", "set" }, { "persisted", false }, { "control", BuildVolumetricShadowsRecord() } };
		}
		if (a_control == "packageEnabled") {
			return {
				{ "error", "package control requires a process restart and is read-only in this session" },
				{ "control", FindControl(a_feature, a_control) },
			};
		}
		return FindControl(a_feature, a_control);
	}

	json BuildControlResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("list"));
		if (action == "list") {
			return RunOnMainThread([]() {
				return json{
					{ "action", "list" },
					{ "sessionOnlyMutations", true },
					{ "controls", BuildControlList() },
				};
			});
		}

		if (action != "get" && action != "set") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "list", "get", "set" }) },
			};
		}

		const auto feature = a_args.value("feature", std::string());
		const auto control = a_args.value("control", std::string());
		if (feature.empty() || control.empty())
			return { { "error", "feature and control are required" } };

		if (action == "get")
			return RunOnMainThread([feature, control]() { return json{ { "action", "get" }, { "control", FindControl(feature, control) } }; });

		const auto value = a_args.find("value");
		if (value == a_args.end() || !value->is_boolean())
			return { { "error", "set requires a boolean value" } };
		const bool requestedValue = value->get<bool>();
		return RunOnMainThread([feature, control, requestedValue]() {
			return SetControl(feature, control, requestedValue);
		});
	}

	void RunHandler(
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			output = BuildControlResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}

	void ControlToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		RunHandler(a_argsJson, a_sink, a_write);
	}

	void ControlInspectExtensionHandler(
		void*,
		const char*,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		const json result{
			{ "registered", g_registered.load(std::memory_order_acquire) },
			{ "tool", "communityshaders.controls" },
			{ "usage", R"(Invoke communityshaders.controls directly, or dispatch it through a DevBench scenario tool step.)" },
			{ "actions", json::array({ "list", "get", "set" }) },
		};
		const auto serialized = result.dump();
		a_write(a_sink, serialized.c_str());
	}

	DevBenchAPI::IDevBenchInterface001* GetDevBenchToolExtensionInterface()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return nullptr;
		DevBenchAPI::DevBenchMessage message;
		messaging->Dispatch(
			DevBenchAPI::DevBenchMessage::kMessage_GetInterface,
			&message,
			sizeof(DevBenchAPI::DevBenchMessage*),
			DevBenchAPI::DevBenchPluginName);
		if (!message.GetApiFunction)
			return nullptr;
		return static_cast<DevBenchAPI::IDevBenchInterface001*>(
			message.GetApiFunction(kDevBenchToolExtensionRevision));
	}
}

namespace FeatureControlDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("FeatureControlDevBenchBridge: devbench host not present; controls tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"({"description":"Discover and mutate typed CSX feature controls with effective readback and explicit live/reload/restart metadata. Mutations are session-only and never rewrite arbitrary settings JSON. The initial writable surface contains the audited live inner controls for IBL and Volumetric Shadows; package enablement is reported as restart-bound and read-only.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["list","get","set"],"default":"list"},"feature":{"type":"string","enum":["ImageBasedLighting","VolumetricShadows"]},"control":{"type":"string","enum":["packageEnabled","EnableIBL","Enabled"]},"value":{"type":"boolean"}},"required":["action"]}})";
		devBench->RegisterTool(
			"communityshaders.controls",
			descriptor,
			&ControlToolHandler,
			nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* inspectDescriptor =
				R"({"description":"Reports the CSX typed feature-control tool registration."})";
			if (auto* extensionDevBench = GetDevBenchToolExtensionInterface()) {
				extensionDevBench->RegisterToolExtension(
					"inspect",
					"communityshaders.controls",
					inspectDescriptor,
					&ControlInspectExtensionHandler,
					nullptr);
			}
		}
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"FeatureControlDevBenchBridge: registered communityshaders.controls with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace FeatureControlDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
