#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>

namespace SettingsMigrations
{
	inline constexpr std::string_view kAdaptiveBalanceSettingsName = "Adaptive Brightness";
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

	// Migrates one root-settings source layer in place. Explicit Adaptive Balance
	// valid values in the same layer win; missing or malformed destination values
	// are repaired from schema-valid legacy CS Utility values. The CS Utility
	// enabled value is retained for DOF and copied to the renderer gate only when
	// that layer is demonstrably legacy and explicitly contains the value.
	bool MigrateAdaptiveBalanceRootLayer(nlohmann::json& a_layer);

	// Splits one feature-scoped CSUtility source layer. Moved renderer fields are
	// removed from a_csUtilityLayer and returned as an AdaptiveBrightness feature
	// patch; enabled and DOF settings remain in the source layer.
	nlohmann::json ExtractAdaptiveBalanceFeaturePatch(nlohmann::json& a_csUtilityLayer);
}
