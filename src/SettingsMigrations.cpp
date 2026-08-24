#include "SettingsMigrations.h"

#include "Features/Bloom.h"
#include "Features/WaterAppearanceMigration.h"

#include <algorithm>
#include <string>
#include <utility>

bool SettingsMigrations::MatchesJsonSchema(const nlohmann::json& a_value, const nlohmann::json& a_schema)
{
	if (a_schema.is_number())
		return a_value.is_number();
	if (a_schema.is_array()) {
		if (!a_value.is_array() || a_value.size() != a_schema.size())
			return false;
		for (std::size_t index = 0; index < a_schema.size(); ++index) {
			if (!MatchesJsonSchema(a_value[index], a_schema[index]))
				return false;
		}
		return true;
	}
	return a_value.type() == a_schema.type();
}

bool SettingsMigrations::HasLegacyUnifiedWaterAppearanceValues(const nlohmann::json& a_value)
{
	return a_value.is_object() && std::ranges::any_of(
									  kLegacyUnifiedWaterAppearanceKeys,
									  [&](std::string_view a_key) {
										  const auto valueIt = a_value.find(a_key.data());
										  return valueIt != a_value.end() && valueIt->is_number();
									  });
}

namespace
{
	using json = nlohmann::json;

	constexpr std::string_view kLegacyBloomKey = "bloomEnhancement";
	constexpr std::string_view kGlobalLightingEnabledKey = "globalLightingEnabled";
	constexpr std::string_view kLightingKey = "lighting";
	constexpr std::string_view kEnabledKey = "enabled";
	bool HasExplicitWaterProfile(const json& a_adaptiveBalance)
	{
		if (!a_adaptiveBalance.is_object())
			return false;

		const auto hasProfileWater = [](const json& a_profile) {
			if (!a_profile.is_object())
				return false;
			const auto waterIt = a_profile.find("water");
			return waterIt != a_profile.end() && waterIt->is_object();
		};

		if (const auto profilesIt = a_adaptiveBalance.find("profiles"); profilesIt != a_adaptiveBalance.end() && profilesIt->is_array()) {
			if (std::ranges::any_of(*profilesIt, hasProfileWater))
				return true;
		}

		if (const auto overridesIt = a_adaptiveBalance.find("locationOverrides"); overridesIt != a_adaptiveBalance.end() && overridesIt->is_array()) {
			return std::ranges::any_of(*overridesIt, [&](const json& a_locationOverride) {
				if (!a_locationOverride.is_object())
					return false;
				const auto profileIt = a_locationOverride.find("profile");
				return profileIt != a_locationOverride.end() && hasProfileWater(*profileIt);
			});
		}

		return false;
	}

	bool HasLegacyRendererSettings(const json& a_csUtility)
	{
		if (!a_csUtility.is_object())
			return false;

		return a_csUtility.contains(kLegacyBloomKey.data()) ||
		       std::ranges::any_of(SettingsMigrations::kLegacyCSUtilityLightingKeys, [&](std::string_view a_key) {
				   return a_csUtility.contains(a_key.data());
			   });
	}

	bool HasValidLegacyLightingSettings(const json& a_csUtility)
	{
		return std::ranges::any_of(SettingsMigrations::kLegacyCSUtilityLightingKeys, [&](std::string_view a_key) {
			const auto legacyIt = a_csUtility.find(a_key.data());
			return legacyIt != a_csUtility.end() && legacyIt->is_number();
		});
	}

	bool MergeValidFallback(json& a_target, const json& a_fallback, const json& a_schema)
	{
		if (!a_fallback.is_object() || !a_schema.is_object())
			return false;

		json candidate = a_target.is_object() ? a_target : json::object();
		bool changed = false;

		for (const auto& [key, fallbackValue] : a_fallback.items()) {
			auto targetIt = candidate.find(key);
			const auto schemaIt = a_schema.find(key);
			if (schemaIt == a_schema.end()) {
				// Preserve forward-compatible fields opaquely, while still giving an
				// explicit destination value precedence.
				if (targetIt == candidate.end()) {
					candidate[key] = fallbackValue;
					changed = true;
				}
				continue;
			}

			if (schemaIt->is_object()) {
				if (!fallbackValue.is_object())
					continue;
				json child = targetIt != candidate.end() ? *targetIt : json::object();
				if (MergeValidFallback(child, fallbackValue, *schemaIt)) {
					candidate[key] = std::move(child);
					changed = true;
				}
			} else if (SettingsMigrations::MatchesJsonSchema(fallbackValue, *schemaIt) &&
					   (targetIt == candidate.end() || !SettingsMigrations::MatchesJsonSchema(*targetIt, *schemaIt))) {
				// Only a schema-valid legacy value may repair a missing or malformed
				// destination. A valid explicit new value always wins.
				candidate[key] = fallbackValue;
				changed = true;
			}
		}

		if (changed)
			a_target = std::move(candidate);
		return changed;
	}

