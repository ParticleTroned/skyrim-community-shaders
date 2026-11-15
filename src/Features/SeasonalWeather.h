#pragma once

#include "Feature.h"
#include "SeasonalWeatherConfig.h"
#include "Utils/Form.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

struct SeasonalWeather : Feature
{
	// Retain persisted settings and package identifiers across display-name changes.
	std::string GetName() override { return "Seasonal Weather"; }
	std::string GetShortName() override { return "SeasonalWeather"; }
	std::string GetDisplayName() override { return "Seasonal Weathers"; }
	std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	bool SupportsVR() override { return true; }
	bool IsCore() const override { return true; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	void DataLoaded() override;
	void DrawSettingsEnabledControl() override;
	void DrawSettings() override;
	void LoadSettings(json& settings) override;
	void SaveSettings(json& settings) override;
	void RestoreDefaultSettings() override;

	/** Inspects regional candidates or atomically applies a validated settings patch. */
	json Control(const json& request);
	/** Draws the same controls in CS Editor; selected weather can supply regional snow. */
	void DrawEditorSettings(RE::TESWeather* selectedWeather);

private:
	struct Plan;
	using SelectFunction = RE::TESWeather* (*)(RE::TESRegion*);
	static inline SelectFunction originalSelect = nullptr;
	static RE::TESWeather* SelectWeather(RE::TESRegion* region);
	Plan BuildPlan(RE::TESRegion* region) const;
	json Inspect(RE::TESRegion* region, bool includeSettings = true) const;
	void Apply(const json& patch);
	void ApplyEditorEdits(const SeasonalWeatherConfig::Settings& original, const SeasonalWeatherConfig::Settings& edited);
	RE::FormID ResolveConfiguredForm(const std::string& key) const;

	mutable std::mutex mutex;
	SeasonalWeatherConfig::Settings settings;
	mutable std::unordered_map<std::string, RE::FormID> configuredForms;
	mutable Util::FormEditorIDIndex editorIDIndex;
	std::atomic_bool enabled{ false };
	std::atomic_bool available{ false };
	std::atomic_bool faulted{ false };
	std::atomic_bool frameworkDetected{ false };
	std::string installationError;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_uint64_t adjustedSelections{ 0 };
#endif
};
