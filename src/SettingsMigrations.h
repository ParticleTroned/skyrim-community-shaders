#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace SettingsMigrations
{
	/** Utility numeric fields must remain finite when converted to renderer floats. */
	inline bool IsFiniteUtilityNumber(const nlohmann::json& a_value)
	{
		return a_value.is_number() && std::isfinite(a_value.get<double>()) &&
		       std::abs(a_value.get<double>()) <= std::numeric_limits<float>::max();
	}
	/** Reject fractional, negative and overflowing values before unsigned conversion. */
	inline bool IsUtilityUnsignedInteger(const nlohmann::json& a_value)
	{
		return a_value.is_number_integer() && a_value.get<double>() >= 0 &&
		       a_value.get<double>() <= std::numeric_limits<uint32_t>::max();
	}

	inline constexpr std::string_view kAdaptiveBalanceSettingsName = "Adaptive Balance";
	inline constexpr std::string_view kLegacyAdaptiveBrightnessSettingsName = "Adaptive Brightness";
	inline constexpr std::string_view kAdaptiveBalanceFeatureName = "AdaptiveBrightness";
	inline constexpr std::string_view kCSUtilitySettingsName = "CS Utility";
	inline constexpr std::string_view kCSUtilityFeatureName = "CSUtility";
	inline constexpr std::string_view kOSUtilitySettingsName = "OS Utility";
	inline constexpr std::string_view kOSUtilityFeatureName = "OSUtility";
	inline constexpr std::string_view kDepthOfFieldSettingsKey = "depthOfField";
	inline constexpr std::array<std::string_view, 2> kLegacyUtilityFeatureNames{
		kOSUtilityFeatureName, kCSUtilityFeatureName
	};
	/** Supported utility settings as legacy and Adaptive Balance JSON pointers. */
	inline constexpr std::array<std::pair<std::string_view, std::string_view>, 43> kLegacyUtilityAppearanceFields{ {
		{ "/useAmbientEffectLighting", "/useAmbientEffectLighting" },
		{ "/skyBrightness", "/globalProfile/skyBrightnessMult" },
		{ "/cloudBrightness", "/globalProfile/cloudBrightnessMult" },
		{ "/skySaturation", "/globalProfile/skySaturation" },
		{ "/cloudSaturation", "/globalProfile/cloudSaturation" },
		{ "/ambientLightMult", "/globalProfile/ambientMult" },
		{ "/directionalLightMult", "/globalProfile/directionalLightMult" },
		{ "/pointLightMult", "/globalProfile/pointLightMult" },
		{ "/linearPointLightMult", "/globalProfile/linearPointLightMult" },
		{ "/spotlightMult", "/globalProfile/spotlightMult" },
		{ "/linearSpotlightMult", "/globalProfile/linearSpotlightMult" },
		{ "/omnidirectionalBulbMult", "/globalProfile/omnidirectionalBulbMult" },
		{ "/linearOmnidirectionalBulbMult", "/globalProfile/linearOmnidirectionalBulbMult" },
		{ "/sceneBrightness", "/globalProfile/brightness" },
		{ "/emitColorMult", "/globalProfile/emitColorMult" },
		{ "/glowmapMult", "/globalProfile/glowmapMult" },
		{ "/effectLightingMult", "/globalProfile/effectLightingMult" },
		{ "/skyGammaOffset", "/globalProfile/skyGammaOffset" },
		{ "/cloudGammaOffset", "/globalProfile/cloudGammaOffset" },
		{ "/effectBrightness", "/globalProfile/effectBrightness" },
		{ "/skyStaticBrightness", "/globalProfile/skyStaticBrightness" },
		{ "/skyStaticTransparency", "/globalProfile/skyStaticTransparency" },
		{ "/fogGammaOffset", "/globalProfile/fogGammaOffset" },
		{ "/fogAlphaGammaOffset", "/globalProfile/fogAlphaGammaOffset" },
		{ "/fogIntensity", "/globalProfile/fogIntensity" },
		{ "/waterGammaOffset", "/globalProfile/waterGammaOffset" },
		{ "/vlGammaOffset", "/globalProfile/vlGammaOffset" },
		{ "/vlIntensity", "/godrayFinalBrightness" },
		{ "/sunGlareIntensity", "/globalProfile/sunGlareIntensity" },
		{ "/water/brightness", "/globalProfile/water/WaterBrightness" },
		{ "/water/reflectionAmount", "/globalProfile/water/GlobalReflectionAmount" },
		{ "/water/refractionAmount", "/globalProfile/water/RefractionAmount" },
		{ "/water/sunSpecularMultiplier", "/globalProfile/water/SunSpecularMultiplier" },
		{ "/water/waveAmplitude", "/globalProfile/water/WaveAmplitude" },
		{ "/water/fresnelMin", "/globalProfile/water/FresnelMin" },
		{ "/water/fresnelMax", "/globalProfile/water/FresnelMax" },
		{ "/water/muddiness", "/globalProfile/water/Muddiness" },
		{ "/water/causticsStrength", "/globalProfile/water/CausticsStrength" },
		{ "/water/causticsTiling", "/globalProfile/water/CausticsTiling" },
		{ "/water/causticsSpeed", "/globalProfile/water/CausticsSpeed" },
		{ "/water/causticsDispersion", "/globalProfile/water/CausticsDispersion" },
		{ "/water/parallaxStrength", "/globalProfile/water/ParallaxStrength" },
		{ "/water/parallaxQuality", "/globalProfile/water/ParallaxQuality" },
	} };

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

	/** Seed absent cloud controls from a profile's formerly shared sky values. */
	bool MigrateCloudProfileSettings(nlohmann::json& a_profile);

	/** Migrate cloud controls within one Adaptive Balance source before merging. */
	bool MigrateCloudSettingsLayer(nlohmann::json& a_settings);

	// Migrates one root-settings source layer in place. The old Adaptive Brightness
	// root is folded into Adaptive Balance, with explicit values under the new name
	// taking precedence. Legacy CS Utility renderer fields and Unified Water's former
	// global appearance fields are then moved into the canonical root. The CS Utility
	// enabled value moves into depthOfField and gates migrated global lighting when
	// renderer values are present. OS Utility is an alias; inactive Bloom data remains.
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

	// Routes one feature-scoped CSUtility or OSUtility source layer. Recognized
	// settings become an AdaptiveBrightness patch; data without a complete canonical
	// representation, including inactive Bloom presets, remains in the source.
	nlohmann::json ExtractAdaptiveBalanceFeaturePatch(nlohmann::json& a_csUtilityLayer);
	/** Retires consumed user fields, retaining unsupported data without replaying Bloom. */
	void RetireAdaptiveBalanceFeaturePatch(nlohmann::json& a_legacyLayer);

	// Splits the eight legacy appearance controls from a feature-scoped UnifiedWater
	// source layer. The moved fields are removed from a_unifiedWaterLayer and returned
	// as an AdaptiveBrightness feature patch; all structural water settings remain.
	nlohmann::json ExtractAdaptiveBalanceWaterFeaturePatch(nlohmann::json& a_unifiedWaterLayer);
}
