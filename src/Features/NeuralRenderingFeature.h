#pragma once

#include "Feature.h"
#include <nlohmann/json.hpp>

/// Owns NR route, character selection and shared colour configuration.
struct NeuralRenderingFeature : Feature
{
	static NeuralRenderingFeature& Instance();
	std::string GetName() override { return "Neural Rendering"; }
	std::string GetShortName() override { return "NeuralRendering"; }
	bool SupportsVR() override { return true; }
	bool IsCore() const override { return true; }
	std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }
	void LoadSettings(nlohmann::json&) override;
	void SaveSettings(nlohmann::json&) override;
	void RestoreDefaultSettings() override;
	/** Uses the installed NR provider's native master toggle for on/off comparisons. */
	bool SupportsPerformanceCostMeasurement() const override;
	bool IsPerformanceToggleEnabled() const override;
	bool IsPerformanceCostMeasurementEnabled() const override;
	bool IsPerformanceCostMeasurementReady() const override;
	void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override { return { "Uses AI to enhance scene detail and actor appearance.", {} }; }
	SettingsHeaderStatus GetSettingsHeaderStatus() const override;
	void DrawSettingsEnabledControl() override;
	std::string_view GetSettingsFooterText() const override;
	void DrawSettings() override;
	void DataLoaded() override;
	void EarlyPrepass() override;

private:
	void DrawColourSettings(bool a_diagnosticsOnly = false);
};
