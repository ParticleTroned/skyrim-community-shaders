#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Util::Settings
{
	namespace Detail
	{
		inline bool OverlayKnownValue(nlohmann::json& a_default, const nlohmann::json& a_user, const nlohmann::json& a_schema)
		{
			if (a_schema.is_object()) {
				if (!a_user.is_object())
					return false;
				if (!a_default.is_object())
					a_default = nlohmann::json::object();
				for (const auto& [key, value] : a_user.items()) {
					const auto schema = a_schema.find(key);
					auto current = a_default.find(key);
					if (schema != a_schema.end()) {
						if (current != a_default.end()) {
							OverlayKnownValue(*current, value, *schema);
						} else {
							auto validated = *schema;
							if (OverlayKnownValue(validated, value, *schema))
								a_default[key] = std::move(validated);
						}
					} else if (current != a_default.end()) {
						const auto known = *current;
						OverlayKnownValue(*current, value, known);
					} else {
						a_default[key] = value;
					}
				}
				return true;
			}
			if (a_schema.is_array()) {
				if (!a_user.is_array() || a_user.size() != a_schema.size())
					return false;
				auto merged = a_default.is_array() && a_default.size() == a_schema.size() ? a_default : a_schema;
				for (std::size_t index = 0; index < merged.size(); ++index) {
					if (!OverlayKnownValue(merged[index], a_user[index], a_schema[index]))
						return false;
				}
				a_default = std::move(merged);
				return true;
			}
			if (a_schema.is_number()) {
				if (!a_user.is_number())
					return false;
				const double value = a_user.get<double>();
				if (!std::isfinite(value))
					return false;
				if (a_schema.is_number_float()) {
					if (std::abs(value) > std::numeric_limits<float>::max())
						return false;
					a_default = static_cast<float>(value);
				} else if (a_schema.is_number_unsigned()) {
					if (std::trunc(value) != value || value < 0 || value > std::numeric_limits<std::uint32_t>::max())
						return false;
					a_default = static_cast<std::uint32_t>(value);
				} else {
					if (std::trunc(value) != value || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
						return false;
					a_default = static_cast<std::int32_t>(value);
				}
				return true;
			}
			if (a_schema.type() != a_user.type())
				return false;
			a_default = a_user;
			return true;
		}
	}

	/**
	 * @brief Merges a core section over loaded defaults, retaining malformed-value fallbacks.
	 * @return True for a handled core section, including a rejected malformed patch.
	 */
	inline bool OverlayCoreSettings(nlohmann::json& a_defaults, std::string_view a_section, const nlohmann::json& a_user)
	{
		constexpr std::array sections{ std::string_view{ "General" }, std::string_view{ "Advanced" },
			std::string_view{ "Replace Original Shaders" }, std::string_view{ "Disable at Boot" } };
		bool recognized = false;
		for (const auto section : sections)
			recognized |= section == a_section;
		if (!recognized)
			return false;
		if (!a_user.is_object())
			return true;
		auto& target = a_defaults[std::string(a_section)];
		if (!target.is_object())
			target = nlohmann::json::object();
		if (a_section == "Replace Original Shaders" || a_section == "Disable at Boot") {
			for (const auto& [key, value] : a_user.items()) {
				if (value.is_boolean())
					target[key] = value;
			}
		} else {
			// Persisted JSON loses the signedness and width of the C++ settings.
			static const nlohmann::json schemas{
				{ "General", { { "Enable Shaders", true }, { "Enable Disk Cache", true },
								 { "Skip Unchanged Shaders", true }, { "Enable Async", true } } },
				{ "Advanced", { { "Dump Shaders", false }, { "Log Level", std::int32_t{ 0 } },
								  { "Shader Defines", "" }, { "Compiler Threads", std::int32_t{ 0 } },
								  { "Background Compiler Threads", std::int32_t{ 0 } }, { "Use FileWatcher", false },
								  { "Frame Annotations", false }, { "Refraction Scale", 0.0f },
								  { "PBR Metal Reflection Scale", 0.0f }, { "PBR Metal Highlight Scale", 0.0f },
								  { "PBR Verbose JSON Logging", false }, { "Partial Precision", false } } }
			};
			Detail::OverlayKnownValue(target, a_user, schemas.at(std::string(a_section)));
		}
		return true;
	}
}
