#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "Features/AdaptiveBalanceDepthOfField.h"
#include "Features/AdaptiveBalanceDepthOfFieldSettingsPolicy.h"
#include "Features/AdaptiveBalanceGodraySettings.h"
#include "LegacyUtilityCompatibility.h"
#include "SettingsMigrations.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using json = nlohmann::json;
using uint = unsigned int;
namespace logger
{
	unsigned warnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
}

struct Feature
{
	bool loaded = false;
	std::string version;
	virtual ~Feature() = default;
	virtual std::string GetName() = 0;
	virtual std::string GetShortName() = 0;
	virtual std::string GetDisplayName() = 0;
	virtual bool IsInMenu() const = 0;
	virtual bool IsHiddenFromUserView() const = 0;
	virtual bool SupportsVR() = 0;
	virtual bool IsCore() const = 0;
	virtual void LoadSettings(json&) = 0;
	virtual void SaveSettings(json&) = 0;
	virtual void RestoreDefaultSettings() = 0;
};

struct Bloom
{
	static json GetPresetProfile(uint a_preset)
	{
		return { { "EnhancementIntensity", a_preset * 0.25f }, { "HaloRadius", 3.5f + a_preset },
			{ "HaloSpread", 0.85f }, { "BloomSaturation", 0.9f }, { "BloomTint", { 1.0, 0.98, 0.94 } },
			{ "CompressionThreshold", 0.0f }, { "CompressionCeiling", 1.5f } };
	}
};

json DefaultCanonicalSettings()
{
	json result;
	for (const auto& [legacyPath, destinationPath] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
		auto& value = result[json::json_pointer(std::string(destinationPath))];
		if (legacyPath == "/useAmbientEffectLighting")
			value = false;
		else if (legacyPath == "/water/parallaxQuality")
			value = 16;
		else
			value = legacyPath.ends_with("GammaOffset") || legacyPath == "/skyStaticTransparency" ? 0.0 : 1.0;
	}
	result["enabled"] = true;
	result["depthOfField"] = AdaptiveBalanceDepthOfField::Settings{};
	result["globalProfile"]["advanced"] = true;
	result["globalProfile"]["bloomAdvanced"] = true;
	result["globalProfile"]["waterAdvanced"] = true;
	result["globalProfile"]["bloom"] = Bloom::GetPresetProfile(0);
	result["globalProfile"]["appearance"] = { { "godrayIntensity", 1.0 } };
	result["globalProfile"]["waterWind"] = { { "enabled", false }, { "calmWaveMultiplier", 0.65 } };
	result["profiles"] = json::array({ { { "brightness", 1.0 } } });
	result["locationOverrides"] = json::array();
	return result;
}

struct AdaptiveBrightness
{
	bool loaded = true;
	std::string version = "7.3.2";
	json state = DefaultCanonicalSettings();
	unsigned loads = 0;
	void SaveSettings(json& a_json) { a_json = state; }
	void LoadSettings(json& a_json)
	{
		// The fixture substitutes engine ownership while keeping DOF deserialization real.
		const auto depth = a_json.at("depthOfField").get<AdaptiveBalanceDepthOfField::Settings>();
		json validated = a_json;
		validated["depthOfField"] = depth;
		state = std::move(validated);
		++loads;
	}
};
namespace globals::features
{
	AdaptiveBrightness adaptiveBrightness;
}
#include "legacy_utility_compatibility_under_test.h"

namespace
{
	auto& balance = globals::features::adaptiveBrightness;
	constexpr const char* aliases[]{ "CSUtility", "OSUtility", "CS Utility", "OS Utility" };

	Feature& Adapter(const char* a_alias = "OSUtility")
	{
		auto* adapter = LegacyUtilityCompatibility::Find(a_alias, true);
		assert(adapter);
		return *adapter;
	}

