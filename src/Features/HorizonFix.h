#pragma once

#include "Feature.h"

/**
 * @brief Enables Water shader compatibility while HorizonFix.dll is installed.
 *
 * Horizon Fix fills the gap between distant water and the sky with water geometry
 * beyond Skyrim's vanilla far clip plane. The Water shader must fold that geometry
 * back inside the depth range and treat the far-depth background as bottomless water.
 */
struct HorizonFix : Feature
{
	virtual inline std::string GetName() override { return "Horizon Fix"; }
	virtual std::string GetDisplayName() override { return T("feature.horizon_fix.name", "Horizon Fix"); }
	virtual inline std::string GetShortName() override { return "HorizonFix"; }
	virtual inline std::string_view GetShaderDefineName() override { return "HORIZON_FIX"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type a_shaderType) override { return a_shaderType == RE::BSShader::Type::Water; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.horizon_fix.description", "Enables water rendering beyond the far clip plane in support of the HorizonFix plugin, which fills the horizon gap between the farthest visible water and the sky."),
			{ T("feature.horizon_fix.key_feature_1", "Active only while the HorizonFix SKSE plugin is installed."),
				T("feature.horizon_fix.key_feature_2", "Without HorizonFix, water keeps exact vanilla far clip behavior.") } };
	}

	virtual void DrawSettings() override;
	virtual void PostPostLoad() override;
	virtual bool IsInMenu() const override;
	virtual bool HasFeatureSettings() const override { return false; }
	virtual bool IsCore() const override { return true; }
};
