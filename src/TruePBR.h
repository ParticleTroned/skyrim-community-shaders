#pragma once

#include "Feature.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#endif

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
	virtual void RestoreDefaultSettingsForLoad() override { settings = {}; }

	struct alignas(16) Settings
	{
		float VertexAOStrength = 1.0f;
		uint Enabled = true;
		uint GrassEnabled = false;
		uint pad{};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 16);

	Settings settings;
	/** @brief Controls per-file PBR JSON logging; failures remain visible when disabled. */
	bool enableVerboseJsonLogging = false;

	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	/** Preserve authored grass maps before the engine interns the material. */
	void SetupGrassMaterial(RE::BSLightingShaderProperty* source, RE::BSLightingShaderProperty* grass);
	/** Identify authored grass without dereferencing an unregistered PBR material. */
	[[nodiscard]] bool IsPBRGrassMaterial(const RE::BSShaderMaterial* material) const;
	/** Bind authored grass through the unchanged native technique and geometry ABI. */
	void SetupGrassShaderMaterial(RE::BSShader* shader, const RE::BSLightingShaderMaterialBase* material);
	/** Report whether the requested material path has both lighting features available. */
	[[nodiscard]] bool IsPBRGrassEnabled() const;
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Enable bounded material-setup counters independently of rendering settings. */
	void SetGrassDiagnosticsEnabled(bool enabled);
	/** Guard all additional grass measurement work in diagnostic builds. */
	[[nodiscard]] bool IsGrassDiagnosticsEnabled() const { return grassDiagnosticsEnabled.load(std::memory_order_relaxed); }
	/** Snapshot setup counters; these count material binds rather than instances. */
	[[nodiscard]] json GetGrassDiagnostics() const;
#endif
	/** @brief Reports whether the raw lighting technique uses custom material setup instead of native setup. */
	[[nodiscard]] bool UsesCustomMaterialSetup(uint32_t rawTechnique) const;
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
		float specularLevel = 0.04f;

		GlintParameters glintParameters;
	};

	void SetupMaterialObjectData();
	PBRMaterialObjectData* GetPBRMaterialObjectData(const RE::TESForm* materialObject);
	bool IsPBRMaterialObject(const RE::TESForm* materialObject);

	std::unordered_map<std::string, PBRMaterialObjectData> pbrMaterialObjects;
	std::string selectedPbrMaterialObjectName;
	PBRMaterialObjectData* selectedPbrMaterialObject = nullptr;

	RE::BGSTextureSet* currentTextureSet = nullptr;

private:
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool grassDiagnosticsEnabled{ false };
	std::atomic_uint64_t grassMaterialsCreated{ 0 };
	std::atomic_uint64_t grassMaterialSetups{ 0 };
	std::atomic_uint64_t grassPBRMaterialSetups{ 0 };
	std::atomic_uint64_t grassShaderFallbackSetups{ 0 };
	std::atomic_uint64_t grassMissingRmaos{ 0 };
#endif
};
