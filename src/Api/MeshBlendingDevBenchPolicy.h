#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace CSX::Api::MeshBlendingDevBench
{
	enum class ValueKind
	{
		Toggle,
		Integer,
		Number
	};

	struct Setting
	{
		std::string_view name;
		ValueKind kind;
		double minimum;
		double maximum;
	};

	inline constexpr Setting Settings[] = {
		{ "Enabled", ValueKind::Toggle, 0, 1 },
		{ "NifEnabled", ValueKind::Toggle, 0, 1 },
		{ "BlendStrength", ValueKind::Number, 0, 1 },
		{ "BlendWidth", ValueKind::Number, 0.01, 256 },
		{ "DepthBias", ValueKind::Number, 0, 128 },
		{ "MaximumGap", ValueKind::Number, 0.01, 4096 },
		{ "DetectionMode", ValueKind::Integer, 0, 2 },
		{ "MaximumDistance", ValueKind::Number, 0, 65536 },
		{ "BoundsExpansion", ValueKind::Number, 0, 1024 },
		{ "RequireOverlappingBounds", ValueKind::Toggle, 0, 1 },
		{ "DeveloperLogging", ValueKind::Toggle, 0, 1 }
	};

	/** Validate a complete patch before returning a replacement settings value. */
	inline nlohmann::json ApplySettingsPatch(const nlohmann::json& a_current, const nlohmann::json& a_patch)
	{
		if (!a_current.is_object() || !a_patch.is_object()) {
			throw std::invalid_argument("Mesh Blending settings must be an object");
		}
		auto proposed = a_current;
		for (const auto& [name, value] : a_patch.items()) {
			const auto field = std::find_if(std::begin(Settings), std::end(Settings), [&](const Setting& a_field) {
				return a_field.name == name;
			});
			if (field == std::end(Settings) || !a_current.contains(name)) {
				throw std::invalid_argument("Unknown Mesh Blending setting: " + name);
			}
			if (field->kind == ValueKind::Toggle && value.is_boolean()) {
				proposed[name] = value.get<bool>() ? 1u : 0u;
				continue;
			}
			if (!value.is_number() || (field->kind != ValueKind::Number && !value.is_number_integer())) {
				throw std::invalid_argument("Invalid Mesh Blending setting type: " + name);
			}
			const auto numeric = value.get<double>();
			if (!std::isfinite(numeric) || numeric < field->minimum || numeric > field->maximum) {
				throw std::invalid_argument("Mesh Blending setting is outside its supported range: " + name);
			}
			proposed[name] = field->kind == ValueKind::Number ? nlohmann::json(static_cast<float>(numeric)) :
			                                                    nlohmann::json(static_cast<std::uint32_t>(numeric));
		}
		return proposed;
	}

	/** Describe the same accepted fields and ranges used by the request parser. */
	inline nlohmann::json Schema()
	{
		using json = nlohmann::json;
		json properties = json::object();
		for (const auto& field : Settings) {
			json schema{ { "type", field.kind == ValueKind::Number ? "number" : "integer" },
				{ "minimum", field.minimum }, { "maximum", field.maximum } };
			if (field.kind == ValueKind::Toggle) {
				schema = { { "oneOf", json::array({ json{ { "type", "boolean" } }, std::move(schema) }) } };
			}
			properties[std::string(field.name)] = std::move(schema);
		}
		return {
			{ "type", "object" },
			{ "additionalProperties", false },
			{ "properties", { { "settings", { { "type", "object" }, { "additionalProperties", false }, { "properties", std::move(properties) },
												{ "description", "Partial session settings; omitted values are preserved. MaximumGap is raised to at least DepthBias + BlendWidth. The response reports sanitized effective settings." } } },
								{ "discoveryCapture", { { "type", "boolean" }, { "description", "Start or stop bounded session discovery without writing the rule file." } } },
								{ "resetDiagnostics", { { "type", "boolean" }, { "description", "Reset Mesh Blending diagnostic counters for a new observation." } } } } }
		};
	}

	/** Describe readback separately from other feature actions' settings objects. */
	inline nlohmann::json StatusSchema()
	{
		return {
			{ "type", "object" },
			{ "additionalProperties", false },
			{ "properties", { { "available", { { "type", "boolean" } } },
								{ "runtimeEnabled", { { "type", "boolean" } } },
								{ "settings", Schema().at("properties").at("settings") },
								{ "persisted", { { "type", "boolean" }, { "const", false } } },
								{ "ruleFileWritable", { { "type", "boolean" } } },
								{ "ruleFileError", { { "type", "string" } } },
								{ "discoveryCapture", { { "type", "boolean" } } },
								{ "diagnosticsEnabled", { { "type", "boolean" } } },
								{ "diagnostics", { { "type", "object" }, { "additionalProperties", { { "type", "integer" }, { "minimum", 0 } } } } } } }
		};
	}
}
