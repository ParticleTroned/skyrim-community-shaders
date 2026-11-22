#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>

struct Feature;
namespace RE
{
	class TESWeather;
}

namespace WeatherFeatureOverrides
{
	/** Normalize and validate a complete feature block against its registered weather variables. */
	bool Prepare(Feature* feature, nlohmann::json& value, std::string& error);
	/** Read an attachment without changing editor or runtime state; a missing file is an empty object. */
	bool Read(RE::TESWeather* weather, nlohmann::json& root, std::string& error);
	/** Save a prepared block atomically (nullptr removes it), preserving all other attachment data. */
	bool Save(RE::TESWeather* weather, const std::string& featureName, const nlohmann::json* value,
		std::map<std::string, nlohmann::json>& persistedSettings, bool& changed, std::string& error);
}
