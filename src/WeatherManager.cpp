#include "WeatherManager.h"

#include "Feature.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Form.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace
{
	void NormalizeFeatureWeatherSettings(const std::string& featureName, json& featureSettings)
	{
		if (auto* feature = Feature::FindFeatureByShortName(featureName)) {
			feature->NormalizeWeatherSettings(featureSettings);
		}
	}

	const json* FindEffectiveWeatherValue(
		const std::map<std::string, json>& weatherSettings,
		const std::string& featureName,
		const WeatherVariables::IWeatherVariable& variable)
	{
		const auto featureIt = weatherSettings.find(featureName);
		if (featureIt == weatherSettings.end() || !featureIt->second.is_object())
			return nullptr;

		const auto enabledIt = featureIt->second.find("__enabled");
		if (enabledIt == featureIt->second.end() ||
			!enabledIt->is_boolean() ||
			!enabledIt->get<bool>()) {
			return nullptr;
		}

		const auto valueIt = featureIt->second.find(variable.GetName());
		return valueIt != featureIt->second.end() &&
		               variable.IsValidJsonValue(*valueIt) ?
		           &*valueIt :
		           nullptr;
	}
}

WeatherManager::CurrentWeathers WeatherManager::GetCurrentWeathers()
{
	CurrentWeathers result;

	auto sky = RE::Sky::GetSingleton();
	if (!sky) {
		return result;
	}

	result.currentWeather = sky->currentWeather;
	result.lerpFactor = std::isfinite(sky->currentWeatherPct) ?
	                        std::clamp(sky->currentWeatherPct, 0.0f, 1.0f) :
	                        1.0f;

	// Update cache: store current lastWeather if it exists, otherwise keep the cached one
	if (result.lerpFactor < 1.0f && sky->lastWeather) {
		cachedLastWeather = sky->lastWeather;
	}

	// A cached source weather is meaningful only while a transition is active.
	if (result.lerpFactor < 1.0f)
		result.lastWeather = sky->lastWeather ? sky->lastWeather : cachedLastWeather;
	else
		cachedLastWeather = nullptr;

	return result;
}