	void SeedProtectedSettings()
	{
		balance = {};
		balance.state["enabled"] = false;
		balance.state["globalProfile"]["advanced"] = false;
		balance.state["globalProfile"]["bloomAdvanced"] = false;
		balance.state["globalProfile"]["waterAdvanced"] = false;
		balance.state["globalProfile"]["appearance"]["godrayIntensity"] = 1.25;
		balance.state["globalProfile"]["waterWind"]["enabled"] = true;
		balance.state["profiles"][0]["brightness"] = 1.4;
		balance.state["locationOverrides"] = json::array({ { { "key", "test.esp|0x000123" } } });
	}

	void CheckAliases()
	{
		balance = {};
		for (const auto* alias : aliases) {
			assert(LegacyUtilityCompatibility::IsAlias(alias));
			auto& adapter = Adapter(alias);
			assert(adapter.GetName() == alias && adapter.GetShortName() == alias);
			assert(adapter.GetDisplayName() == "Adaptive Balance");
			assert(!adapter.IsInMenu() && adapter.IsHiddenFromUserView() && adapter.SupportsVR() && adapter.IsCore());
			assert(adapter.loaded && adapter.version == balance.version);
		}
		assert(!LegacyUtilityCompatibility::IsAlias("Utility") && !LegacyUtilityCompatibility::Find("Utility", false));
		balance.loaded = false;
		for (const auto* alias : aliases) {
			assert(!LegacyUtilityCompatibility::Find(alias, true));
			const auto* adapter = LegacyUtilityCompatibility::Find(alias, false);
			assert(adapter && !adapter->loaded);
		}
		balance = {};
	}

	void CheckEveryAppearanceRoute()
	{
		for (const auto* alias : aliases) {
			for (const auto& [legacyPath, destinationPath] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
				SeedProtectedSettings();
				auto& adapter = Adapter(alias);
				json projected;
				adapter.SaveSettings(projected);
				const json::json_pointer source{ std::string(legacyPath) };
				const json::json_pointer destination{ std::string(destinationPath) };
				assert(projected.at(source) == balance.state.at(destination));
				auto expected = balance.state;
				json newValue = legacyPath == "/useAmbientEffectLighting" ? json(true) :
				                legacyPath == "/water/parallaxQuality"    ? json(24) :
				                                                            json(0.75);
				projected[source] = newValue;
				expected[destination] = newValue;
				adapter.LoadSettings(projected);
				assert(balance.state == expected);
				json observed;
				Adapter("CSUtility").SaveSettings(observed);
				assert(observed.at(source) == newValue);
			}
		}
	}

	void CheckDofRouting()
	{
		SeedProtectedSettings();
		auto& adapter = Adapter();
		auto expected = balance.state;
		json scenePatch{ { "sceneDof", { { "locked", true }, { "values", { { "strength", 0.5 } } } } } };
		adapter.LoadSettings(scenePatch);
		expected["depthOfField"]["enabled"] = true;
		expected["depthOfField"]["sceneDof"]["locked"] = true;
		expected["depthOfField"]["sceneDof"]["values"]["strength"] = 0.5;
		assert(balance.state == expected);
		json disablePatch{ { "enabled", false }, { "fixUnderwaterFogDofBlur", true } };
		adapter.LoadSettings(disablePatch);
		expected["depthOfField"]["enabled"] = false;
		expected["depthOfField"]["fixUnderwaterFogDofBlur"] = true;
		assert(balance.state == expected);
		json explicitOff{ { "enabled", false }, { "underwaterDof", { { "locked", true } } } };
		adapter.LoadSettings(explicitOff);
		assert(!balance.state["depthOfField"]["enabled"].get<bool>());
		assert(balance.state["depthOfField"]["underwaterDof"]["locked"].get<bool>());
		assert(balance.state["depthOfField"]["fixUnderwaterFogDofBlur"].get<bool>());
		json underwaterOnly{ { "underwaterDof", { { "locked", true } } } };
		adapter.LoadSettings(underwaterOnly);
		assert(balance.state["depthOfField"]["enabled"].get<bool>());
		assert(!balance.state["enabled"].get<bool>());
	}

