#include "LegacyUtilityCompatibility.h"

#include "Features/AdaptiveBalanceDepthOfFieldSettingsPolicy.h"
#include "Features/AdaptiveBalanceGodraySettings.h"
#include "Features/AdaptiveBrightness.h"
#include "Globals.h"
#include "SettingsMigrations.h"

#include <algorithm>
#include <string>
#include <utility>

namespace
{
	bool ValidateLegacyShape(const json& a_value, const json& a_schema, const std::string& a_path)
	{
		if (a_path == "vlIntensity")
			return a_value.is_number() && AdaptiveBalanceGodray::IsValidFinalBrightness(a_value.get<double>());
		if (a_path == "bloomEnhancement.Enabled")
			return a_value.is_boolean() || (a_value.is_number_integer() && (a_value == 0 || a_value == 1));
		if (a_path == "bloomEnhancement.SelectedPreset")
			return a_value.is_number_integer() && a_value >= 0 && a_value <= 2;
		if (a_schema.is_number_integer())
			return SettingsMigrations::IsUtilityUnsignedInteger(a_value);
		if (a_schema.is_number())
			return SettingsMigrations::IsFiniteUtilityNumber(a_value);
		if (a_schema.is_array()) {
			if (!a_value.is_array() || a_value.size() != a_schema.size())
				return false;
			for (std::size_t index = 0; index < a_value.size(); ++index) {
				if (!ValidateLegacyShape(a_value[index], a_schema[index], a_path))
					return false;
			}
			return true;
		}
		if (a_schema.is_object()) {
			if (!a_value.is_object())
				return false;
			for (const auto& [name, value] : a_value.items()) {
				const auto path = a_path.empty() ? name : a_path + "." + name;
				if (!a_schema.contains(name) || !ValidateLegacyShape(value, a_schema.at(name), path))
					return false;
			}
			return true;
		}
		return a_value.type() == a_schema.type();
	}

	struct LegacyUtilitySettings final : Feature
	{
		explicit LegacyUtilitySettings(std::string_view a_name) : name(a_name) {}
		std::string GetName() override { return name; }
		std::string GetShortName() override { return name; }
		std::string GetDisplayName() override { return "Adaptive Balance"; }
		bool IsInMenu() const override { return false; }
		bool IsHiddenFromUserView() const override { return true; }
		bool SupportsVR() override { return true; }
		bool IsCore() const override { return true; }

