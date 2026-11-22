#include "WeatherFeatureOverrides.h"

#include "Feature.h"
#include "Utils/FileSystem.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include <exception>
#include <filesystem>
#include <format>

namespace WeatherFeatureOverrides
{
	using json = nlohmann::json;

	bool Prepare(Feature* feature, json& value, std::string& error)
	{
		if (!feature) {
			error = "weather feature is unavailable";
			return false;
		}
		if (!value.is_object()) {
			error = "valueJson must encode an object";
			return false;
		}
		const auto enabled = value.find("__enabled");
		if (enabled == value.end() || !enabled->is_boolean()) {
			error = "valueJson.__enabled is required and must be boolean";
			return false;
		}
		try {
			feature->NormalizeWeatherSettings(value);
		} catch (const std::exception& e) {
			error = std::format("feature normalization failed: {}", e.what());
			return false;
		}
		auto* featureRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetFeatureRegistry(feature->GetShortName());
		if (!featureRegistry) {
			error = "feature has no registered weather variables";
			return false;
		}
		std::map<std::string, const WeatherVariables::IWeatherVariable*> variables;
		for (const auto& variable : featureRegistry->GetVariables())
			variables.emplace(variable->GetName(), variable.get());
		for (const auto& [name, setting] : value.items()) {
			if (name == "__enabled")
				continue;
			const auto found = variables.find(name);
			if (found == variables.end()) {
				error = std::format("unknown weather variable '{}.{}'", feature->GetShortName(), name);
				return false;
			}
			if (!found->second->IsValidJsonValue(setting)) {
				error = std::format("invalid value for weather variable '{}.{}'", feature->GetShortName(), name);
				return false;
			}
		}
		return true;
	}

	namespace
	{
		std::filesystem::path AttachmentPath(RE::TESWeather* weather)
		{
			return Util::PathHelpers::GetCommunityShaderPath() / "Weathers" /
			       (WeatherManager::GetWeatherKey(weather) + ".json");
		}
	}

	bool Read(RE::TESWeather* weather, json& root, std::string& error)
	{
		if (!weather) {
			error = "weather is unavailable";
			return false;
		}
		const auto read = Util::FileHelpers::ReadJsonFile(AttachmentPath(weather), root, error);
		if (read == Util::FileHelpers::JsonFileReadResult::NotFound)
			root = json::object();
		else if (read != Util::FileHelpers::JsonFileReadResult::Success)
			return false;
		if (!root.is_object()) {
			error = "weather attachment root is not an object";
			return false;
		}
		return true;
	}

	bool Save(RE::TESWeather* weather, const std::string& featureName, const json* value,
		std::map<std::string, json>& persistedSettings, bool& changed, std::string& error)
	{
		changed = false;
		json root;
		if (!Read(weather, root, error))
			return false;
		auto& settings = root["featureSettings"];
		if (settings.is_null())
			settings = json::object();
		if (!settings.is_object()) {
			error = "weather attachment featureSettings is not an object";
			return false;
		}
		const auto old = settings.find(featureName);
		changed = value ? old == settings.end() || *old != *value : old != settings.end();
		if (value)
			settings[featureName] = *value;
		else
			settings.erase(featureName);
		auto preparedSettings = settings.get<std::map<std::string, json>>();
		if (changed && !Util::FileHelpers::WriteTextFileAtomic(AttachmentPath(weather), root.dump(2), error, false))
			return false;
		persistedSettings = std::move(preparedSettings);
		return true;
	}
}