	void CheckBloomAndScopedDefaults()
	{
		SeedProtectedSettings();
		auto& adapter = Adapter();
		json projected;
		adapter.SaveSettings(projected);
		projected["bloomEnhancement"]["SelectedPreset"] = 2;
		projected["bloomEnhancement"]["Enabled"] = true;
		projected["bloomEnhancement"]["Dreamy"]["HaloRadius"] = 5.25;
		const auto protectedState = balance.state;
		adapter.LoadSettings(projected);
		auto expected = protectedState;
		expected["globalProfile"]["bloom"] = projected["bloomEnhancement"]["Dreamy"];
		assert(balance.state == expected);
		json partialBloom;
		partialBloom["bloomEnhancement"]["Default"]["HaloRadius"] = 4.25;
		adapter.LoadSettings(partialBloom);
		expected["globalProfile"]["bloom"]["HaloRadius"] = 4.25;
		assert(balance.state == expected);
		json roundTrip;
		adapter.SaveSettings(roundTrip);
		adapter.LoadSettings(roundTrip);
		assert(balance.state == expected);
		json off{ { "bloomEnhancement", { { "Enabled", 0 }, { "SelectedPreset", 1 } } } };
		adapter.LoadSettings(off);
		assert(balance.state["globalProfile"]["bloom"]["EnhancementIntensity"] == 0);
		assert(!balance.state["globalProfile"]["bloomAdvanced"].get<bool>());
		const auto beforeDefaults = balance.state;
		const auto defaults = DefaultCanonicalSettings();
		expected = beforeDefaults;
		for (const auto& [unused, destinationPath] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
			const json::json_pointer destination{ std::string(destinationPath) };
			expected[destination] = defaults.at(destination);
		}
		expected["depthOfField"] = defaults.at("depthOfField");
		expected["globalProfile"]["bloom"] = defaults.at("globalProfile").at("bloom");
		adapter.RestoreDefaultSettings();
		assert(balance.state == expected);
	}

	void CheckBloomUnsupportedTransitions()
	{
		for (const auto* alias : aliases) {
			SeedProtectedSettings();
			auto& adapter = Adapter(alias);
			balance.state["globalProfile"]["bloom"]["EnhancementIntensity"] = 0.65;
			auto expected = balance.state;
			json disable{ { "bloomEnhancement", { { "Enabled", false } } } };
			adapter.LoadSettings(disable);
			expected["globalProfile"]["bloom"]["EnhancementIntensity"] = 0.0;
			assert(balance.state == expected);

			const auto disabledLoads = balance.loads;
			const auto disabledWarnings = logger::warnings;
			json enableOnly{ { "bloomEnhancement", { { "Enabled", true } } } };
			adapter.LoadSettings(enableOnly);
			assert(balance.state == expected && balance.loads == disabledLoads);
			assert(logger::warnings > disabledWarnings);

			json explicitAmount;
			explicitAmount["bloomEnhancement"]["Enabled"] = true;
			explicitAmount["bloomEnhancement"]["Default"]["EnhancementIntensity"] = 0.45;
			adapter.LoadSettings(explicitAmount);
			expected["globalProfile"]["bloom"]["EnhancementIntensity"] = 0.45;
			assert(balance.state == expected && balance.loads == disabledLoads + 1);

			json projected;
			adapter.SaveSettings(projected);
			constexpr const char* presetNames[]{ "Default", "Fantasy", "Dreamy" };
			for (unsigned selected = 0; selected < 3; ++selected) {
				for (unsigned inactive = 0; inactive < 3; ++inactive) {
					if (selected == inactive)
						continue;
					json inactiveEdit{ { "skyBrightness", 0.3 } };
					inactiveEdit["bloomEnhancement"]["Enabled"] = true;
					inactiveEdit["bloomEnhancement"]["SelectedPreset"] = selected;
					inactiveEdit["bloomEnhancement"][presetNames[inactive]]["HaloRadius"] =
						projected["bloomEnhancement"][presetNames[inactive]]["HaloRadius"].get<double>() + 0.5;
					const auto loads = balance.loads;
					const auto warnings = logger::warnings;
					adapter.LoadSettings(inactiveEdit);
					assert(balance.state == expected && balance.loads == loads);
					assert(logger::warnings > warnings);
				}
			}

			projected["skyBrightness"] = 0.75;
			adapter.LoadSettings(projected);
			const auto skyDestination = std::find_if(SettingsMigrations::kLegacyUtilityAppearanceFields.begin(),
				SettingsMigrations::kLegacyUtilityAppearanceFields.end(),
				[](const auto& entry) { return entry.first == "/skyBrightness"; });
			assert(skyDestination != SettingsMigrations::kLegacyUtilityAppearanceFields.end());
			expected[json::json_pointer(std::string(skyDestination->second))] = 0.75;
			assert(balance.state == expected);
		}
	}

