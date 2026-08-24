#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>

namespace SettingsMigrations
{
	inline constexpr std::string_view kAdaptiveBalanceSettingsName = "Adaptive Balance";
	inline constexpr std::string_view kLegacyAdaptiveBrightnessSettingsName = "Adaptive Brightness";
	inline constexpr std::string_view kAdaptiveBalanceFeatureName = "AdaptiveBrightness";
	inline constexpr std::string_view kCSUtilitySettingsName = "CS Utility";
	inline constexpr std::string_view kCSUtilityFeatureName = "CSUtility";
	inline constexpr std::string_view kUnifiedWaterSettingsName = "Unified Water";
	inline constexpr std::string_view kUnifiedWaterFeatureName = "UnifiedWater";
	inline constexpr std::string_view kLegacyWaterAppearanceSettingsKey = "waterAppearance";
	inline constexpr std::string_view kLegacyWaterAppearanceForceGlobalKey = "forceGlobal";
	inline constexpr std::string_view kLegacyWaterProfileExplicitKey = "legacyWaterExplicit";
	inline constexpr std::array<std::string_view, 8> kLegacyUnifiedWaterAppearanceKeys{
		"WaterBrightness",
		"GlobalReflectionAmount",
		"RefractionAmount",
		"SunSpecularMultiplier",
		"WaveAmplitude",
		"FresnelMin",
		"FresnelMax",
		"Muddiness"
	};

	inline constexpr std::array<std::string_view, 8> kLegacyCSUtilityLightingKeys{
		"skyBrightness",
		"directionalLightMult",
		"pointLightMult",
		"linearPointLightMult",
		"spotlightMult",
		"linearSpotlightMult",
		"omnidirectionalBulbMult",
		"linearOmnidirectionalBulbMult"
	};

	// Checks whether a JSON value has the same deserializable shape as a default
	// value. Number types are intentionally interchangeable because the settings
	// serializers accept both integral and floating-point representations.
	bool MatchesJsonSchema(const nlohmann::json& a_value, const nlohmann::json& a_schema);

	// Checks whether an object carries at least one of Unified Water's eight
	// former appearance-control values.
	bool HasLegacyUnifiedWaterAppearanceValues(const nlohmann::json& a_value);

	// Migrates one root-settings source layer in place. The old Adaptive Brightness
	// root is folded into Adaptive Balance, with explicit values under the new name
	// taking precedence. Legacy CS Utility renderer fields and Unified Water's former
	// global appearance fields are then mirrored into the canonical root while being
	// retained by Unified Water as its runtime fallback. The CS Utility
	// enabled value is retained for DOF and copied to the global-lighting gate only when
	// that layer is demonstrably legacy and explicitly contains the value.
	bool MigrateAdaptiveBalanceRootLayer(
		nlohmann::json& a_layer,
		bool a_forceLegacyWaterAppearance = false);

	// Marks explicit profile water values in one source layer before it is merged.
	// The marker is consumed during Adaptive Balance's legacy migration and is not
	// emitted by its normal settings serialization.
	bool MarkExplicitAdaptiveBalanceWaterProfiles(nlohmann::json& a_adaptiveBalanceLayer);

	// Reports whether this source layer carries a legacy Unified Water global
	// appearance patch that must supersede lower-priority profile values.
	bool HasForcedLegacyWaterAppearance(const nlohmann::json& a_adaptiveBalanceLayer);

	// Removes source markers from a lower-priority merged layer before a legacy
	// Unified Water global patch is applied over it.
	void ClearExplicitAdaptiveBalanceWaterProfiles(nlohmann::json& a_adaptiveBalanceLayer);

	// Splits one feature-scoped CSUtility source layer. Moved renderer fields are
	// removed from a_csUtilityLayer and returned as an AdaptiveBrightness feature
	// patch; enabled and DOF settings remain in the source layer.
	nlohmann::json ExtractAdaptiveBalanceFeaturePatch(nlohmann::json& a_csUtilityLayer);

	// Mirrors the eight appearance controls from a feature-scoped UnifiedWater source
	// layer into an AdaptiveBrightness feature patch. The source values are retained
	// so Unified Water can provide the fallback when Adaptive Balance is inactive.
	nlohmann::json ExtractAdaptiveBalanceWaterFeaturePatch(nlohmann::json& a_unifiedWaterLayer);
}
