#include "SettingsMigrations.h"

#include "Features/AdaptiveBalanceDepthOfFieldSettingsPolicy.h"
#include "Features/AdaptiveBalanceGodraySettings.h"
#include "Features/Bloom.h"
#include "Features/WaterAppearanceMigration.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

bool SettingsMigrations::MigrateCloudProfileSettings(nlohmann::json& a_profile)
{
	if (!a_profile.is_object())
		return false;

	constexpr std::array fields{
		std::pair{ "skyBrightnessMult", "cloudBrightnessMult" },
		std::pair{ "skySaturation", "cloudSaturation" },
		std::pair{ "skyGammaOffset", "cloudGammaOffset" }
	};
	bool migrated = false;
	for (const auto& [skyName, cloudName] : fields) {
		if (!a_profile.contains(cloudName)) {
			if (const auto sky = a_profile.find(skyName); sky != a_profile.end() && sky->is_number()) {
				a_profile[cloudName] = *sky;
				migrated = true;
			}
		}
	}
	return migrated;
}

bool SettingsMigrations::MigrateCloudSettingsLayer(nlohmann::json& a_settings)
{
	if (!a_settings.is_object())
		return false;

	bool migrated = false;
	if (auto global = a_settings.find("globalProfile"); global != a_settings.end())
		migrated |= MigrateCloudProfileSettings(*global);
	if (auto profiles = a_settings.find("profiles"); profiles != a_settings.end() && profiles->is_array()) {
		for (auto& profile : *profiles)
			migrated |= MigrateCloudProfileSettings(profile);
	}
	if (auto locations = a_settings.find("locationOverrides"); locations != a_settings.end() && locations->is_array()) {
		for (auto& location : *locations) {
			if (auto profile = location.find("profile"); profile != location.end())
				migrated |= MigrateCloudProfileSettings(*profile);
		}
	}

	// Preserve legacy global brightness before lower-priority cloud defaults merge.
	if (const auto lighting = a_settings.find("lighting"); lighting != a_settings.end() && lighting->is_object()) {
		if (const auto sky = lighting->find("skyBrightness"); sky != lighting->end() && sky->is_number()) {
			auto global = a_settings.find("globalProfile");
			if (global == a_settings.end()) {
				a_settings["globalProfile"] = nlohmann::json::object();
				global = a_settings.find("globalProfile");
			}
			if (global->is_object() && !global->contains("cloudBrightnessMult")) {
				(*global)["cloudBrightnessMult"] = *sky;
				migrated = true;
			}
		}
	}
	return migrated;
}

namespace
{
	using json = nlohmann::json;

	constexpr std::string_view kLegacyBloomKey = "bloomEnhancement";
	constexpr std::string_view kLegacyBloomMigratedKey = "_adaptiveBalanceMigrated";
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