	void CheckFinalGodrayBrightnessRouting()
	{
		for (const auto* alias : aliases) {
			SeedProtectedSettings();
			auto& adapter = Adapter(alias);
			for (const double value : { 0.0, 1.0, 4.5, 5.0 }) {
				auto expected = balance.state;
				expected["godrayFinalBrightness"] = value;
				json patch{ { "vlIntensity", value } };
				adapter.LoadSettings(patch);
				assert(balance.state == expected);
				json observed;
				adapter.SaveSettings(observed);
				assert(observed.at("vlIntensity") == value);
			}
			for (const json& value : { json(-0.01), json(5.01), json(true), json(nullptr), json("1"),
					 json(std::numeric_limits<double>::infinity()), json(std::numeric_limits<double>::quiet_NaN()) }) {
				const auto before = balance.state;
				const auto loads = balance.loads;
				const auto warnings = logger::warnings;
				json patch{ { "vlIntensity", value }, { "skyBrightness", 0.3 } };
				adapter.LoadSettings(patch);
				assert(balance.state == before && balance.loads == loads);
				assert(logger::warnings > warnings);
			}
		}
	}

	void CheckMalformedAtomicity()
	{
		SeedProtectedSettings();
		auto& adapter = Adapter();
		const auto before = balance.state;
		const json invalidBloom{ { "skyBrightness", 0.3 }, { "bloomEnhancement", { { "SelectedPreset", "broken" } } } };
		const json invalidDof{ { "skyBrightness", 0.3 }, { "enabled", "broken" } };
		const json invalidNumber{ { "skyBrightness", std::numeric_limits<double>::infinity() } };
		const json unknown{ { "water", { { "unknownControl", 0.5 } } } };
		for (auto patch : { json(nullptr), json::array(), json(true), json{ { "sceneDof", true } }, invalidBloom, invalidDof, invalidNumber, unknown,
				 json{ { "skyBrightness", 1e100 } },
				 json{ { "skyBrightness", 0.3 }, { "sceneDof", { { "values", { { "mode", -1 } } } } } },
				 json{ { "skyBrightness", 0.3 }, { "sceneDof", { { "values", { { "blurRadius", 2.5 } } } } } },
				 json{ { "skyBrightness", 0.3 }, { "sceneDof", { { "values", { { "strength", 1.1 } } } } } } }) {
			const auto warnings = logger::warnings;
			adapter.LoadSettings(patch);
			assert(balance.state == before);
			assert(logger::warnings > warnings);
		}
	}
}

int main()
{
	CheckAliases();
	CheckEveryAppearanceRoute();
	CheckDofRouting();
	CheckBloomAndScopedDefaults();
	CheckBloomUnsupportedTransitions();
	CheckFinalGodrayBrightnessRouting();
	CheckMalformedAtomicity();
	std::cout << "Legacy utility aliases, 43 appearance routes, DOF, bloom and scoped defaults passed\n";
}
