#pragma once

#include "AdaptiveBalanceDepthOfField.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace AdaptiveBalanceDepthOfFieldSettingsPolicy
{
	/** Shared partial-settings schema for migration, native compatibility and DevBench. */
	inline nlohmann::json Schema()
	{
		using namespace AdaptiveBalanceDepthOfField;
		using json = nlohmann::json;
		const auto number = [](double minimum, double maximum) -> json {
			return { { "type", "number" }, { "minimum", minimum }, { "maximum", maximum } };
		};
		const auto object = [](json properties) -> json {
			return { { "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false }, { "properties", std::move(properties) } };
		};
		const json boolean{ { "type", "boolean" } };
		const json autofocus = object({ { "nearDistance", number(kDofDistanceMin, kDofAutoFocusDepthMax) },
			{ "farDistance", number(kDofDistanceMin, kDofAutoFocusDepthMax) },
			{ "nearRange", number(kDofRangeMin, kDofAutoFocusDepthMax) },
			{ "farRange", number(kDofRangeMin, kDofAutoFocusDepthMax) },
			{ "nearBlur", number(kDofAutoFocusBlurMin, kDofAutoFocusBlurMax) },
			{ "farBlur", number(kDofAutoFocusBlurMin, kDofAutoFocusBlurMax) },
			{ "blurMultiplier", number(kDofAutoFocusBlurMultiplierMin, kDofAutoFocusBlurMultiplierMax) } });
		const json values = object({ { "strength", number(kDofStrengthMin, kDofStrengthMax) },
			{ "distance", number(kDofDistanceMin, kDofDistanceMax) },
			{ "range", number(kDofRangeMin, kDofRangeMax) },
			{ "mode", { { "type", "integer" }, { "minimum", 0 }, { "maximum", kDofModeMask } } },
			{ "blurRadius", { { "type", "integer" }, { "minimum", 0 }, { "maximum", kDofBlurRadiusMax } } },
			{ "excludeSky", boolean }, { "autoFocus", boolean }, { "autoFocusSettings", autofocus } });
		const json overrideSettings = object({ { "locked", boolean }, { "values", values }, { "baseline", values } });
		return object({ { "enabled", boolean }, { "fixUnderwaterFogDofBlur", boolean },
			{ "sceneDof", overrideSettings }, { "underwaterDof", overrideSettings } });
	}

	namespace Detail
	{
		inline bool Validate(const nlohmann::json& a_value, const nlohmann::json& a_schema, const std::string& a_path, std::string& a_error)
		{
			const auto type = a_schema.at("type").get<std::string>();
			if (type == "object") {
				if (!a_value.is_object() || a_value.empty()) {
					a_error = a_path + " must be a nonempty object";
					return false;
				}
				const auto& properties = a_schema.at("properties");
				for (const auto& [name, value] : a_value.items()) {
					if (!properties.contains(name)) {
						a_error = "Unknown depth-of-field field: " + a_path + "." + name;
						return false;
					}
					if (!Validate(value, properties.at(name), a_path + "." + name, a_error))
						return false;
				}
				return true;
			}
			if (type == "boolean") {
				if (a_value.is_boolean())
					return true;
			} else if (a_value.is_number() && (type != "integer" || a_value.is_number_integer())) {
				const double value = a_value.get<double>();
				if (std::isfinite(value) && value >= a_schema.at("minimum").get<double>() && value <= a_schema.at("maximum").get<double>())
					return true;
			}
			a_error = "Invalid type, non-finite value or out-of-range depth-of-field field: " + a_path;
			return false;
		}
	}

	/** Tests a value against the corresponding shared settings schema. */
	inline bool IsValidValue(const nlohmann::json& a_value, const nlohmann::json& a_schema)
	{
		std::string error;
		return Detail::Validate(a_value, a_schema, "depthOfField", error);
	}

	/** Reject the complete patch before mutation, including unknown nested fields. */
	inline bool Validate(const nlohmann::json& a_patch, std::string& a_error)
	{
		static const auto schema = Schema();
		a_error.clear();
		return Detail::Validate(a_patch, schema, "depthOfField", a_error);
	}

	/** Publish one validated partial update while retaining all omitted values. */
	inline bool Apply(const nlohmann::json& a_patch, AdaptiveBalanceDepthOfField::Settings& a_settings, std::string& a_error)
	{
		if (!Validate(a_patch, a_error))
			return false;
		nlohmann::json candidate = a_settings;
		candidate.merge_patch(a_patch);
		a_settings = candidate.get<AdaptiveBalanceDepthOfField::Settings>();
		return true;
	}
}