		return std::ranges::any_of(SettingsMigrations::kLegacyCSUtilityLightingKeys, [&](std::string_view a_key) {
			return a_csUtility.contains(a_key.data());
		});
	}

	bool HasValidLegacyLightingSettings(const json& a_csUtility)
	{
		return std::ranges::any_of(SettingsMigrations::kLegacyCSUtilityLightingKeys, [&](std::string_view a_key) {
			const auto legacyIt = a_csUtility.find(a_key.data());
			return legacyIt != a_csUtility.end() && SettingsMigrations::IsFiniteUtilityNumber(*legacyIt);
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
					   (!schemaIt->is_number() || SettingsMigrations::IsFiniteUtilityNumber(fallbackValue)) &&
					   (targetIt == candidate.end() || !SettingsMigrations::MatchesJsonSchema(*targetIt, *schemaIt) ||
						   (schemaIt->is_number() && !SettingsMigrations::IsFiniteUtilityNumber(*targetIt)))) {
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

	bool MigrateLegacyUtilityAlias(json& a_layer)
	{
		auto alias = a_layer.find(SettingsMigrations::kOSUtilitySettingsName.data());
		if (alias == a_layer.end())
			return false;
		auto legacy = a_layer.find(SettingsMigrations::kCSUtilitySettingsName.data());
		if (legacy == a_layer.end() || !legacy->is_object()) {
			if (!alias->is_object())
				return false;
			a_layer[std::string(SettingsMigrations::kCSUtilitySettingsName)] = *alias;
		} else if (alias->is_object()) {
			MergeMissingObjectMembers(*legacy, *alias);
		}
		a_layer.erase(SettingsMigrations::kOSUtilitySettingsName.data());
		return true;
	}

	bool MigrateLegacyUtilityAppearanceRoot(json& a_layer)
	{
		auto legacy = a_layer.find(SettingsMigrations::kCSUtilitySettingsName.data());
		if (legacy == a_layer.end() || !legacy->is_object())
			return false;

		const auto canonical = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		json fallback = json::object();
		std::vector<json::json_pointer> consumed;
		bool hasLighting = false;
		for (const auto& [sourceName, destinationName] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
			const json::json_pointer sourcePath{ std::string(sourceName) };
			if (!legacy->contains(sourcePath))
				continue;
			const auto& value = legacy->at(sourcePath);
			const bool isBoolean = sourceName == "/useAmbientEffectLighting";
			if (isBoolean ? !value.is_boolean() : !SettingsMigrations::IsFiniteUtilityNumber(value))
				continue;
			if (sourceName == "/water/parallaxQuality" && !SettingsMigrations::IsUtilityUnsignedInteger(value))
				continue;
			if (sourceName == "/vlIntensity" && !AdaptiveBalanceGodray::IsValidFinalBrightness(value.get<double>()))
				continue;
			json migratedValue = value;
			if (canonical != a_layer.end() && canonical->is_object() &&
				std::ranges::find(SettingsMigrations::kLegacyCSUtilityLightingKeys, sourcePath.back()) != SettingsMigrations::kLegacyCSUtilityLightingKeys.end()) {
				const auto lighting = canonical->find(kLightingKey.data());
				if (lighting != canonical->end() && lighting->is_object()) {
					const auto existing = lighting->find(sourcePath.back());
					if (existing != lighting->end() && SettingsMigrations::IsFiniteUtilityNumber(*existing))
						migratedValue = *existing;
				}
			}
			fallback[json::json_pointer{ std::string(destinationName) }] = std::move(migratedValue);
			consumed.push_back(sourcePath);
			hasLighting |= destinationName.starts_with("/globalProfile/") && !sourceName.starts_with("/water/");
		}
		if (fallback.empty())
			return false;

		if (hasLighting) {
			const auto enabled = legacy->find("enabled");
			fallback["globalProfile"]["advanced"] = enabled != legacy->end() && enabled->is_boolean() ? *enabled : json(true);
			if (canonical != a_layer.end() && canonical->is_object()) {
				const auto existing = canonical->find(kGlobalLightingEnabledKey.data());
				if (existing != canonical->end() && existing->is_boolean())
					fallback["globalProfile"]["advanced"] = *existing;
			}
		}
		auto adaptive = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		json destination = adaptive != a_layer.end() ? *adaptive : json::object();
		if (fallback.contains("godrayFinalBrightness") && destination.is_object()) {
			const auto existing = destination.find("godrayFinalBrightness");
			if (existing != destination.end() && (!existing->is_number() || !AdaptiveBalanceGodray::IsValidFinalBrightness(existing->get<double>())))
				destination.erase(existing);
		}
		MergeValidFallback(destination, fallback, fallback);
		a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)] = std::move(destination);
		for (const auto& path : consumed) {
			if (path.parent_pointer().empty())
				legacy->erase(path.back());
			else
				legacy->at(path.parent_pointer()).erase(path.back());
		}
		if (const auto water = legacy->find("water"); water != legacy->end() && water->is_object() && water->empty())
			legacy->erase(water);
		if (legacy->empty())
			a_layer.erase(legacy);
		return true;
	}

	bool MigrateLegacyUtilityBloomRoot(json& a_layer)
	{
		const auto legacy = a_layer.find(SettingsMigrations::kCSUtilitySettingsName.data());
		if (legacy == a_layer.end() || !legacy->is_object())
			return false;
		const auto bloom = legacy->find(kLegacyBloomKey.data());
		if (bloom == legacy->end() || !bloom->is_object())
			return false;
		if (const auto migrated = bloom->find(kLegacyBloomMigratedKey.data());
			migrated != bloom->end() && migrated->is_boolean() && migrated->get<bool>())
			return false;
		const auto canonical = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		if (canonical != a_layer.end() && canonical->is_object()) {
			const auto existing = canonical->find(kLegacyBloomKey.data());
			if (existing != canonical->end() && existing->is_object())
				return false;
		}

		unsigned preset = 0;
		if (const auto selected = bloom->find("SelectedPreset"); selected != bloom->end() && selected->is_number_integer())
			preset = static_cast<unsigned>(std::clamp(selected->get<double>(), 0.0, 2.0));
		const char* name = preset == 1 ? "Fantasy" : preset == 2 ? "Dreamy" :
		                                                           "Default";
		const auto& defaults = GetBloomSchema().at(name);
		const auto profile = bloom->find(name);
		json selected = profile != bloom->end() && profile->is_object() ? *profile : json::object();
		MergeValidFallback(selected, defaults, defaults);
		const auto enabled = bloom->find("Enabled");
		const bool isEnabled = enabled != bloom->end() &&
		                       (enabled->is_boolean() ? enabled->get<bool>() : enabled->is_number() && enabled->get<double>() != 0.0);
		if (!isEnabled)
			selected["EnhancementIntensity"] = 0.0f;
		const json fallback = { { "globalProfile", { { "bloom", std::move(selected) } } } };
		auto adaptive = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		json destination = adaptive != a_layer.end() ? *adaptive : json::object();
		if (!MergeValidFallback(destination, fallback, fallback))
			return false;
		a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)] = std::move(destination);
		// Inactive presets have no canonical slot; retain their complete source blob.
		return true;
	}

	bool MigrateDepthOfFieldMembers(json& a_target, json& a_legacy, const json& a_schema)
	{
		if (!a_legacy.is_object())
			return false;
		using AdaptiveBalanceDepthOfFieldSettingsPolicy::IsValidValue;
		bool consumed = false;
		const auto& properties = a_schema.at("properties");
		for (auto source = a_legacy.begin(); source != a_legacy.end();) {
			const auto schema = properties.find(source.key());
			if (schema == properties.end()) {
				++source;
				continue;
			}
			if (schema->at("type") == "object") {
				json child = a_target.is_object() && a_target.contains(source.key()) ? a_target.at(source.key()) : json::object();
				if (MigrateDepthOfFieldMembers(child, *source, *schema)) {
					if (!a_target.is_object())
						a_target = json::object();
					a_target[source.key()] = std::move(child);
					consumed = true;
					if (source->empty()) {
						source = a_legacy.erase(source);
						continue;
					}
				}
			} else if (IsValidValue(*source, *schema)) {
				if (!a_target.is_object())
					a_target = json::object();
				const auto destination = a_target.find(source.key());
				if (destination == a_target.end() || !IsValidValue(*destination, *schema))
					a_target[source.key()] = *source;
				source = a_legacy.erase(source);
				consumed = true;
				continue;
			}
			++source;
		}
		return consumed;
	}

	bool MigrateLegacyUtilityBootState(json& a_layer)
	{
		const auto boot = a_layer.find("Disable at Boot");
		if (boot == a_layer.end() || !boot->is_object())
			return false;
		bool disabled = false;
		for (const auto alias : SettingsMigrations::kLegacyUtilityFeatureNames) {
			if (const auto value = boot->find(alias.data()); value != boot->end() && value->is_boolean())
				disabled = value->get<bool>();
		}
		if (!disabled)
			return false;
		const auto adaptive = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		json target = json::object();
		if (adaptive != a_layer.end() && adaptive->is_object()) {
			if (const auto depth = adaptive->find(SettingsMigrations::kDepthOfFieldSettingsKey.data()); depth != adaptive->end() && depth->is_object())
				target = *depth;
		}
		bool changed = false;
		for (const auto* flag : { "enabled", "fixUnderwaterFogDofBlur" }) {
			if (!target.contains(flag) || !target.at(flag).is_boolean()) {
				target[flag] = false;
				changed = true;
			}
		}
		if (changed) {
			if (adaptive == a_layer.end() || !adaptive->is_object())
				a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)] = json::object();
			a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)][std::string(SettingsMigrations::kDepthOfFieldSettingsKey)] = std::move(target);
		}
		return changed;
	}

	bool MigrateLegacyDepthOfFieldRoot(json& a_layer)
	{
		auto legacy = a_layer.find(SettingsMigrations::kCSUtilitySettingsName.data());
		if (legacy == a_layer.end() || !legacy->is_object())
			return false;

		bool inferEnabled = false;
		if (!legacy->contains("enabled")) {
			for (const auto* scope : { "sceneDof", "underwaterDof" }) {
				const auto dof = legacy->find(scope);
				if (dof != legacy->end() && dof->is_object()) {
					const auto locked = dof->find("locked");
					inferEnabled |= locked != dof->end() && locked->is_boolean() && locked->get<bool>();
				}
			}
		}
		auto adaptive = a_layer.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
		json target = json::object();
		if (adaptive != a_layer.end() && adaptive->is_object()) {
			if (const auto depth = adaptive->find(SettingsMigrations::kDepthOfFieldSettingsKey.data()); depth != adaptive->end())
				target = *depth;
		}
		static const auto schema = AdaptiveBalanceDepthOfFieldSettingsPolicy::Schema();
		if (!MigrateDepthOfFieldMembers(target, *legacy, schema))
			return false;
		if (inferEnabled && (!target.contains("enabled") || !target.at("enabled").is_boolean()))
			target["enabled"] = true;

		// Only supported valid leaves are consumed; malformed and future fields remain recoverable.
		if (adaptive == a_layer.end() || !adaptive->is_object())
			a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)] = json::object();
		a_layer[std::string(SettingsMigrations::kAdaptiveBalanceSettingsName)][std::string(SettingsMigrations::kDepthOfFieldSettingsKey)] = std::move(target);
		if (legacy->empty())
			a_layer.erase(legacy);
		return true;
	}

}