	bool MergeMissingObjectMembers(json& a_target, const json& a_fallback)
	{
		if (!a_target.is_object() || !a_fallback.is_object())
			return false;

		bool changed = false;
		for (const auto& [key, fallbackValue] : a_fallback.items()) {
			auto targetIt = a_target.find(key);
			if (targetIt == a_target.end()) {
				a_target[key] = fallbackValue;
				changed = true;
			} else if (targetIt->is_object() && fallbackValue.is_object()) {
				changed |= MergeMissingObjectMembers(*targetIt, fallbackValue);
			}
		}
		return changed;
	}

	bool MigrateLegacyAdaptiveBrightnessRoot(json& a_layer)
	{
		auto legacyIt = a_layer.find(SettingsMigrations::kLegacyAdaptiveBrightnessSettingsName.data());
		if (legacyIt == a_layer.end())
			return false;

		auto adaptiveIt = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		if (adaptiveIt == a_layer.end()) {
			// A malformed legacy section previously loaded defaults. Preserve that
			// behavior with an empty canonical object instead of retaining both names.
			a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)] =
				legacyIt->is_object() ? *legacyIt : json::object();
		} else if (adaptiveIt->is_object() && legacyIt->is_object()) {
			// During the transition, settings explicitly written under the canonical
			// name win while the old section supplies any still-missing members.
			MergeMissingObjectMembers(*adaptiveIt, *legacyIt);
		}

		a_layer.erase(SettingsMigrations::kLegacyAdaptiveBrightnessSettingsName.data());
		return true;
	}

	bool MigrateLegacyUnifiedWaterAppearanceRoot(
		json& a_layer,
		bool a_forceLegacyWaterAppearance)
	{
		auto unifiedWaterIt = a_layer.find(SettingsMigrations::kUnifiedWaterSettingsName.data());
		if (unifiedWaterIt == a_layer.end() || !unifiedWaterIt->is_object())
			return false;

		if (!WaterAppearanceMigration::ContainsAnyKey(
				*unifiedWaterIt,
				SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys))
			return false;

		auto adaptiveIt = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		if (adaptiveIt == a_layer.end()) {
			a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)] = json::object();
			adaptiveIt = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		} else if (!adaptiveIt->is_object()) {
			// A malformed destination could not have represented valid explicit
			// profile settings. Recover it so the valid legacy water values survive.
			*adaptiveIt = json::object();
		}

		const bool forceGlobal = a_forceLegacyWaterAppearance && !HasExplicitWaterProfile(*adaptiveIt);

		auto legacyWaterIt = adaptiveIt->find(SettingsMigrations::kLegacyWaterAppearanceSettingsKey.data());
		if (legacyWaterIt == adaptiveIt->end() || !legacyWaterIt->is_object()) {
			(*adaptiveIt)[std::string(SettingsMigrations::kLegacyWaterAppearanceSettingsKey)] = json::object();
			legacyWaterIt = adaptiveIt->find(SettingsMigrations::kLegacyWaterAppearanceSettingsKey.data());
		}
		return WaterAppearanceMigration::MoveValues(
			*unifiedWaterIt,
			*legacyWaterIt,
			SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey,
			forceGlobal,
			SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys);
	}

	const json& GetBloomSchema()
	{
		static const json schema = {
			{ "Enabled", 0u },
			{ "SelectedPreset", 0u },
			{ "Default", Bloom::GetPresetProfile(0) },
			{ "Fantasy", Bloom::GetPresetProfile(1) },
			{ "Dreamy", Bloom::GetPresetProfile(2) },
		};
		return schema;
	}

	bool HasRecoverableLegacyRendererSettings(const json& a_csUtility)
	{
		const auto enabledIt = a_csUtility.find(kEnabledKey.data());
		if (enabledIt != a_csUtility.end() && enabledIt->is_boolean())
			return true;

		if (HasValidLegacyLightingSettings(a_csUtility))
			return true;

		const auto bloomIt = a_csUtility.find(kLegacyBloomKey.data());
		if (bloomIt == a_csUtility.end() || !bloomIt->is_object())
			return false;
		json migratedBloom = json::object();
		return MergeValidFallback(migratedBloom, *bloomIt, GetBloomSchema());
	}
}

