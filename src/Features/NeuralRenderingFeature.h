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
	void DrawSettings() override;
	bool HasEssentialSettings() const override { return true; }
	void DrawEssentialSettings() override;
	void DataLoaded() override;
	void EarlyPrepass() override;
};