		void LoadSettings(json& a_json) override
		{
			auto& balance = globals::features::adaptiveBrightness;
			json candidate;
			balance.SaveSettings(candidate);
			if (!a_json.is_object()) {
				logger::warn("Rejected non-object legacy utility settings");
				return;
			}
			if (!ValidateLegacyShape(a_json, ProjectSettings(candidate), "")) {
				logger::warn("Rejected malformed or unknown legacy utility settings; no values were changed");
				return;
			}
			json depthPatch = json::object();
			for (const auto* key : { "enabled", "fixUnderwaterFogDofBlur", "sceneDof", "underwaterDof" }) {
				if (a_json.contains(key))
					depthPatch[key] = a_json.at(key);
			}
			std::string error;
			if (!depthPatch.empty() && !AdaptiveBalanceDepthOfFieldSettingsPolicy::Validate(depthPatch, error)) {
				logger::warn("Rejected legacy utility settings: {}", error);
				return;
			}
			try {
				for (const auto& [legacyPath, destinationPath] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
					const json::json_pointer source{ std::string(legacyPath) };
					if (a_json.contains(source))
						candidate[json::json_pointer(std::string(destinationPath))] = a_json.at(source);
				}
				for (const auto* key : { "enabled", "fixUnderwaterFogDofBlur", "sceneDof", "underwaterDof" }) {
					if (a_json.contains(key))
						candidate["depthOfField"][key].merge_patch(a_json.at(key));
				}
				if (!a_json.contains("enabled") &&
					((a_json.contains("sceneDof") && a_json.at("sceneDof").value("locked", false)) ||
						(a_json.contains("underwaterDof") && a_json.at("underwaterDof").value("locked", false))))
					candidate["depthOfField"]["enabled"] = true;
				if (a_json.contains("bloomEnhancement")) {
					const json projectedBloom = ProjectSettings(candidate).at("bloomEnhancement");
					json bloom = projectedBloom;
					bloom.merge_patch(a_json.at("bloomEnhancement"));
					const auto selected = std::clamp(bloom.value("SelectedPreset", 0), 0, 2);
					const auto* selectedName = selected == 1 ? "Fantasy" : selected == 2 ? "Dreamy" :
					                                                                       "Default";
					for (const auto* presetName : { "Default", "Fantasy", "Dreamy" }) {
						if (std::string_view(presetName) != selectedName && bloom.at(presetName) != projectedBloom.at(presetName)) {
							logger::warn("Rejected legacy utility update: only the selected Bloom profile can be changed");
							return;
						}
					}
					json profile = Bloom::GetPresetProfile(static_cast<uint>(selected));
					if (bloom.contains(selectedName))
						profile.merge_patch(bloom.at(selectedName));
					const auto enabled = bloom.contains("Enabled") ?
					                         (bloom.at("Enabled").is_boolean() ? bloom.at("Enabled").get<bool>() : bloom.at("Enabled").get<int>() != 0) :
					                         profile.value("EnhancementIntensity", 0.0f) > 0;
					if (enabled && profile.value("EnhancementIntensity", 0.0f) <= 0) {
						logger::warn("Rejected legacy utility update: enabling Bloom requires supplying or selecting a profile with positive EnhancementIntensity");
						return;
					}
					if (!enabled)
						profile["EnhancementIntensity"] = 0.0f;
					candidate["globalProfile"]["bloom"] = std::move(profile);
				}
				// Forwarded values retain the master and profile gates chosen by the user.
				balance.LoadSettings(candidate);
			} catch (const json::exception& error) {
				logger::warn("Rejected malformed legacy utility settings: {}", error.what());
			}
		}

		void SaveSettings(json& a_json) override
		{
			json canonical;
			globals::features::adaptiveBrightness.SaveSettings(canonical);
			a_json = ProjectSettings(canonical);
		}

		void RestoreDefaultSettings() override
		{
			AdaptiveBrightness defaults;
			json canonical;
			defaults.SaveSettings(canonical);
			auto legacy = ProjectSettings(canonical);
			LoadSettings(legacy);
		}

		static json ProjectSettings(const json& a_canonical)
		{
			json legacy = a_canonical.at("depthOfField");
			for (const auto& [legacyPath, destinationPath] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
				const json::json_pointer source{ std::string(destinationPath) };
				if (a_canonical.contains(source))
					legacy[json::json_pointer(std::string(legacyPath))] = a_canonical.at(source);
			}
			const auto& profile = a_canonical.at("globalProfile").at("bloom");
			legacy["bloomEnhancement"] = {
				{ "Enabled", profile.value("EnhancementIntensity", 0.0f) > 0 ? 1u : 0u },
				{ "SelectedPreset", 0u }, { "Default", profile },
				{ "Fantasy", Bloom::GetPresetProfile(1) }, { "Dreamy", Bloom::GetPresetProfile(2) }
			};
			return legacy;
		}

	private:
		std::string name;
	};
}

bool LegacyUtilityCompatibility::IsAlias(std::string_view a_name)
{
	return a_name == "CSUtility" || a_name == "OSUtility" ||
	       a_name == "CS Utility" || a_name == "OS Utility";
}

Feature* LegacyUtilityCompatibility::Find(std::string_view a_name, bool a_requireLoaded)
{
	if (!IsAlias(a_name))
		return nullptr;
	auto& balance = globals::features::adaptiveBrightness;
	if (a_requireLoaded && !balance.loaded)
		return nullptr;
	static LegacyUtilitySettings adapters[]{ LegacyUtilitySettings("CSUtility"), LegacyUtilitySettings("OSUtility"),
		LegacyUtilitySettings("CS Utility"), LegacyUtilitySettings("OS Utility") };
	for (auto& adapter : adapters) {
		if (adapter.GetShortName() == a_name) {
			adapter.loaded = balance.loaded;
			adapter.version = balance.version;
			return &adapter;
		}
	}
	return nullptr;
}
