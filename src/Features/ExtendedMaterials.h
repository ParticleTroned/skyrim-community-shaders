#pragma once

#include "Buffer.h"

struct ExtendedMaterials : Feature
{
	virtual inline std::string GetName() override { return "Extended Materials"; }
	virtual std::string GetDisplayName() override { return T("feature.extended_materials.name", "Extended Materials"); }
	virtual inline std::string GetShortName() override { return "ExtendedMaterials"; }
	virtual inline std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.extended_materials.description", "Extended Materials adds advanced material effects including parallax occlusion mapping and complex material blending.\nThis feature enhances surface detail and depth perception for more realistic textures."),
			{ T("feature.extended_materials.key_feature_1", "Parallax occlusion mapping for depth"),
				T("feature.extended_materials.key_feature_2", "Complex material blending"),
				T("feature.extended_materials.key_feature_3", "Terrain heightmap support"),
				T("feature.extended_materials.key_feature_4", "Parallax shadows"),
				T("feature.extended_materials.key_feature_5", "Height-based texture blending") } };
	};

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct alignas(16) Settings
	{
		uint EnableComplexMaterial = 1;

		uint EnableParallax = 1;
		uint EnableTerrain = 0;
		uint EnableHeightBlending = 1;

		uint EnableShadows = 1;
		uint EnableParallaxWarpingFix = 1;

		uint pad[2]{};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual void DataLoaded() override;

	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual PerformanceTuningConfig GetPerformanceTuningConfig() const override
	{
		return { 16,
			T("menu.performance_tuning.feature.extended_materials.comparison_label", "Off"),
			T("menu.performance_tuning.feature.extended_materials.comparison_details", "complex materials, parallax, legacy terrain parallax, height blending, parallax shadows, and curvature correction are switched off.") };
	}
	virtual json GetPerformanceTuningUserSettingsMask() const override
	{
		return {
			{ "EnableComplexMaterial", true },
			{ "EnableParallax", true },
			{ "EnableTerrain", true },
			{ "EnableHeightBlending", true },
			{ "EnableShadows", true },
			{ "EnableParallaxWarpingFix", true }
		};
	}
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override;
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	virtual json CapturePerformanceCostMeasurementState() const override;
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; };

private:
	static void SanitizeSettings(Settings& a_settings);
};
