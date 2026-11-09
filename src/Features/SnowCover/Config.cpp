#include "Config.h"
#include "Utils/FileSystem.h"
#include "Utils/StringUtils.h"

namespace SnowConfig
{
	void Validate(const json& value, const json& schema, const std::string& path)
	{
		const auto type = schema.at("type").get<std::string>();
		if (type == "object") {
			if (!value.is_object())
				throw std::invalid_argument(path + " must be an object");
			const auto& properties = schema.at("properties");
			for (const auto& [key, field] : value.items()) {
				if (!properties.contains(key))
					throw std::invalid_argument(path + ": unknown setting " + key);
				Validate(field, properties.at(key), path + "." + key);
			}
			if (schema.contains("required"))
				for (const auto& key : schema.at("required"))
					if (!value.contains(key.get<std::string>()))
						throw std::invalid_argument(path + ": incomplete vector");
		} else if (type == "array") {
			if (!value.is_array() || value.size() != schema.at("minItems").get<size_t>())
				throw std::invalid_argument(path + " has the wrong vector length");
			for (const auto& item : value) Validate(item, schema.at("items"), path);
		} else if (type == "boolean") {
			if (!value.is_boolean())
				throw std::invalid_argument(path + " must be boolean");
		} else if (type == "string") {
			if (!value.is_string() || value.get_ref<const std::string&>().size() > schema.at("maxLength").get<size_t>())
				throw std::invalid_argument(path + " must be a bounded string");
			const auto& text = value.get_ref<const std::string&>();
			if (text.find('\0') != std::string::npos)
				throw std::invalid_argument(path + " contains a null byte");
			Util::JoinRelativePath(Util::PathHelpers::GetDataPath(), std::filesystem::path(text));
		} else {
			if (!value.is_number() || (type == "integer" && !value.is_number_integer()))
				throw std::invalid_argument(path + " has the wrong numeric type");
			const double number = value.get<double>();
			if (!std::isfinite(number) || number < schema.at("minimum").get<float>() || number > schema.at("maximum").get<float>())
				throw std::invalid_argument(path + " is outside its finite range");
		}
	}