bool SettingsMigrations::MigrateAdaptiveBalanceRootLayer(
	nlohmann::json& a_layer,
	bool a_forceLegacyWaterAppearance)
{
	if (!a_layer.is_object())
		return false;

	auto migratedLayer = a_layer;
	bool migrated = MigrateLegacyUtilityAlias(migratedLayer);
	migrated |= MigrateLegacyUtilityBootState(migratedLayer);
	migrated |= MigrateLegacyAdaptiveBrightnessRoot(migratedLayer);
	migrated |= MigrateLegacyUnifiedWaterAppearanceRoot(migratedLayer, a_forceLegacyWaterAppearance);
	migrated |= MigrateLegacyUtilityAppearanceRoot(migratedLayer);
	migrated |= MigrateLegacyUtilityBloomRoot(migratedLayer);
	if (auto adaptive = migratedLayer.find(kAdaptiveBalanceSettingsName.data()); adaptive != migratedLayer.end())
		migrated |= MigrateCloudSettingsLayer(*adaptive);

	const auto finalizeLayer = [&]() {
		migrated |= MigrateLegacyDepthOfFieldRoot(migratedLayer);
		if (migrated)
			a_layer = std::move(migratedLayer);
		return migrated;
	};

	const auto sourceCSUtilityIt = migratedLayer.find(kCSUtilitySettingsName.data());
	if (sourceCSUtilityIt == migratedLayer.end() ||
		!sourceCSUtilityIt->is_object() ||
		!HasLegacyRendererSettings(*sourceCSUtilityIt)) {
		return finalizeLayer();
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
			return finalizeLayer();
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
				if (legacyIt == csUtilityIt->end() || !IsFiniteUtilityNumber(*legacyIt))
					continue;

				auto targetIt = lightingIt->find(key.data());
				if (targetIt != lightingIt->end() && IsFiniteUtilityNumber(*targetIt)) {
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

	migrated |= MigrateCloudSettingsLayer(*adaptiveIt);
	return finalizeLayer();
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

	const auto legacyIt = rootLayer.find(kCSUtilitySettingsName.data());
	a_csUtilityLayer = legacyIt != rootLayer.end() ? std::move(*legacyIt) : json::object();
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

void SettingsMigrations::RetireAdaptiveBalanceFeaturePatch(nlohmann::json& a_legacyLayer)
{
	const auto patch = ExtractAdaptiveBalanceFeaturePatch(a_legacyLayer);
	if (patch.contains(json::json_pointer("/globalProfile/bloom"))) {
		if (auto bloom = a_legacyLayer.find(kLegacyBloomKey.data()); bloom != a_legacyLayer.end() && bloom->is_object())
			(*bloom)[std::string(kLegacyBloomMigratedKey)] = true;
	}
}
