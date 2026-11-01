#pragma once
#include "Feature.h"

/** Optional single-view neural enhancement; Upscaling retains SE reconstruction and frame-generation ownership. */
struct NeuralRenderingFeature : Feature
{
	std::string GetName() override { return "Neural Rendering"; }
	std::string GetDisplayName() override { return T("feature.neural_rendering.name", "Neural Rendering"); }
	std::string GetShortName() override { return "NeuralRendering"; }
	std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.neural_rendering.description", "Optional neural scene enhancement with single-view character selection and independent inference resolution."),
			{ T("feature.neural_rendering.key_scene", "Scene and character enhancement before the interface is drawn"),
				T("feature.neural_rendering.key_scale", "Separate scene render scale and inference resolution"),
				T("feature.neural_rendering.key_fallback", "Preserves the original scene when the optional runtime is unavailable") } };
	}
	void SetupResources() override;
	SettingsHeaderStatus GetSettingsHeaderStatus() const override;
	void DrawSettingsEnabledControl() override;
	void DrawSettings() override;
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void RestoreDefaultSettings() override;
	void ClearShaderCache() override;
};