void WeatherManager::LoadPerWeatherSettingsFromDisk()
{
	const auto weathersPath = Util::PathHelpers::GetCommunityShaderPath() / "Weathers";
	std::map<std::string, std::map<std::string, json>> loadedSettings;

	std::error_code ec;
	if (!std::filesystem::is_directory(weathersPath, ec)) {
		if (ec && ec != std::errc::no_such_file_or_directory) {
			logger::warn("Failed to inspect Weathers directory ({}): {}", weathersPath.string(), ec.message());
			return;
		}

		ec.clear();
		const bool pathExists = std::filesystem::exists(weathersPath, ec);
		if (ec) {
			logger::warn("Failed to inspect Weathers path ({}): {}", weathersPath.string(), ec.message());
			return;
		}
		if (pathExists) {
			logger::warn("Weathers path exists but is not a directory: {}", weathersPath.string());
			return;
		}

		logger::info("Weathers directory does not exist: {}", weathersPath.string());
		PublishLoadedSettings(std::move(loadedSettings));
		return;
	}

	logger::info("Loading per-weather settings from: {}", weathersPath.string());

	for (std::filesystem::directory_iterator it(weathersPath, ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code entryError;
		if (!it->is_regular_file(entryError)) {
			if (entryError) {
				logger::warn("Failed to inspect weather settings file ({}): {}", it->path().string(), entryError.message());
				return;
			}
			continue;
		}
		if (_stricmp(it->path().extension().string().c_str(), ".json") != 0)
			continue;

		json weatherData;
		std::string readError;
		if (Util::FileHelpers::ReadJsonFile(it->path(), weatherData, readError) !=
			Util::FileHelpers::JsonFileReadResult::Success) {
			logger::warn("Failed to read weather settings file ({}): {}", it->path().string(), readError);
			continue;
		}
		if (!weatherData.is_object()) {
			logger::warn("Ignoring weather settings file with non-object root: {}", it->path().string());
			continue;
		}

		const auto featureSettingsIt = weatherData.find("featureSettings");
		if (featureSettingsIt == weatherData.end())
			continue;
		if (!featureSettingsIt->is_object()) {
			logger::warn("Ignoring malformed featureSettings in {}", it->path().string());
			continue;
		}

		const std::string weatherKey = it->path().stem().string();
		for (const auto& [featureName, storedSettings] : featureSettingsIt->items()) {
			json normalizedSettings = storedSettings;
			if (!PrepareFeatureSettings(featureName, normalizedSettings)) {
				logger::warn(
					"Ignoring malformed {} settings for weather '{}' in {}",
					featureName, weatherKey, it->path().string());
				continue;
			}
			loadedSettings[weatherKey][featureName] = std::move(normalizedSettings);
		}
		if (loadedSettings.contains(weatherKey))
			logger::info("Loaded settings for weather: {}", weatherKey);
	}
	if (ec) {
		logger::warn("Error scanning Weathers directory ({}): {}", weathersPath.string(), ec.message());
		return;
	}

	PublishLoadedSettings(std::move(loadedSettings));
	logger::info("Finished loading per-weather settings. Total weathers: {}", perWeatherSettingsCache.size());
}

void WeatherManager::UpdateFeatures()
{
	EvaluateCurrentWeatherState(false);
}

void WeatherManager::StageSettingsForWeather(
	RE::TESWeather* weather,
	const std::map<std::string, json>& settings)
{
	if (!weather)
		return;

	const std::string weatherKey = GetWeatherKey(weather);
	auto normalizedSettings = PrepareWeatherSettings(weather, settings);
	const auto oldSettingsIt = perWeatherSettingsCache.find(weatherKey);
	static const std::map<std::string, json> emptySettings;
	RebaseEditedTransitionSource(
		weather,
		oldSettingsIt != perWeatherSettingsCache.end() ?
			oldSettingsIt->second :
			emptySettings,
		normalizedSettings);

	stagedWeatherSettings[weatherKey] = normalizedSettings;
	if (normalizedSettings.empty())
		perWeatherSettingsCache.erase(weatherKey);
	else
		perWeatherSettingsCache[weatherKey] = std::move(normalizedSettings);
}

void WeatherManager::CommitSettingsForWeather(
	RE::TESWeather* weather,
	const std::map<std::string, json>& settings)
{
	if (!weather)
		return;

	const std::string weatherKey = GetWeatherKey(weather);
	auto normalizedSettings = PrepareWeatherSettings(weather, settings);

	// The manager cache represents what has actually been applied. Saving is a
	// persistence transaction, not an implicit Apply: if the saved data differs,
	// preserve the current effective state as an overlay so a later disk reload
	// or weather tick cannot apply unapplied editor values.
	const auto effectiveIt = perWeatherSettingsCache.find(weatherKey);
	static const std::map<std::string, json> emptySettings;
	const auto& effectiveSettings =
		effectiveIt != perWeatherSettingsCache.end() ?
			effectiveIt->second :
			emptySettings;
	if (effectiveSettings == normalizedSettings)
		stagedWeatherSettings.erase(weatherKey);
	else
		stagedWeatherSettings[weatherKey] = effectiveSettings;
}

void WeatherManager::RefreshFeatureOverrides()
{
	EvaluateCurrentWeatherState(true);
}

void WeatherManager::NotifyUserSettingsChanged()
{
	const auto currentWeathers = GetCurrentWeathers();
	if (currentWeathers.lerpFactor < 1.0f && currentWeathers.lastWeather) {
		static const std::map<std::string, json> emptySettings;
		const auto sourceIt =
			perWeatherSettingsCache.find(GetWeatherKey(currentWeathers.lastWeather));
		const auto& sourceSettings =
			sourceIt != perWeatherSettingsCache.end() ?
				sourceIt->second :
				emptySettings;

		auto* globalRegistry =
			WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		for (const auto& [featureName, activeVariables] : activeWeatherVariables) {
			auto* registry = globalRegistry->GetFeatureRegistry(featureName);
			if (!registry)
				continue;

			json transitionSource = json::object();
			for (const auto& variable : registry->GetVariables()) {
				if (!activeVariables.contains(variable->GetName()))
					continue;
				if (const auto* value = FindEffectiveWeatherValue(
						sourceSettings, featureName, *variable)) {
					transitionSource[variable->GetName()] = *value;
				}
			}
			globalRegistry->RebaseFeatureTransition(
				featureName, transitionSource, activeVariables);
		}
	}
	evaluationInvalidated = true;
}

bool WeatherManager::LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json)
{
	o_json = json();
	if (!weather) {
		return false;
	}

	std::string weatherKey = GetWeatherKey(weather);

	// Check cache first
	auto weatherIt = perWeatherSettingsCache.find(weatherKey);
	if (weatherIt != perWeatherSettingsCache.end()) {
		auto featureIt = weatherIt->second.find(featureName);
		if (featureIt != weatherIt->second.end()) {
			json featureJson = featureIt->second;
			if (!PrepareFeatureSettings(featureName, featureJson))
				return false;

			const auto enabledIt = featureJson.find("__enabled");
			if (enabledIt == featureJson.end() || !enabledIt->get<bool>())
				return false;

			o_json = std::move(featureJson);
			return true;
		}
	}

	return false;
}

