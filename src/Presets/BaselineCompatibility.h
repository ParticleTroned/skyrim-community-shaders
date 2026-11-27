#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace Presets::BaselineCompatibility
{
	inline constexpr std::string_view kUpstreamLocalShadowKeys[]{
		"EnableLocalShadows",
		"LocalShadowFilterScale",
		"LocalShadowResolution",
		"LocalShadowSamples",
		"LocalShadowSlots",
	};

	/** Ignores known foreign baseline fields only while the active feature schema lacks them. */
	inline void EraseUnsupportedSettings(std::string_view featureName, const nlohmann::json& schema, nlohmann::json& values)
	{
		if (featureName != "LightLimitFix" || !schema.is_object() || !values.is_object())
			return;
		for (const auto key : kUpstreamLocalShadowKeys)
			if (!schema.contains(key))
				values.erase(key);
	}
}
