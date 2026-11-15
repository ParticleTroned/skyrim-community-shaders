#pragma once

#include "Finite.h"

#include <concepts>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Util
{
	/** Validate the closed settings-schema subset; callers supply domain-specific string rules.
	 * NumericBounds preserves the precision of the settings storage at inclusive range boundaries.
	 */
	template <std::floating_point NumericBounds = double, class StringValidator>
	void ValidateSettingsSchema(const nlohmann::json& value, const nlohmann::json& schema,
		const std::string& path, const StringValidator& validateString)
	{
		using json = nlohmann::json;
		const auto type = schema.at("type").get<std::string>();
		if (type == "object") {
			if (!value.is_object())
				throw std::invalid_argument(path + " must be an object");
			const auto& properties = schema.at("properties");
			for (const auto& [key, item] : value.items()) {
				if (!properties.contains(key))
					throw std::invalid_argument(path + ": unknown setting " + key);
				ValidateSettingsSchema<NumericBounds>(item, properties.at(key), path + "." + key, validateString);
			}
			if (const auto required = schema.find("required"); required != schema.end())
				for (const auto& key : *required)
					if (!value.contains(key.get<std::string>()))
						throw std::invalid_argument(path + ": missing " + key.get<std::string>());
		} else if (type == "array") {
			if (!value.is_array() || value.size() < schema.value("minItems", size_t{ 0 }) || value.size() > schema.at("maxItems").get<size_t>())
				throw std::invalid_argument(path + " has an invalid array length");
			for (const auto& item : value)
				ValidateSettingsSchema<NumericBounds>(item, schema.at("items"), path, validateString);
		} else if (type == "string") {
			if (!value.is_string())
				throw std::invalid_argument(path + " must be a string");
			const auto& text = value.get_ref<const std::string&>();
			if (text.size() < schema.value("minLength", size_t{ 0 }) || text.size() > schema.at("maxLength").get<size_t>() || text.find('\0') != std::string::npos)
				throw std::invalid_argument(path + " has an invalid string length or null byte");
			validateString(text, schema);
		} else if (type == "boolean") {
			if (!value.is_boolean())
				throw std::invalid_argument(path + " must be boolean");
		} else if (type == "number" || type == "integer") {
			if (!value.is_number() || (type == "integer" && !value.is_number_integer()))
				throw std::invalid_argument(path + " has the wrong numeric type");
			const double number = value.get<double>();
			if (!IsFinite(number) || number < schema.at("minimum").get<NumericBounds>() || number > schema.at("maximum").get<NumericBounds>())
				throw std::invalid_argument(path + " is outside its finite range");
		} else {
			throw std::invalid_argument(path + " has an unsupported schema type");
		}
	}
}