bool WeatherManager::GetAllSettingsForWeather(
	RE::TESWeather* weather,
	std::map<std::string, json>& o_settings) const
{
	o_settings.clear();
	if (!weather)
		return false;

	const auto weatherIt = perWeatherSettingsCache.find(GetWeatherKey(weather));
	if (weatherIt == perWeatherSettingsCache.end())
		return false;

	o_settings = weatherIt->second;
	return true;
}

bool WeatherManager::IsVariableOverrideActive(
	const std::string& featureName,
	const std::string& variableName) const
{
	const auto featureIt = activeWeatherVariables.find(featureName);
	return featureIt != activeWeatherVariables.end() &&
	       featureIt->second.contains(variableName);
}

std::set<std::string> WeatherManager::GetActiveVariablesForFeature(
	const std::string& featureName) const
{
	const auto featureIt = activeWeatherVariables.find(featureName);
	return featureIt != activeWeatherVariables.end() ?
	           featureIt->second :
	           std::set<std::string>{};
}

void WeatherManager::SetFeaturePaused(
	const std::string& featureName,
	bool paused)
{
	auto* registry =
		WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	if (registry->IsFeaturePaused(featureName) == paused)
		return;

	if (paused) {
		registry->SetFeaturePaused(featureName, true);
		RestoreAllFeatureVariables(featureName);
	} else {
		// Paused features are restored to their global values, so any edits made
		// while paused are safe to adopt as the new weather fallback baseline.
		registry->CaptureFeatureUserSettings(featureName);
		registry->SetFeaturePaused(featureName, false);
	}
	RefreshFeatureOverrides();
}

std::string WeatherManager::GetWeatherKey(RE::TESWeather* weather)
{
	return Util::GetFormFileKey(weather);
}

void WeatherManager::ClearCache()
{
	while (!activeWeatherVariables.empty())
		RestoreAllFeatureVariables(activeWeatherVariables.begin()->first);

	stagedWeatherSettings.clear();
	cachedLastWeather = nullptr;
	lastKnownWeather = CurrentWeathers();
	lastKnownWeather.currentWeather = nullptr;
	lastKnownWeather.lastWeather = nullptr;
	lastKnownWeather.lerpFactor = 0.0f;
	LoadPerWeatherSettingsFromDisk();
	logger::info("Reset WeatherManager runtime state and reloaded per-weather settings");
}

bool WeatherManager::PrepareFeatureSettings(const std::string& featureName, json& settings) const
{
	if (!settings.is_object())
		return false;

	const auto enabledIt = settings.find("__enabled");
	if (enabledIt != settings.end() && !enabledIt->is_boolean())
		return false;

	try {
		NormalizeFeatureWeatherSettings(featureName, settings);
	} catch (const nlohmann::json::exception& e) {
		logger::warn("Invalid {} weather settings: {}", featureName, e.what());
		return false;
	} catch (const std::exception& e) {
		logger::warn("Failed to normalize {} weather settings: {}", featureName, e.what());
		return false;
	}

	if (!settings.is_object())
		return false;
	const auto normalizedEnabledIt = settings.find("__enabled");
	return normalizedEnabledIt == settings.end() || normalizedEnabledIt->is_boolean();
}

std::map<std::string, json> WeatherManager::PrepareWeatherSettings(
	RE::TESWeather* weather,
	const std::map<std::string, json>& settings) const
{
	std::map<std::string, json> normalizedSettings;
	for (const auto& [featureName, storedSettings] : settings) {
		json featureSettings = storedSettings;
		if (!PrepareFeatureSettings(featureName, featureSettings)) {
			logger::warn(
				"Ignoring malformed {} settings for weather '{}'",
				featureName, GetWeatherKey(weather));
			continue;
		}
		normalizedSettings[featureName] = std::move(featureSettings);
	}
	return normalizedSettings;
}