bool SettingsMigrations::MigrateAdaptiveBalanceRootLayer(
	nlohmann::json& a_layer,
	bool a_forceLegacyWaterAppearance)
{
	if (!a_layer.is_object())
		return false;

	auto migratedLayer = a_layer;
	bool migrated = MigrateLegacyAdaptiveBrightnessRoot(migratedLayer);
	migrated |= MigrateLegacyUnifiedWaterAppearanceRoot(migratedLayer, a_forceLegacyWaterAppearance);

	const auto sourceCSUtilityIt = migratedLayer.find(kCSUtilitySettingsName.data());
	if (sourceCSUtilityIt == migratedLayer.end() ||
		!sourceCSUtilityIt->is_object() ||
		!HasLegacyRendererSettings(*sourceCSUtilityIt)) {
		if (migrated)
			a_layer = std::move(migratedLayer);
		return migrated;
	}

	// Work on the candidate so a layer is never partially rewritten when its
	// legacy CS Utility renderer data is present but unusable.
	auto csUtilityIt = migratedLayer.find(kCSUtilitySettingsName.data());

	auto adaptiveIt = migratedLayer.find(kAdaptiveBalanceSettingsName.data());
	if (adaptiveIt == migratedLayer.end()) {
		migratedLayer[std::string(kAdaptiveBalanceSettingsName)] = json::object();
		adaptiveIt = migratedLayer.find(kAdaptiveBalanceSettingsName.data());
	} else if (!adaptiveIt->is_object()) {
		if (!HasRecoverableLegacyRendererSettings(*csUtilityIt)) {
			if (migrated)
				a_layer = std::move(migratedLayer);
			return migrated;
		}
		// A malformed destination cannot represent any valid explicit setting.
		// Recover into a clean object when the legacy layer contains usable data.
		*adaptiveIt = json::object();
	}

	const auto enabledIt = csUtilityIt->find(kEnabledKey.data());
	if (enabledIt != csUtilityIt->end() && enabledIt->is_boolean()) {
		auto rendererEnabledIt = adaptiveIt->find(kGlobalLightingEnabledKey.data());
		if (rendererEnabledIt == adaptiveIt->end() || !rendererEnabledIt->is_boolean()) {
			(*adaptiveIt)[std::string(kGlobalLightingEnabledKey)] = *enabledIt;
			migrated = true;
		}
	}

	const bool hasLegacyLighting = std::ranges::any_of(kLegacyCSUtilityLightingKeys, [&](std::string_view a_key) {
		return csUtilityIt->contains(a_key.data());
	});
	if (hasLegacyLighting) {
		auto lightingIt = adaptiveIt->find(kLightingKey.data());
		const bool hasValidLegacyLighting = HasValidLegacyLightingSettings(*csUtilityIt);
		if (hasValidLegacyLighting && (lightingIt == adaptiveIt->end() || !lightingIt->is_object())) {
			(*adaptiveIt)[std::string(kLightingKey)] = json::object();
			lightingIt = adaptiveIt->find(kLightingKey.data());
		}

		if (lightingIt != adaptiveIt->end() && lightingIt->is_object()) {
			for (const auto key : kLegacyCSUtilityLightingKeys) {
				auto legacyIt = csUtilityIt->find(key.data());
				if (legacyIt == csUtilityIt->end())
					continue;

				auto targetIt = lightingIt->find(key.data());
				if (targetIt != lightingIt->end() && targetIt->is_number()) {
					// A valid explicit destination value wins within this source.
					csUtilityIt->erase(legacyIt);
					migrated = true;
				} else if (legacyIt->is_number()) {
					(*lightingIt)[std::string(key)] = *legacyIt;
					csUtilityIt->erase(legacyIt);
					migrated = true;
				}
			}
		}
	}

	auto legacyBloomIt = csUtilityIt->find(kLegacyBloomKey.data());
	if (legacyBloomIt != csUtilityIt->end()) {
		auto adaptiveBloomIt = adaptiveIt->find(kLegacyBloomKey.data());
		if (legacyBloomIt->is_object()) {
			const bool hasValidDestination = adaptiveBloomIt != adaptiveIt->end() && adaptiveBloomIt->is_object();
			json mergedBloom = hasValidDestination ? *adaptiveBloomIt : json::object();
			const bool repaired = MergeValidFallback(mergedBloom, *legacyBloomIt, GetBloomSchema());
			if (hasValidDestination || repaired) {
				if (repaired)
					(*adaptiveIt)[std::string(kLegacyBloomKey)] = std::move(mergedBloom);
				csUtilityIt->erase(legacyBloomIt);
				migrated = true;
			}
		} else if (adaptiveBloomIt != adaptiveIt->end() && adaptiveBloomIt->is_object()) {
			// A valid destination Bloom object supersedes an unusable legacy one.
			csUtilityIt->erase(legacyBloomIt);
			migrated = true;
		}
	}

	if (migrated)
		a_layer = std::move(migratedLayer);
	return migrated;
}

