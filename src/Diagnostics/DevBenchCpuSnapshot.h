#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "VRAPI/CSfeatureapi.h"
#	include <nlohmann/json.hpp>
#	include <stdexcept>
#	include <string>
#	include <utility>

namespace CSX::Diagnostics
{
	/** Copies effective settings through the main-thread feature API without persisting them. */
	inline nlohmann::json CaptureFeatureSettings(const FeatureAPI::Interface001& a_api)
	{
		using namespace FeatureAPI;
		using json = nlohmann::json;
		Snapshot001 state;
		if (a_api.GetSnapshot(a_api.context, &state) != Status::kSuccess || !state.available)
			throw std::runtime_error("feature snapshot unavailable");
		json result{
			{ "stateRevision", state.stateRevision },
			{ "persistentMutationBlocked", state.persistentMutationBlocked != 0 },
			{ "saveLoadSafeModeActive", state.saveLoadSafeModeActive != 0 },
			{ "features", json::array() }, { "constraints", json::array() }
		};
		for (std::uint32_t index = 0; index < state.featureCount; ++index) {
			FeatureDescriptor001 feature;
			if (a_api.GetFeatureDescriptor(a_api.context, index, &feature) != Status::kSuccess || !feature.shortName)
				throw std::runtime_error("feature descriptor unavailable");
			// API strings belong to shared scratch storage, invalidated by the next call.
			const std::string name = feature.shortName;
			json item{
				{ "shortName", name }, { "loaded", feature.loaded != 0 },
				{ "disabledAtBoot", feature.disabledAtBoot != 0 },
				{ "runtimeDisabledByMissingDependency", feature.runtimeDisabledByMissingDependency != 0 },
				{ "failureMessage", feature.failureMessage ? feature.failureMessage : "" },
				{ "installedVersion", feature.installedVersion ? feature.installedVersion : "" },
				{ "settings", nullptr }, { "settingsStatus", "not_applicable" }
			};
			if (feature.loaded && feature.hasFeatureSettings) {
				SettingsSnapshot001 settings;
				if (a_api.GetFeatureSettings(a_api.context, name.c_str(), &settings) != Status::kSuccess || !settings.settingsJson)
					throw std::runtime_error("effective settings unavailable for " + name);
				item["settings"] = json::parse(settings.settingsJson);
				item["settingsStatus"] = "captured";
			}
			result["features"].push_back(std::move(item));
		}
		const auto count = a_api.GetConstraintCount(a_api.context);
		for (std::uint32_t index = 0; index < count; ++index) {
			ConstraintDescriptor001 constraint;
			if (a_api.GetConstraintDescriptor(a_api.context, index, &constraint) != Status::kSuccess ||
				!constraint.sourceFeatureShortName || !constraint.targetFeatureShortName ||
				!constraint.targetSettingPath || !constraint.forcedValueJson || !constraint.reason)
				throw std::runtime_error("feature constraint unavailable");
			result["constraints"].push_back({ { "sourceFeature", constraint.sourceFeatureShortName },
				{ "targetFeature", constraint.targetFeatureShortName },
				{ "targetSettingPath", constraint.targetSettingPath },
				{ "forcedValue", json::parse(constraint.forcedValueJson) },
				{ "reason", constraint.reason },
				{ "recommendDisableAtBoot", constraint.recommendDisableAtBoot != 0 } });
		}
		return result;
	}
}

#endif
