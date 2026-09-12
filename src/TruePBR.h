#pragma once

#include "Feature.h"

struct GlintParameters
{
	bool enabled = false;
	float screenSpaceScale = 1.5f;
	float logMicrofacetDensity = 40.f;
	float microfacetRoughness = .015f;
	float densityRandomization = 2.f;
};

struct TruePBR : Feature
{
public:
	virtual std::string GetName() override { return "True PBR"; }
	virtual std::string GetShortName() override { return "TruePBR"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	virtual bool IsCore() const override { return true; }
	virtual bool SupportsVR() override { return true; }
	virtual bool IsInMenu() const override { return true; }
	virtual bool DrawFailLoadMessage() const override { return false; }
	virtual bool HasFeatureSettings() const override { return false; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return std::make_pair(
			"True PBR replaces Skyrim's legacy material system with physically-based rendering. "
			"Mod authors can supply PBR texture sets that are interpreted in a physically correct BRDF, "
			"producing realistic surface response to lighting across weather and time-of-day conditions.",
			std::vector<std::string>{
				"Physically-based BRDF",
				"Roughness, metallic, and displacement map support",
				"Clearcoat, subsurface scattering, fuzz, and glint layers",
				"Landscape and decal PBR support" });
	}

	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return settings.Enabled != 0; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.Enabled = a_enabled ? 1u : 0u; }
	virtual void SetupResources() override;
	virtual void Prepass() override;
	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	struct alignas(16) Settings
	{
		float VertexAOStrength = 1.0f;
		uint Enabled = true;
		uint pad[2]{};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 16);

	Settings settings;
	/** @brief Controls per-file PBR JSON logging; failures remain visible when disabled. */
	bool enableVerboseJsonLogging = false;

	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	bool BSLightingShader_SetupMaterial(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material);

	void SetShaderResouces(ID3D11DeviceContext* a_context);
	virtual void GenerateShaderPermutations(RE::BSShader* shader) override;

	void SetupGlintsTexture();
	eastl::unique_ptr<Texture2D> glintsNoiseTexture = nullptr;

	std::unordered_map<uint32_t, std::string> editorIDs;

	struct PBRTextureSetData
	{
		float roughnessScale = 1.f;
		float displacementScale = 1.f;
		float specularLevel = 0.04f;

		RE::NiColor subsurfaceColor;
		float subsurfaceOpacity = 0.f;

		RE::NiColor coatColor = { 1.f, 1.f, 1.f };
		float coatStrength = 1.f;
		float coatRoughness = 1.f;
		float coatSpecularLevel = 0.04f;
		float innerLayerDisplacementOffset = 0.f;

		RE::NiColor fuzzColor;
		float fuzzWeight = 0.f;

		GlintParameters glintParameters;
	};

	void SetupTextureSetData();
	void ReloadTextureSetData();
	PBRTextureSetData* GetPBRTextureSetData(const RE::TESForm* textureSet);
	bool IsPBRTextureSet(const RE::TESForm* textureSet);

	void SetupDefaultPBRLandTextureSet();

	std::unordered_map<std::string, PBRTextureSetData> pbrTextureSets;
	RE::BGSTextureSet* defaultPbrLandTextureSet = nullptr;
	RE::BGSTextureSet* originalDefaultLandTextureSet = nullptr;
	bool defaultLandTextureSetReplaced = false;
	std::string selectedPbrTextureSetName;
	PBRTextureSetData* selectedPbrTextureSet = nullptr;

	struct PBRMaterialObjectData
	{
		std::array<float, 3> baseColorScale = { 1.f, 1.f, 1.f };
		float roughness = 1.f;
		float specularLevel = 1.f;

		GlintParameters glintParameters;
	};

	void SetupMaterialObjectData();
	PBRMaterialObjectData* GetPBRMaterialObjectData(const RE::TESForm* materialObject);
	bool IsPBRMaterialObject(const RE::TESForm* materialObject);

	std::unordered_map<std::string, PBRMaterialObjectData> pbrMaterialObjects;
	std::string selectedPbrMaterialObjectName;
	PBRMaterialObjectData* selectedPbrMaterialObject = nullptr;

	RE::BGSTextureSet* currentTextureSet = nullptr;
};
