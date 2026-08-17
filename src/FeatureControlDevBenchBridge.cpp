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
#	include <unordered_map>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr unsigned int kDevBenchToolExtensionRevision = 5;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };
	std::unordered_map<std::string, json> g_performanceSnapshots;

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

	Feature* FindFeatureByShortName(const std::string& a_featureName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && a_featureName == feature->GetShortName())
				return feature;
		}
		return nullptr;
	}

	bool ResetsHistoryForPerformanceToggle(const std::string& a_featureName)
	{
		return a_featureName == "Skylighting" ||
		       a_featureName == "ScreenSpaceGI" ||
		       a_featureName == "Wetterness" ||
		       a_featureName == "WetnessEffects" ||
		       a_featureName == "Upscaling";
	}

	const char* GetPerformanceResourceImpact(const std::string& a_featureName)
	{
		if (a_featureName == "Upscaling")
			return "render-target-relatch-and-vendor-resources";
		if (a_featureName == "VolumetricLighting")
			return "recreate-volumetric-targets";
		if (a_featureName == "Skylighting")
			return "reset-probe-resources-and-history";
		if (a_featureName == "Wetterness" || a_featureName == "WetnessEffects")
			return "reset-temporal-weather-state";
		if (a_featureName == "VR")
			return "shared-multi-feature-vr-state";
		return "retained";
	}

	json BuildPerformanceRecord(Feature& a_feature)
	{
		const std::string featureName = a_feature.GetShortName();
		const bool supported = a_feature.SupportsPerformanceCostMeasurement();
		const bool available = a_feature.loaded && supported;
		const bool value = available && a_feature.IsPerformanceCostMeasurementEnabled();
		return {
			{ "feature", featureName },
			{ "displayName", a_feature.GetDisplayName() },
			{ "settingsSection", a_feature.GetName() },
			{ "control", "performanceActive" },
			{ "valueType", "boolean" },
			{ "requestedValue", value },
			{ "effectiveValue", value },
			{ "runtimeActive", value },
			{ "ready", available && a_feature.IsPerformanceCostMeasurementReady() },
			{ "waitText", a_feature.GetPerformanceCostMeasurementWaitText() },
			{ "mutability", "live-settle" },
			{ "settle",
				{
					{ "kind", "seconds-and-readiness" },
					{ "whenEnabledSeconds", a_feature.GetPerformanceCostMeasurementSettleSeconds(true) },
					{ "whenDisabledSeconds", a_feature.GetPerformanceCostMeasurementSettleSeconds(false) },
					{ "requiresMenuCloseWhenEnabled", a_feature.RequiresMenuCloseForPerformanceCostMeasurement(true) },
					{ "requiresMenuCloseWhenDisabled", a_feature.RequiresMenuCloseForPerformanceCostMeasurement(false) },
					{ "menuCloseWaitMs", a_feature.GetPerformanceCostMeasurementMenuCloseWaitMs() },
					{ "resetsHistory", ResetsHistoryForPerformanceToggle(featureName) },
				} },
			{ "cacheImpact", "loaded-shader-set-retained" },
			{ "resourceImpact", GetPerformanceResourceImpact(featureName) },
			{ "canRestoreInSession", true },
			{ "snapshotHeld", g_performanceSnapshots.contains(featureName) },
			{ "writable", available },
			{ "available", available },
			{ "unavailableReason", available ? json(nullptr) : json(a_feature.loaded ? "feature does not expose the Performance Tuning measurement contract" : "feature package is not loaded") },
		};
	}

	json BuildPerformanceControlList()
	{
		json controls = json::array();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && feature->SupportsPerformanceCostMeasurement())
				controls.push_back(BuildPerformanceRecord(*feature));
		}
		return controls;
	}

	json BuildControlList()
	{
		json controls = json::array({
			BuildPackageRecord(globals::features::ibl, "ImageBasedLighting"),
			BuildIBLRecord(),
			BuildPackageRecord(globals::features::volumetricShadows, "VolumetricShadows"),
			BuildVolumetricShadowsRecord(),
		});
		for (auto& control : BuildPerformanceControlList())
			controls.push_back(std::move(control));
		return controls;
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
		if (a_control == "performanceActive") {
			if (auto* feature = FindFeatureByShortName(a_feature))
				return BuildPerformanceRecord(*feature);
		}
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
		if (a_control == "performanceActive") {
			auto* feature = FindFeatureByShortName(a_feature);
			if (!feature)
				return FindControl(a_feature, a_control);
			if (!feature->loaded || !feature->SupportsPerformanceCostMeasurement())
				return { { "error", "performance control is unavailable" }, { "control", BuildPerformanceRecord(*feature) } };

			bool restoredSnapshot = false;
			if (!a_value) {
				const bool inserted = !g_performanceSnapshots.contains(a_feature);
				if (inserted)
					g_performanceSnapshots.emplace(a_feature, feature->CapturePerformanceCostMeasurementState());
				try {
					feature->SetPerformanceCostMeasurementEnabled(false);
				} catch (...) {
					if (inserted)
						g_performanceSnapshots.erase(a_feature);
					throw;
				}
			} else if (const auto snapshot = g_performanceSnapshots.find(a_feature); snapshot != g_performanceSnapshots.end()) {
				feature->RestorePerformanceCostMeasurementState(snapshot->second);
				g_performanceSnapshots.erase(snapshot);
				restoredSnapshot = true;
			} else {
				feature->SetPerformanceCostMeasurementEnabled(true);
			}

			return {
				{ "action", "set" },
				{ "persisted", false },
				{ "restoredSnapshot", restoredSnapshot },
				{ "control", BuildPerformanceRecord(*feature) },
			};
		}
		if (a_control == "packageEnabled") {
			return {
				{ "error", "package control requires a process restart and is read-only in this session" },
				{ "control", FindControl(a_feature, a_control) },
			};
		}
		return FindControl(a_feature, a_control);
	}

	json RestoreAllPerformanceSnapshots()
	{
		json restored = json::array();
		json failed = json::array();
		for (auto snapshot = g_performanceSnapshots.begin(); snapshot != g_performanceSnapshots.end();) {
			auto* feature = FindFeatureByShortName(snapshot->first);
			if (!feature) {
				failed.push_back({ { "feature", snapshot->first }, { "error", "feature is no longer registered" } });
				++snapshot;
				continue;
			}
			try {
				feature->RestorePerformanceCostMeasurementState(snapshot->second);
				restored.push_back(snapshot->first);
				snapshot = g_performanceSnapshots.erase(snapshot);
			} catch (const std::exception& e) {
				failed.push_back({ { "feature", snapshot->first }, { "error", e.what() } });
				++snapshot;
			} catch (...) {
				failed.push_back({ { "feature", snapshot->first }, { "error", "restore failed" } });
				++snapshot;
			}
		}
		return {
			{ "action", "restoreAll" },
			{ "persisted", false },
			{ "restored", std::move(restored) },
			{ "failed", std::move(failed) },
			{ "remainingSnapshots", g_performanceSnapshots.size() },
		};
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
		if (action == "restoreAll")
			return RunOnMainThread([]() { return RestoreAllPerformanceSnapshots(); });

		if (action != "get" && action != "set") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "list", "get", "set", "restoreAll" }) },
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
			{ "actions", json::array({ "list", "get", "set", "restoreAll" }) },
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
			R"({"description":"Discover and mutate typed CSX feature controls with effective readback and explicit live/reload/restart metadata. Mutations are session-only and never rewrite arbitrary settings JSON. IBL and Volumetric Shadows expose audited inner toggles. Features that implement CSX's production Performance Tuning measurement contract also expose a reversible performanceActive control: the first disable snapshots the complete feature state, and enabling restores that snapshot. restoreAll is a safety action for every outstanding snapshot. Package enablement remains restart-bound and read-only.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["list","get","set","restoreAll"],"default":"list"},"feature":{"type":"string"},"control":{"type":"string","enum":["packageEnabled","EnableIBL","Enabled","performanceActive"]},"value":{"type":"boolean"}},"required":["action"]}})";
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