bool SettingsMigrations::MarkExplicitAdaptiveBalanceWaterProfiles(nlohmann::json& a_adaptiveBalanceLayer)
{
	if (!a_adaptiveBalanceLayer.is_object())
		return false;

	bool marked = false;
	const auto markProfile = [&](json& a_profile) {
		if (!a_profile.is_object())
			return;
		auto waterIt = a_profile.find("water");
		if (waterIt == a_profile.end() || !waterIt->is_object())
			return;
		if (!waterIt->contains(kLegacyWaterProfileExplicitKey.data()) || !(*waterIt)[kLegacyWaterProfileExplicitKey.data()].is_boolean() || !(*waterIt)[kLegacyWaterProfileExplicitKey.data()].get<bool>()) {
			(*waterIt)[std::string(kLegacyWaterProfileExplicitKey)] = true;
			marked = true;
		}
	};

	if (auto profilesIt = a_adaptiveBalanceLayer.find("profiles"); profilesIt != a_adaptiveBalanceLayer.end() && profilesIt->is_array()) {
		for (auto& profile : *profilesIt)
			markProfile(profile);
	}
	if (auto overridesIt = a_adaptiveBalanceLayer.find("locationOverrides"); overridesIt != a_adaptiveBalanceLayer.end() && overridesIt->is_array()) {
		for (auto& locationOverride : *overridesIt) {
			if (!locationOverride.is_object())
				continue;
			if (auto profileIt = locationOverride.find("profile"); profileIt != locationOverride.end())
				markProfile(*profileIt);
		}
	}

	return marked;
}

bool SettingsMigrations::HasForcedLegacyWaterAppearance(const nlohmann::json& a_adaptiveBalanceLayer)
{
	if (!a_adaptiveBalanceLayer.is_object())
		return false;

	const auto waterIt = a_adaptiveBalanceLayer.find(kLegacyWaterAppearanceSettingsKey.data());
	if (waterIt == a_adaptiveBalanceLayer.end() || !HasLegacyUnifiedWaterAppearanceValues(*waterIt))
		return false;
	const auto forceIt = waterIt->find(kLegacyWaterAppearanceForceGlobalKey.data());
	return forceIt != waterIt->end() && forceIt->is_boolean() && forceIt->get<bool>();
}

void SettingsMigrations::ClearExplicitAdaptiveBalanceWaterProfiles(nlohmann::json& a_adaptiveBalanceLayer)
{
	if (!a_adaptiveBalanceLayer.is_object())
		return;

	const auto clearProfile = [](json& a_profile) {
		if (!a_profile.is_object())
			return;
		if (auto waterIt = a_profile.find("water"); waterIt != a_profile.end() && waterIt->is_object())
			waterIt->erase(kLegacyWaterProfileExplicitKey.data());
	};

	if (auto profilesIt = a_adaptiveBalanceLayer.find("profiles"); profilesIt != a_adaptiveBalanceLayer.end() && profilesIt->is_array()) {
		for (auto& profile : *profilesIt)
			clearProfile(profile);
	}
	if (auto overridesIt = a_adaptiveBalanceLayer.find("locationOverrides"); overridesIt != a_adaptiveBalanceLayer.end() && overridesIt->is_array()) {
		for (auto& locationOverride : *overridesIt) {
			if (!locationOverride.is_object())
				continue;
			if (auto profileIt = locationOverride.find("profile"); profileIt != locationOverride.end())
				clearProfile(*profileIt);
		}
	}
}

nlohmann::json SettingsMigrations::ExtractAdaptiveBalanceFeaturePatch(nlohmann::json& a_csUtilityLayer)
{
	if (!a_csUtilityLayer.is_object())
		return json::object();

	json rootLayer = json::object();
	rootLayer[std::string(kCSUtilitySettingsName)] = a_csUtilityLayer;
	if (!MigrateAdaptiveBalanceRootLayer(rootLayer))
		return json::object();

	a_csUtilityLayer = std::move(rootLayer[std::string(kCSUtilitySettingsName)]);
	auto adaptiveIt = rootLayer.find(kAdaptiveBalanceSettingsName.data());
	return adaptiveIt != rootLayer.end() && adaptiveIt->is_object() ? std::move(*adaptiveIt) : json::object();
}

nlohmann::json SettingsMigrations::ExtractAdaptiveBalanceWaterFeaturePatch(nlohmann::json& a_unifiedWaterLayer)
{
	if (!a_unifiedWaterLayer.is_object())
		return json::object();

	json rootLayer = json::object();
	rootLayer[std::string(kUnifiedWaterSettingsName)] = a_unifiedWaterLayer;
	if (!MigrateAdaptiveBalanceRootLayer(rootLayer, true))
		return json::object();

	a_unifiedWaterLayer = std::move(rootLayer[std::string(kUnifiedWaterSettingsName)]);
	auto adaptiveIt = rootLayer.find(kAdaptiveBalanceSettingsName.data());
	return adaptiveIt != rootLayer.end() && adaptiveIt->is_object() ? std::move(*adaptiveIt) : json::object();
}
