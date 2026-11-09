#include "SeasonalWeatherConfig.h"
#include "Utils/StringUtils.h"

#include <charconv>
#include <format>
#include <regex>
#include <stdexcept>
#include <unordered_set>

namespace SeasonalWeatherConfig
{
	using json = nlohmann::json;
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SnowAssignment, Region, Weather, Weight)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherCategory, Weather, Category)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Settings, Enabled, SkipGlobalControlledRegions, MonthlyWeights,
		ExcludedWorldspaces, ExcludedRegions, SnowAssignments, WeatherCategories)

	namespace
	{
		void Validate(const json& value, const json& schema, const std::string& path)
		{
			const auto type = schema.at("type").get<std::string>();
			if (type == "object") {
				if (!value.is_object())
					throw std::invalid_argument(path + " must be an object");
				for (const auto& [key, item] : value.items()) {
					if (!schema.at("properties").contains(key))
						throw std::invalid_argument(path + ": unknown setting " + key);
					Validate(item, schema.at("properties").at(key), path + "." + key);
				}
				for (const auto& key : schema.value("required", json::array()))
					if (!value.contains(key.get<std::string>()))
						throw std::invalid_argument(path + ": missing " + key.get<std::string>());
			} else if (type == "array") {
				if (!value.is_array() || value.size() < schema.value("minItems", size_t{ 0 }) || value.size() > schema.at("maxItems").get<size_t>())
					throw std::invalid_argument(path + " has an invalid array length");
				for (const auto& item : value)
					Validate(item, schema.at("items"), path);
			} else if (type == "string") {
				if (!value.is_string())
					throw std::invalid_argument(path + " must be a string");
				const auto& text = value.get_ref<const std::string&>();
				if (text.empty() || text.size() > 255 || text.find('\0') != std::string::npos)
					throw std::invalid_argument(path + " must be a nonempty bounded identifier");
				if (schema.contains("pattern"))
					FormKey(text);
			} else if (type == "boolean") {
				if (!value.is_boolean())
					throw std::invalid_argument(path + " must be boolean");
			} else {
				if (!value.is_number() || (type == "integer" && !value.is_number_integer()))
					throw std::invalid_argument(path + " has the wrong numeric type");
				const double number = value.get<double>();
				if (!Util::IsFinite(number) || number < schema.at("minimum").get<double>() || number > schema.at("maximum").get<double>())
					throw std::invalid_argument(path + " is outside its finite range");
			}
		}
	}

	const json& FormKeySchema()
	{
		static const json schema{ { "type", "string" }, { "minLength", 1 }, { "maxLength", 255 },
			{ "pattern", R"(^0[xX][0-9A-Fa-f]{1,6}~[^~\\/\x00-\x1f\x7f]{1,240}\.[eE][sS][mMpPlL]$)" } };
		return schema;
	}

	std::string FormKey(std::string_view key)
	{
		static const std::regex pattern(FormKeySchema().at("pattern").get<std::string>());
		if (key.size() > 255 || !std::regex_match(key.begin(), key.end(), pattern))
			throw std::invalid_argument("Expected a portable 0xLOCAL~Plugin.esp form key");
		const auto separator = key.find('~');
		if (!Util::IsSinglePathComponent(key.substr(separator + 1)))
			throw std::invalid_argument("Portable form keys require a plugin filename");
		uint32_t localID = 0;
		const auto parsed = std::from_chars(key.data() + 2, key.data() + separator, localID, 16);
		if (parsed.ec != std::errc{} || localID == 0)
			throw std::invalid_argument("Portable form keys require a nonzero local ID");
		return std::format("0x{:x}~{}", localID, Util::ToLowerAscii(key.substr(separator + 1)));
	}

	const json& Schema()
	{
		static const json schema = [] {
			const json identifier{ { "type", "string" }, { "minLength", 1 }, { "maxLength", 255 } };
			const auto& form = FormKeySchema();
			const json identifiers{ { "type", "array" }, { "maxItems", SeasonalWeatherPolicy::MaxAssignments }, { "uniqueItems", true }, { "items", identifier } };
			json regionIdentifiers = identifiers;
			regionIdentifiers["items"] = form;
			const json weight{ { "type", "number" }, { "minimum", 0 }, { "maximum", SeasonalWeatherPolicy::MaxMultiplier } };
			const json monthly{ { "type", "array" }, { "minItems", 12 }, { "maxItems", 12 },
				{ "items", { { "type", "array" }, { "minItems", 4 }, { "maxItems", 4 }, { "items", weight } } } };
			const json assignment{ { "type", "object" }, { "additionalProperties", false }, { "required", { "Region", "Weather", "Weight" } },
				{ "properties", { { "Region", form }, { "Weather", form }, { "Weight", { { "type", "number" }, { "minimum", SeasonalWeatherPolicy::MinAssignmentWeight }, { "maximum", SeasonalWeatherPolicy::MaxAssignmentWeight } } } } } };
			const json category{ { "type", "object" }, { "additionalProperties", false }, { "required", { "Weather", "Category" } },
				{ "properties", { { "Weather", form }, { "Category", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 4 } } } } } };
			return json{ { "type", "object" }, { "additionalProperties", false }, { "properties", { { "Enabled", { { "type", "boolean" }, { "default", false } } }, { "SkipGlobalControlledRegions", { { "type", "boolean" }, { "default", true } } }, { "MonthlyWeights", monthly }, { "ExcludedWorldspaces", identifiers }, { "ExcludedRegions", regionIdentifiers }, { "SnowAssignments", { { "type", "array" }, { "maxItems", SeasonalWeatherPolicy::MaxAssignments }, { "items", assignment } } }, { "WeatherCategories", { { "type", "array" }, { "maxItems", SeasonalWeatherPolicy::MaxAssignments }, { "items", category } } } } } };
		}();
		return schema;
	}

	Settings Parse(const json& input)
	{
		Validate(input, Schema(), "seasonalWeather");
		json complete = Settings{};
		complete.update(input);
		auto result = complete.get<Settings>();
		for (auto& key : result.ExcludedWorldspaces)
			key = Util::ToLowerAscii(key);
		for (auto& key : result.ExcludedRegions)
			key = FormKey(key);
		for (auto& entry : result.SnowAssignments) {
			entry.Region = FormKey(entry.Region);
			entry.Weather = FormKey(entry.Weather);
		}
		for (auto& entry : result.WeatherCategories)
			entry.Weather = FormKey(entry.Weather);
		for (const auto* list : { &result.ExcludedWorldspaces, &result.ExcludedRegions }) {
			std::unordered_set<std::string> seen;
			for (const auto& key : *list)
				if (!seen.insert(key).second)
					throw std::invalid_argument("Duplicate seasonal exclusion: " + key);
		}
		std::unordered_set<std::string> assignments;
		for (const auto& entry : result.SnowAssignments)
			if (!assignments.insert(entry.Region + "|" + entry.Weather).second)
				throw std::invalid_argument("Duplicate regional snow assignment");
		std::unordered_set<std::string> categories;
		for (const auto& entry : result.WeatherCategories)
			if (!categories.insert(entry.Weather).second)
				throw std::invalid_argument("Duplicate weather category override");
		return result;
	}

	json Serialize(const Settings& settings) { return settings; }
}
