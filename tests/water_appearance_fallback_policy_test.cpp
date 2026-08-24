#include "Features/WaterAppearanceFallbackPolicy.h"
#include "SettingsMigrations.h"

namespace
{
	using WaterAppearanceFallbackPolicy::EffectiveSource;

	constexpr bool CoversRuntimeSourceSelection()
	{
		return WaterAppearanceFallbackPolicy::SelectEffectiveSource(false) == EffectiveSource::UnifiedWater &&
		       WaterAppearanceFallbackPolicy::SelectEffectiveSource(true) == EffectiveSource::AdaptiveBalance;
	}

	bool CoversRetainedMigrationFallback()
	{
		nlohmann::json unifiedWater{
			{ "WaterBrightness", 1.25 },
			{ "FresnelMin", 0.2 },
			{ "Muddiness", "malformed" }
		};
		nlohmann::json adaptiveDestination{
			{ "WaterBrightness", 0.75 },
			{ "FresnelMin", "malformed" }
		};

		if (!WaterAppearanceFallbackPolicy::MirrorAppearanceValues(
				unifiedWater,
				adaptiveDestination,
				SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys))
			return false;

		// A valid explicit profile value wins; malformed or absent destinations
		// are repaired, and the source remains available for runtime fallback.
		if (adaptiveDestination["WaterBrightness"] != 0.75 ||
			adaptiveDestination["FresnelMin"] != 0.2 ||
			adaptiveDestination.contains("Muddiness") ||
			unifiedWater["WaterBrightness"] != 1.25 ||
			unifiedWater["FresnelMin"] != 0.2)
			return false;

		return !WaterAppearanceFallbackPolicy::MirrorAppearanceValues(
			unifiedWater,
			adaptiveDestination,
			SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys);
	}

	static_assert(CoversRuntimeSourceSelection());
}

int main()
{
	return CoversRuntimeSourceSelection() && CoversRetainedMigrationFallback() ? 0 : 1;
}
