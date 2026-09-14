#pragma once

#include "Feature.h"
#include <nlohmann/json.hpp>

// General NR controls: independent of character classification and selection.
struct NeuralColor : Feature
{
	static NeuralColor& Instance();
	std::string GetName() override { return "Neural Rendering Colour"; }
	std::string GetShortName() override { return "NeuralColor"; }
	bool SupportsVR() override { return true; }
	bool IsCore() const override { return true; }
	std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }
	void LoadSettings(nlohmann::json&) override;
	void SaveSettings(nlohmann::json&) override;
	void RestoreDefaultSettings() override;
	void DrawSettings() override;
	void DataLoaded() override;
	void EarlyPrepass() override;
};
