#pragma once

#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace PerformanceProfiles
{
	inline constexpr std::array<std::string_view, 4> Names{ "Performance", "Balanced", "Quality", "MyPreset" };

	/** Accept only selections; arbitrary feature parameters never enter a performance profile. */
	inline bool ValidateChoices(const nlohmann::json& a_choices)
	{
		if (!a_choices.is_object())
			return false;
		for (const auto& [category, values] : a_choices.items()) {
			if (!values.is_object() || values.size() > 128)
				return false;
			if (category == "upscaling") {
				for (const auto& [key, value] : values.items()) {
					if (key == "fsr4RuntimeEnable") {
						if (!value.is_boolean())
							return false;
						continue;
					}
					const unsigned limit = key == "qualityMode" ? 6 : key == "dlssPreset"      ? 5 :
					                                              key == "upscaleMethod"       ? 3 :
					                                              key == "upscaleMethodNoDLSS" ? 2 :
					                                                                             0;
					if (!limit || !value.is_number_integer() || value.get<std::int64_t>() < 0 || value.get<std::uint64_t>() > limit)
						return false;
				}
			} else if (category == "features" || category == "presets") {
				for (const auto& [key, value] : values.items()) {
					if (key.empty() || key.size() > 128 || key == "Upscaling")
						return false;
					if (category == "features" ? !value.is_boolean() : !value.is_string())
						return false;
					if (category == "presets") {
						const auto& name = value.get_ref<const std::string&>();
						if (name.empty() || name.size() > 128)
							return false;
					}
				}
			} else {
				return false;
			}
		}
		return true;
	}

	/** Unified tiers supply switches and upscaling; feature-owned presets supply their quality settings. */
	inline nlohmann::json BuiltIn(std::string_view a_name)
	{
		if (a_name != Names[0] && a_name != Names[1] && a_name != Names[2])
			return nullptr;
		const bool performance = a_name == Names[0];
		const unsigned qualityMode = performance ? 4 : a_name == Names[1] ? 3 :
		                                                                    2;
		return {
			{ "upscaling", { { "upscaleMethod", 3 }, { "upscaleMethodNoDLSS", 2 }, { "qualityMode", qualityMode },
							   { "dlssPreset", 1 }, { "fsr4RuntimeEnable", true } } },
			{ "features", { { "GrassCollision", !performance }, { "ScreenSpaceGI", a_name == Names[2] }, { "ScreenSpaceShadows", true }, { "Skylighting", !performance }, { "TerrainBlending", true }, { "Wetterness", !performance } } },
			{ "presets", { { "Skylighting", a_name }, { "Wetterness", a_name } } }
		};
	}

	struct UserProfile
	{
		std::string base = "Balanced";
		nlohmann::json choices = nlohmann::json::object();

		/** Resolve the base afresh, then replace only explicitly saved selections. */
		nlohmann::json Resolve() const
		{
			auto result = BuiltIn(base);
			for (const auto& [category, values] : choices.items())
				for (const auto& [key, value] : values.items())
					result[category][key] = value;
			return result;
		}
		/** Reject an invalid document atomically, retaining the last valid profile. */
		bool Load(const nlohmann::json& a_document)
		{
			if (!a_document.is_object() || a_document.size() != 3 || !a_document.contains("version") ||
				!a_document["version"].is_number_integer() || a_document["version"] != 1 ||
				!a_document.contains("base") || !a_document["base"].is_string() ||
				BuiltIn(a_document["base"].get_ref<const std::string&>()).is_null() ||
				!a_document.contains("choices") || !ValidateChoices(a_document["choices"]))
				return false;
			base = a_document["base"].get<std::string>();
			choices = a_document["choices"];
			return true;
		}
		nlohmann::json Save() const { return { { "version", 1 }, { "base", base }, { "choices", choices } }; }
	};
}
