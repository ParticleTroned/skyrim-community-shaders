#include "Features/WaterAppearanceFallbackPolicy.h"
#include "SettingsMigrations.h"

namespace
{
	using WaterAppearanceFallbackPolicy::EffectiveSource;

	constexpr bool CoversRuntimeSourceSelection()
	{
		return WaterAppearanceFallbackPolicy::SelectEffectiveSource(false) == EffectiveSource::UnifiedWaterFallback &&
		       WaterAppearanceFallbackPolicy::SelectEffectiveSource(true) == EffectiveSource::AdaptiveBalance;
	}

	bool CoversLegacyCanonicalizationAndPrecedence()
	{
		nlohmann::json unifiedWater{
			{ std::string(SettingsMigrations::kUnifiedWaterAppearanceFallbackKey),
				{ { "WaterBrightness", 0.5 }, { "FresnelMax", "malformed" } } },
			{ "WaterBrightness", 1.25 },
			{ "FresnelMin", 0.2 },
			{ "Muddiness", "malformed" }
		};
		nlohmann::json adaptiveDestination{
			{ "WaterBrightness", 0.75 },
			{ "FresnelMin", "malformed" },
			{ std::string(SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey), true }
		};

		if (!WaterAppearanceFallbackPolicy::MigrateAppearanceValues(
				unifiedWater,
				adaptiveDestination,
				SettingsMigrations::kUnifiedWaterAppearanceFallbackKey,
				SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey,
				false,
				SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys))
			return false;

		const auto fallbackIt = unifiedWater.find(SettingsMigrations::kUnifiedWaterAppearanceFallbackKey.data());
		if (fallbackIt == unifiedWater.end() || !fallbackIt->is_object())
			return false;

		// A valid Adaptive Balance value wins, malformed destinations are repaired,
		// and old top-level values become a named fallback snapshot.
		if (adaptiveDestination["WaterBrightness"] != 0.75 ||
			adaptiveDestination["FresnelMin"] != 0.2 ||
			adaptiveDestination.contains("Muddiness") ||
			adaptiveDestination[SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey.data()] != false ||
			(*fallbackIt)["WaterBrightness"] != 1.25 ||
			(*fallbackIt)["FresnelMin"] != 0.2 ||
			fallbackIt->contains("FresnelMax") ||
			fallbackIt->contains("Muddiness") ||
			unifiedWater.contains("WaterBrightness") ||
			unifiedWater.contains("FresnelMin") ||
			unifiedWater.contains("Muddiness"))
			return false;

		// The canonical source survives a settings round trip and a second migration
		// is a true no-op.
		unifiedWater = nlohmann::json::parse(unifiedWater.dump());
		return !WaterAppearanceFallbackPolicy::MigrateAppearanceValues(
			unifiedWater,
			adaptiveDestination,
			SettingsMigrations::kUnifiedWaterAppearanceFallbackKey,
			SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey,
			false,
			SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys);
	}

	bool CoversForceGlobalOnlyChange()
	{
		nlohmann::json rootLayer{
			{ std::string(SettingsMigrations::kUnifiedWaterSettingsName),
				{ { std::string(SettingsMigrations::kUnifiedWaterAppearanceFallbackKey),
					{ { "WaterBrightness", 1.25 } } } } },
			{ std::string(SettingsMigrations::kAdaptiveBalanceSettingsName),
				{ { std::string(SettingsMigrations::kLegacyWaterAppearanceSettingsKey),
					{ { "WaterBrightness", 0.75 },
						{ std::string(SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey), false } } } } }
		};

		// Even when every appearance value already has valid precedence, changing
		// forceGlobal must commit the outer root-layer transaction.
		if (!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(rootLayer, true))
			return false;

		const auto& adaptiveDestination =
			rootLayer[SettingsMigrations::kAdaptiveBalanceSettingsName.data()]
					 [SettingsMigrations::kLegacyWaterAppearanceSettingsKey.data()];
		if (adaptiveDestination["WaterBrightness"] != 0.75 ||
			adaptiveDestination[SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey.data()] != true)
			return false;

		return !SettingsMigrations::MigrateAdaptiveBalanceRootLayer(rootLayer, true);
	}

	bool CoversFeatureMigrationAndSerialization()
	{
		nlohmann::json unifiedWater{
			{ "Enabled", true },
			{ "GlobalReflectionAmount", 0.6 },
			{ "RefractionAmount", 0.7 }
		};

		auto adaptivePatch = SettingsMigrations::ExtractAdaptiveBalanceWaterFeaturePatch(unifiedWater);
		const auto fallbackIt = unifiedWater.find(SettingsMigrations::kUnifiedWaterAppearanceFallbackKey.data());
		const auto waterIt = adaptivePatch.find(SettingsMigrations::kLegacyWaterAppearanceSettingsKey.data());
		if (fallbackIt == unifiedWater.end() || !fallbackIt->is_object() ||
			waterIt == adaptivePatch.end() || !waterIt->is_object() ||
			!unifiedWater.value("Enabled", false) ||
			unifiedWater.contains("GlobalReflectionAmount") ||
			unifiedWater.contains("RefractionAmount") ||
			(*fallbackIt)["GlobalReflectionAmount"] != 0.6 ||
			(*fallbackIt)["RefractionAmount"] != 0.7 ||
			(*waterIt)["GlobalReflectionAmount"] != 0.6 ||
			(*waterIt)["RefractionAmount"] != 0.7 ||
			(*waterIt)[SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey.data()] != true)
			return false;

		// Exercise the actual persisted JSON boundary and prove the canonical feature
		// layer produces the same patch after a settings round trip.
		unifiedWater = nlohmann::json::parse(unifiedWater.dump());
		adaptivePatch = nlohmann::json::parse(adaptivePatch.dump());
		const auto secondPatch = SettingsMigrations::ExtractAdaptiveBalanceWaterFeaturePatch(unifiedWater);
		return secondPatch == adaptivePatch;
	}

	static_assert(CoversRuntimeSourceSelection());
}

int main()
{
	return CoversRuntimeSourceSelection() &&
	               CoversLegacyCanonicalizationAndPrecedence() &&
	               CoversForceGlobalOnlyChange() &&
	               CoversFeatureMigrationAndSerialization() ?
	           0 :
	           1;
}
