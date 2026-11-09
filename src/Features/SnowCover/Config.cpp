#include "Config.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsValidation.h"
#include "Utils/StringUtils.h"

namespace SnowConfig
{
	void Validate(const json& value, const json& schema, const std::string& path)
	{
		Util::ValidateSettingsSchema<float>(value, schema, path, [](const std::string& text, const json&) {
			Util::JoinRelativePath(Util::PathHelpers::GetDataPath(), std::filesystem::path(text));
		});
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