	const json& Schemas()
	{
		static const json schemas = [] {
			json result;
			result["settings"] = { { "type", "object" }, { "additionalProperties", false }, { "properties", json::object() } };
			result["settings"]["properties"]["Enable"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["EnableExpensiveFoliage"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["AffectHavok"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["AffectFloraTint"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["EnableGlints"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["EnableRooftops"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["EnableFoliageTint"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["EnableBoundsCulling"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["settings"]["properties"]["SnowHeightOffset"] = json::parse(R"({"type":"number","minimum":-20000,"maximum":20000})");
			result["settings"]["properties"]["RooftopSnow"] = json::parse(R"({"type":"number","minimum":0,"maximum":20000})");
			result["settings"]["properties"]["RooftopHeight"] = json::parse(R"({"type":"number","minimum":32,"maximum":1024})");
			result["settings"]["properties"]["RooftopExposure"] = json::parse(R"({"type":"number","minimum":0,"maximum":0.95})");
			result["settings"]["properties"]["FireMeltRadiusScale"] = json::parse(R"({"type":"number","minimum":1,"maximum":16})");
			result["settings"]["properties"]["FireMeltStrength"] = json::parse(R"({"type":"number","minimum":0,"maximum":1})");
			result["settings"]["properties"]["FireMeltEnabled"] = json::parse(R"({"type":"boolean"})");
			result["settings"]["properties"]["SurvivalEnabled"] = json::parse(R"({"type":"boolean"})");
			result["world"] = { { "type", "object" }, { "additionalProperties", false }, { "properties", json::object() } };
			result["world"]["properties"]["EnableSnowCover"] = json::parse(R"({"type":"boolean"})");
			result["world"]["properties"]["UseLandscapeSnowColor"] = json::parse(R"({"type":"boolean","default":false,"description":"Follow installed main/alternate snow texture-set paths, seasonal swaps and PBR classification for color, retaining configured normal/material maps and tints. Standard snow01/snow02 paths are used when canonical records are unavailable. Failed color loads retain the configured material color."})");
			result["world"]["properties"]["AffectGrassTint"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["world"]["properties"]["AffectTreeTint"] = json::parse(R"({"type":"integer","minimum":0,"maximum":1})");
			result["world"]["properties"]["MaxSummerMonth"] = json::parse(R"({"type":"integer","minimum":0,"maximum":11})");
			result["world"]["properties"]["MaxWinterMonth"] = json::parse(R"({"type":"integer","minimum":0,"maximum":11})");
			result["world"]["properties"]["FoliageHeightOffset"] = json::parse(R"({"type":"number","minimum":-20000,"maximum":20000})");
			result["world"]["properties"]["UVScale"] = json::parse(R"({"type":"number","minimum":0.01,"maximum":10})");
			result["world"]["properties"]["SummerHeightOffset"] = json::parse(R"({"type":"number","minimum":-20000,"maximum":20000})");
			result["world"]["properties"]["WinterHeightOffset"] = json::parse(R"({"type":"number","minimum":-20000,"maximum":20000})");
			result["world"]["properties"]["MapZscale"] = json::parse(R"({"type":"number","minimum":0.1,"maximum":1000000})");
			result["world"]["properties"]["BlendSmoothness"] = json::parse(R"({"type":"number","minimum":1,"maximum":10000})");
			result["world"]["properties"]["WeatherFadeStart"] = json::parse(R"({"type":"number","minimum":0,"maximum":200000})");
			result["world"]["properties"]["WeatherFadeEnd"] = json::parse(R"({"type":"number","minimum":1,"maximum":300000})");
			result["world"]["properties"]["ObjectFadeStart"] = json::parse(R"({"type":"number","minimum":0,"maximum":40000})");
			result["world"]["properties"]["ObjectFadeEnd"] = json::parse(R"({"type":"number","minimum":1,"maximum":40001})");
			result["world"]["properties"]["ObjectFadeAmount"] = json::parse(R"({"type":"number","minimum":0,"maximum":1})");
			result["world"]["properties"]["ScreenSpaceScale"] = json::parse(R"({"type":"number","minimum":1,"maximum":3})");
			result["world"]["properties"]["LogMicrofacetDensity"] = json::parse(R"({"type":"number","minimum":0,"maximum":40})");
			result["world"]["properties"]["MicrofacetRoughness"] = json::parse(R"({"type":"number","minimum":0.001,"maximum":1})");
			result["world"]["properties"]["DensityRandomization"] = json::parse(R"({"type":"number","minimum":0.001,"maximum":5})");
			result["world"]["properties"]["SnowingSpeed"] = json::parse(R"({"type":"number","minimum":0.01,"maximum":10})");
			result["world"]["properties"]["MeltingSpeed"] = json::parse(R"({"type":"number","minimum":0.01,"maximum":10})");
			result["world"]["properties"]["PeakMainAngle"] = json::parse(R"({"type":"number","minimum":0,"maximum":1})");
			result["world"]["properties"]["PeakAltAngle"] = json::parse(R"({"type":"number","minimum":0,"maximum":1})");
			result["world"]["properties"]["MinAngle"] = json::parse(R"({"type":"number","minimum":0,"maximum":0.999})");
			result["world"]["properties"]["MaxAngle"] = json::parse(R"({"type":"number","minimum":0.001,"maximum":1})");
			result["world"]["properties"]["MainSpec"] = json::parse(R"({"type":"number","minimum":0,"maximum":1})");
			result["world"]["properties"]["AltSpec"] = json::parse(R"({"type":"number","minimum":0,"maximum":1})");
			result["world"]["properties"]["MapMin"] = json::parse(R"({"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","minimum":-10000000.0,"maximum":10000000.0}})");
			result["world"]["properties"]["MapMax"] = json::parse(R"({"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","minimum":-10000000.0,"maximum":10000000.0}})");
			result["world"]["properties"]["MainTint"] = json::parse(R"({"type":"array","minItems":4,"maxItems":4,"items":{"type":"number","minimum":0,"maximum":1}})");
			result["world"]["properties"]["AltTint"] = json::parse(R"({"type":"array","minItems":4,"maxItems":4,"items":{"type":"number","minimum":0,"maximum":1}})");
			result["world"]["properties"]["MapTexture"] = json::parse(R"({"type":"string","maxLength":255})");
			result["world"]["properties"]["MainTexture"] = json::parse(R"({"type":"string","maxLength":255})");
			result["world"]["properties"]["AltTexture"] = json::parse(R"({"type":"string","maxLength":255})");
			return result;
		}();
		return schemas;
	}

	void ValidateWorld(const json& config)
	{
		Validate(config, Schemas().at("world"), "snowWorld");
		if (config.at("WeatherFadeEnd").get<float>() < config.at("WeatherFadeStart").get<float>() + 1.0f ||
			config.at("ObjectFadeEnd").get<float>() < config.at("ObjectFadeStart").get<float>() + 1.0f ||
			config.at("MaxAngle").get<float>() < config.at("MinAngle").get<float>() + 0.001f)
			throw std::invalid_argument("Snow ranges must have a positive width");
		for (size_t i = 0; i < 2; ++i)
			if (std::abs(config.at("MapMax").at(i).get<float>() - config.at("MapMin").at(i).get<float>()) < 1.0f)
				throw std::invalid_argument("Snow map extent must span at least one unit per axis");
	}
}
