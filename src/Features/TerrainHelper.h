#pragma once

#include "Utils/RuntimeToggle.h"

struct TerrainHelper : Feature
{
private:
	static constexpr std::string_view MOD_ID = "143149";

public:
	virtual inline std::string GetName() override { return "Terrain Helper"; }
	virtual std::string GetDisplayName() override { return T("feature.terrain_helper.name", "Terrain Helper"); }
	virtual inline std::string GetShortName() override { return "TerrainHelper"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_HELPER"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.terrain_helper.description", "Provides enhanced terrain material support for terrain mods that require additional texture slots and parallax mapping capabilities."),
			{ T("feature.terrain_helper.key_feature_1", "Extended texture slot support for terrain materials"),
				T("feature.terrain_helper.key_feature_2", "Parallax mapping integration for terrain textures"),
				T("feature.terrain_helper.key_feature_3", "Automatic terrain material detection and setup"),
				T("feature.terrain_helper.key_feature_4", "Support for advanced terrain modifications"),
				T("feature.terrain_helper.key_feature_5", "Compatibility layer for terrain enhancement mods") } };
	};

	struct Settings
	{
		bool EnableTerrainHelper = true;
	} settings;
	Util::RuntimeToggle enabledRequested{ true };

	struct ExtendedSlots
	{
		std::array<RE::NiSourceTexturePtr, 6> parallax;
	};

	std::shared_mutex extendedSlotsMutex;
	std::unordered_map<uint32_t, ExtendedSlots> extendedSlots;
	RE::BGSTextureSet* defaultLandTexture = nullptr;
	bool featureAvailable = false;

	virtual void Load() override;
	void DrawSettings() override;
	void LoadSettings(json& a_json) override;
	void SaveSettings(json& a_json) override;
	void RestoreDefaultSettings() override;
	bool SupportsPerformanceCostMeasurement() const override { return featureAvailable; }
	bool IsPerformanceCostMeasurementEnabled() const override { return enabledRequested.Get(); }
	void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { enabledRequested.Set(a_enabled); }
	bool IsRuntimeEnabled() const { return featureAvailable && settings.EnableTerrainHelper; }
	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;
	virtual std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }

	void SetShaderResources(ID3D11DeviceContext* a_context);
	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);
};
