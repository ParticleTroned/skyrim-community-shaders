#pragma once

#include "WeatherVariableRegistry.h"
#include <map>
#include <set>
#include <string>

using json = nlohmann::json;

class WeatherManager
{
public:
	static WeatherManager* GetSingleton()
	{
		static WeatherManager singleton;
		return &singleton;
	}

	struct CurrentWeathers
	{
		RE::TESWeather* currentWeather = nullptr;
		RE::TESWeather* lastWeather = nullptr;
		float lerpFactor = 0.0f;
	};

	// Get current weather state and transition info
	CurrentWeathers GetCurrentWeathers();

	// Load all per-weather settings from disk into cache
	void LoadPerWeatherSettingsFromDisk();

	// Per-frame update - notify features of weather changes
	void UpdateFeatures();

	// Replace the in-memory feature settings for a weather without writing to disk.
	// Used by the editor's explicit Apply path so subsequent transitions use the
	// same values as the live preview.
	void StageSettingsForWeather(
		RE::TESWeather* weather,
		const std::map<std::string, json>& settings);

	// Record a successful atomic save without implicitly applying settings that
	// were edited while auto-apply was disabled.
	void CommitSettingsForWeather(
		RE::TESWeather* weather,
		const std::map<std::string, json>& settings);

	// Re-evaluate the current weather state immediately (for editor Apply and
	// pause/resume changes) without waiting for a weather transition tick.
	void RefreshFeatureOverrides();
	// Rebase transition fallbacks after global feature settings are replaced,
	// then force the next weather update to reassert current ownership.
	void NotifyUserSettingsChanged();

	// Load feature settings for a specific weather
	bool LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json);
	bool GetAllSettingsForWeather(
		RE::TESWeather* weather,
		std::map<std::string, json>& o_settings) const;
	bool IsVariableOverrideActive(
		const std::string& featureName,
		const std::string& variableName) const;
	std::set<std::string> GetActiveVariablesForFeature(
		const std::string& featureName) const;
	void SetFeaturePaused(
		const std::string& featureName,
		bool paused);

	// Get the weather key for caching
	static std::string GetWeatherKey(RE::TESWeather* weather);

	// Clear all cached settings
	void ClearCache();

private:
	WeatherManager() = default;
	~WeatherManager() = default;
	WeatherManager(const WeatherManager&) = delete;
	WeatherManager& operator=(const WeatherManager&) = delete;

	// Cache of all loaded per-weather settings: weatherKey -> featureName -> settings
	std::map<std::string, std::map<std::string, json>> perWeatherSettingsCache;
	// Editor-applied effective values survive render-resource Setup reloads and
	// keep a later Save-without-Apply from changing the live state. A present
	// empty map intentionally masks all persisted settings for that weather.
	std::map<std::string, std::map<std::string, json>> stagedWeatherSettings;

	// Track last known weather state to detect changes
	CurrentWeathers lastKnownWeather;
	std::map<std::string, std::set<std::string>> activeWeatherVariables;
	bool evaluationInvalidated = false;

	// Cached last weather - sky->lastWeather can be cleared before currentWeatherPct reaches 1.0
	RE::TESWeather* cachedLastWeather = nullptr;

	bool PrepareFeatureSettings(const std::string& featureName, json& settings) const;
	std::map<std::string, json> PrepareWeatherSettings(
		RE::TESWeather* weather,
		const std::map<std::string, json>& settings) const;
	void PublishLoadedSettings(
		std::map<std::string, std::map<std::string, json>> loadedSettings);
	void RebaseEditedTransitionSource(
		RE::TESWeather* weather,
		const std::map<std::string, json>& oldSettings,
		const std::map<std::string, json>& newSettings);
	void EvaluateCurrentWeatherState(bool force);
	void ApplyFeatureOverrides(
		const CurrentWeathers& currentWeathers,
		bool transitionStarting,
		bool transitionEnding);
	void RestoreFeatureVariables(
		const std::string& featureName,
		const std::set<std::string>& variableNames);
	void RestoreAllFeatureVariables(const std::string& featureName);
};
