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

	// Migrates one root-settings source layer in place. The old Adaptive Brightness
	// root is folded into Adaptive Balance, with explicit values under the new name
	// taking precedence. Legacy CS Utility renderer fields are then moved into the
	// canonical root. The CS Utility enabled value is retained for DOF and copied to
	// the renderer gate only when that layer is demonstrably legacy and explicitly
	// contains the value.
	bool MigrateAdaptiveBalanceRootLayer(nlohmann::json& a_layer);

	// Splits one feature-scoped CSUtility source layer. Moved renderer fields are
	// removed from a_csUtilityLayer and returned as an AdaptiveBrightness feature
	// patch; enabled and DOF settings remain in the source layer.
	nlohmann::json ExtractAdaptiveBalanceFeaturePatch(nlohmann::json& a_csUtilityLayer);
}