void WeatherManager::PublishLoadedSettings(
	std::map<std::string, std::map<std::string, json>> loadedSettings)
{
	for (const auto& [weatherKey, staged] : stagedWeatherSettings) {
		if (staged.empty())
			loadedSettings.erase(weatherKey);
		else
			loadedSettings[weatherKey] = staged;
	}

	const auto currentWeathers = GetCurrentWeathers();
	if (currentWeathers.lerpFactor < 1.0f && currentWeathers.lastWeather) {
		static const std::map<std::string, json> emptySettings;
		const auto weatherKey = GetWeatherKey(currentWeathers.lastWeather);
		const auto oldIt = perWeatherSettingsCache.find(weatherKey);
		const auto newIt = loadedSettings.find(weatherKey);
		RebaseEditedTransitionSource(
			currentWeathers.lastWeather,
			oldIt != perWeatherSettingsCache.end() ?
				oldIt->second :
				emptySettings,
			newIt != loadedSettings.end() ?
				newIt->second :
				emptySettings);
	}

	perWeatherSettingsCache.swap(loadedSettings);
	evaluationInvalidated = true;
}

void WeatherManager::RebaseEditedTransitionSource(
	RE::TESWeather* weather,
	const std::map<std::string, json>& oldSettings,
	const std::map<std::string, json>& newSettings)
{
	const auto currentWeathers = GetCurrentWeathers();
	if (currentWeathers.lerpFactor >= 1.0f ||
		currentWeathers.lastWeather != weather) {
		return;
	}

	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();

	for (const auto& [featureName, activeVariables] : activeWeatherVariables) {
		auto* registry = globalRegistry->GetFeatureRegistry(featureName);
		if (!registry)
			continue;

		json newSourceSettings = json::object();
		std::set<std::string> changedVariables;
		for (const auto& variable : registry->GetVariables()) {
			const auto& variableName = variable->GetName();
			if (!activeVariables.contains(variableName))
				continue;

			const auto* oldValue =
				FindEffectiveWeatherValue(oldSettings, featureName, *variable);
			const auto* newValue =
				FindEffectiveWeatherValue(newSettings, featureName, *variable);
			const bool changed =
				(oldValue == nullptr) != (newValue == nullptr) ||
				(oldValue && newValue && *oldValue != *newValue);
			if (!changed)
				continue;

			changedVariables.insert(variableName);
			if (newValue)
				newSourceSettings[variableName] = *newValue;
		}

		if (!changedVariables.empty()) {
			globalRegistry->RebaseFeatureTransition(
				featureName, newSourceSettings, changedVariables);
		}
	}
}

void WeatherManager::EvaluateCurrentWeatherState(bool force)
{
	const auto currentWeathers = GetCurrentWeathers();
	const bool weatherChanged =
		currentWeathers.currentWeather != lastKnownWeather.currentWeather ||
		currentWeathers.lastWeather != lastKnownWeather.lastWeather;
	const bool transitionStarting =
		currentWeathers.lerpFactor < 1.0f &&
		(weatherChanged ||
			lastKnownWeather.lerpFactor >= 1.0f ||
			currentWeathers.lerpFactor + 0.001f < lastKnownWeather.lerpFactor);
	const bool transitionEnding =
		lastKnownWeather.lerpFactor < 1.0f &&
		currentWeathers.lerpFactor >= 1.0f;
	const bool factorChanged =
		std::abs(currentWeathers.lerpFactor - lastKnownWeather.lerpFactor) >
		0.001f;

	if (!force &&
		!evaluationInvalidated &&
		!weatherChanged &&
		!transitionEnding &&
		!factorChanged) {
		return;
	}

	ApplyFeatureOverrides(
		currentWeathers, transitionStarting, transitionEnding);
	lastKnownWeather = currentWeathers;
	evaluationInvalidated = false;
}

