#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace PerformanceProfiles
{
	inline constexpr std::array<std::string_view, 4> Names{ "Performance", "Balanced", "Quality", "MyPerformance" };

	/** Bound portable feature documents before passing them to native settings loaders. */
	inline bool ValidateSettings(const nlohmann::json& value, unsigned depth = 0)
	{
		if (depth > 32 || value.size() > 16384 || value.is_discarded() || value.is_binary())
			return false;
		if (value.is_number())
			return std::isfinite(value.get<double>());
		if (value.is_string())
			return value.get_ref<const std::string&>().size() <= 4096 && value.get_ref<const std::string&>().find('\0') == std::string::npos;
		if (value.is_object())
			for (const auto& [key, item] : value.items())
				if (key.size() > 4096 || key.find('\0') != std::string::npos)
					return false;
		if (value.is_structured())
			for (const auto& item : value)
				if (!ValidateSettings(item, depth + 1))
					return false;
		return true;
	}

	/** Apply the same snapshot envelope limits to captured and imported profiles. */
	inline bool ValidateSnapshot(const nlohmann::json& settings)
	{
		if (!settings.is_object() || settings.size() > 256 || settings.contains("Performance Profiles") || !ValidateSettings(settings))
			return false;
		for (const auto& [name, value] : settings.items())
			if (name.empty() || name.size() > 128)
				return false;
		return true;
	}

	/** Keep switches and named selections separate from full settings snapshots. */
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
		nlohmann::json settings = nlohmann::json::object();

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
			if (!a_document.is_object() || !a_document.contains("version") ||
				!a_document["version"].is_number_integer() || (a_document["version"] != 1 && a_document["version"] != 2) ||
				a_document.size() != (a_document["version"] == 1 ? 3 : 4) ||
				(a_document["version"] == 2 && !a_document.contains("settings")) ||
				!a_document.contains("base") || !a_document["base"].is_string() ||
				BuiltIn(a_document["base"].get_ref<const std::string&>()).is_null() ||
				!a_document.contains("choices") || !ValidateChoices(a_document["choices"]))
				return false;
			const auto candidate = a_document.value("settings", nlohmann::json::object());
			if (!ValidateSnapshot(candidate))
				return false;
			base = a_document["base"].get<std::string>();
			choices = a_document["choices"];
			settings = candidate;
			return true;
		}
		nlohmann::json Save() const { return { { "version", 2 }, { "base", base }, { "choices", choices }, { "settings", settings } }; }
	};
	/** Validates the public exchange envelope before replacing a personal profile. */
	inline bool ReadExport(const nlohmann::json& document, UserProfile& profile)
	{
		return document.is_object() && document.value("format", nlohmann::json{}) == "CSX.MyPerformance" &&
		       document.contains("version") && document["version"].is_number_integer() && (document["version"] == 1 || document["version"] == 2) && document.contains("profile") && profile.Load(document["profile"]);
	}
	inline nlohmann::json MakeExport(const UserProfile& profile, std::string_view sourceVersion)
	{
		return { { "format", "CSX.MyPerformance" }, { "version", 2 }, { "sourceVersion", sourceVersion }, { "profile", profile.Save() } };
	}

}