void WeatherManager::ApplyFeatureOverrides(
	const CurrentWeathers& currentWeathers,
	bool transitionStarting,
	bool transitionEnding)
{
	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	const float lerpFactor = std::clamp(currentWeathers.lerpFactor, 0.0f, 1.0f);
	std::set<std::string> evaluatedFeatures;

	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature || !feature->loaded)
			continue;

		const std::string featureName = feature->GetShortName();
		if (!globalRegistry->HasWeatherSupport(featureName))
			continue;
		evaluatedFeatures.insert(featureName);

		// Paused features relinquish weather ownership and expose their global
		// settings for editing. Defensively restore here too if a caller changed
		// the registry pause flag without using SetFeaturePaused().
		if (globalRegistry->IsFeaturePaused(featureName)) {
			globalRegistry->EndFeatureTransition(featureName);
			RestoreAllFeatureVariables(featureName);
			continue;
		}

		json fromWeatherSettings;
		json toWeatherSettings;
		const bool hasFromOverride =
			currentWeathers.lastWeather && lerpFactor < 1.0f &&
			LoadSettingsFromWeather(currentWeathers.lastWeather, featureName, fromWeatherSettings);
		const bool hasToOverride =
			currentWeathers.currentWeather &&
			LoadSettingsFromWeather(currentWeathers.currentWeather, featureName, toWeatherSettings);
		std::set<std::string> controlledVariables;
		if (auto* featureRegistry = globalRegistry->GetFeatureRegistry(featureName)) {
			for (const auto& variable : featureRegistry->GetVariables()) {
				const auto& name = variable->GetName();
				const auto hasValidValue = [&](json& settings) {
					const auto it = settings.find(name);
					if (it == settings.end())
						return false;
					if (variable->IsValidJsonValue(*it))
						return true;
					// Keep malformed values out of the interpolation path so
					// they behave exactly like a missing override.
					settings.erase(it);
					return false;
				};
				const bool hasValidFrom =
					hasFromOverride && hasValidValue(fromWeatherSettings);
				const bool hasValidTo =
					hasToOverride && hasValidValue(toWeatherSettings);
				if (hasValidFrom || hasValidTo) {
					controlledVariables.insert(name);
				}
			}
		}

		auto& previouslyControlled = activeWeatherVariables[featureName];
		std::set<std::string> releasedVariables;
		std::set<std::string> newlyControlledVariables;
		std::ranges::set_difference(
			previouslyControlled,
			controlledVariables,
			std::inserter(releasedVariables, releasedVariables.end()));
		std::ranges::set_difference(
			controlledVariables,
			previouslyControlled,
			std::inserter(
				newlyControlledVariables, newlyControlledVariables.end()));
		RestoreFeatureVariables(featureName, releasedVariables);

		if (controlledVariables.empty()) {
			activeWeatherVariables.erase(featureName);
			continue;
		}

		if (lerpFactor < 1.0f) {
			const auto& variablesToBegin =
				transitionStarting ? controlledVariables : newlyControlledVariables;
			if (!variablesToBegin.empty()) {
				globalRegistry->BeginFeatureTransition(
					featureName, fromWeatherSettings, variablesToBegin);
			}
		}

		activeWeatherVariables[featureName] = controlledVariables;
		globalRegistry->UpdateFeatureFromWeathers(
			featureName, fromWeatherSettings, toWeatherSettings, lerpFactor);

		if (transitionEnding || lerpFactor >= 1.0f)
			globalRegistry->EndFeatureTransition(featureName);
	}

	std::vector<std::string> orphanedFeatures;
	for (const auto& [featureName, _] : activeWeatherVariables) {
		if (!evaluatedFeatures.contains(featureName))
			orphanedFeatures.push_back(featureName);
	}
	for (const auto& featureName : orphanedFeatures)
		RestoreAllFeatureVariables(featureName);
}

void WeatherManager::RestoreFeatureVariables(
	const std::string& featureName,
	const std::set<std::string>& variableNames)
{
	if (variableNames.empty())
		return;
	WeatherVariables::GlobalWeatherRegistry::GetSingleton()->RestoreFeatureUserSettings(
		featureName, variableNames);
}

void WeatherManager::RestoreAllFeatureVariables(const std::string& featureName)
{
	const auto activeIt = activeWeatherVariables.find(featureName);
	if (activeIt == activeWeatherVariables.end())
		return;

	const auto variableNames = activeIt->second;
	activeWeatherVariables.erase(activeIt);
	RestoreFeatureVariables(featureName, variableNames);
}
