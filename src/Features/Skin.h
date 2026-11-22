#pragma once

#include "Feature.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// The aligned per-geometry upload data requires padding in its owning feature.
#pragma warning(push)
#pragma warning(disable: 4324)

/** Released skin lighting, detail normals and actor wetness for SE, AE and VR. */
struct Skin : Feature
{
	std::string GetName() override { return "Advanced Skin"; }
	std::string GetShortName() override { return "Skin"; }
	std::string_view GetShaderDefineName() override { return "CS_SKIN"; }
	std::string_view GetShaderCacheAbiVersion() override { return "3"; }
	std::string_view GetCategory() const override { return FeatureCategories::kCharacters; }
	bool SupportsVR() override { return true; }
	bool HasShaderDefine(RE::BSShader::Type a_type) override { return a_type == RE::BSShader::Type::Lighting; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	void DrawSettings() override;
	SettingsHeaderStatus GetSettingsHeaderStatus() const override;
	void LoadSettings(json& a_json) override;
	void SaveSettings(json& a_json) override;
	void RestoreDefaultSettings() override;
	void SetupResources() override;
	void Prepass() override;
	/** Clear save-specific actor history without disturbing an active draw. */
	void ResetRuntimeStateAfterGameLoad();
	bool HasPerformanceSettings() const override { return true; }
	void DrawPerformanceSettings(bool a_advanced) override;
	json CapturePerformanceSettingsState() const override;
	void RestorePerformanceToggleState(const json& a_state) override;
	bool IsPerformanceCostMeasurementReady() const override { return !settings.EnableSkin || (perGeometryCB && resourceError.empty() && pendingTextureCount == 0); }
	const char* GetPerformanceCostMeasurementWaitText() const override { return "Waiting for skin resources"; }
	bool SupportsPerformanceCostMeasurement() const override { return true; }
	bool IsPerformanceCostMeasurementEnabled() const override { return settings.EnableSkin; }
	void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.EnableSkin = a_enabled; }

	struct Settings
	{
		bool EnableSkin = true;
		float SkinMainRoughness = 0.7f;
		float SkinSecondRoughness = 0.35f;
		float SkinSpecularTexMultiplier = 1.0f;
		float SecondarySpecularStrength = 0.15f;
		float F0 = 0.0278f;
		float BaseColorMultiplier = 1.0f;
		float PhysicalMainRoughnessMultiplier = 1.3f;
		float PhysicalSecondRoughnessMultiplier = 0.75f;
		float PhysicalSpecularStrength = 1.0f;
		float ExtraEdgeRoughness = 0.25f;
		bool EnableSkinDetail = true;
		float SkinDetailStrength = 0.25f;
		float SkinDetailTiling = 10.0f;
		float BodyTilingMultiplier = 2.0f;
		float ExtraSkinWetness = 0.0f;
		float WetResponse = 1.0f;
		float WetFadeTime = 10.0f;
		float StartSweat = 0.75f;
		float FullSweat = 0.15f;
		float4 WetParams{ 512.0f, 0.7f, 10.0f, 4.0f };
		float Translucency = 0.1f;
		float sssWidth = 0.2f;
		float SSSAmount = 1.0f;
		bool UseSSS = true;
		float FuzzStrength = 1.0f;
		float FuzzRoughness = 0.35f;
		float FuzzF0 = 0.045f;
	} settings;

	struct alignas(16) SkinData
	{
		float4 skinParams, skinParams2, skinDetailParams, sssParams;
		float4 fuzzParams, physicalParams, wetParams;
		float wetResponse;
		float3 padding;
	};
	static_assert(sizeof(SkinData) == 128);
	static_assert(offsetof(SkinData, wetResponse) == 112);
	/** Packs validated global settings without per-material profiles. */
	SkinData GetCommonBufferData() const;
	/** Prepares actor and optional-map data after the native lighting setup. */
	void BeginPass(RE::BSRenderPass* a_pass);
	/** Restores sampler state even when the feature was disabled during a pass. */
	void EndPass();
	/** Binds pending resources only for an accepted custom lighting draw. */
	void SetShaderResources(ID3D11DeviceContext* a_context);

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspects or atomically applies session settings; never requests compilation. */
	json Control(const json& a_args);
	static json SettingsSchema();
#endif

private:
	struct TexturePathHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view a_path) const noexcept { return std::hash<std::string_view>{}(a_path); }
	};
	struct alignas(16) PerGeometryData
	{
		float4 wetness{};
		// Map presence/encoding, BC5/head bits, then thickness (-1 disables skin shading).
		float4 flags{};
	};
	static_assert(sizeof(PerGeometryData) == 32);
	struct ExtraTextures
	{
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> views;
		uint32_t lastFrame = 0;
		bool pending = true;
		bool wetnessIsGrayscale = false;
	};
	struct ActorWetness
	{
		float4 value{};
		uint32_t frame = 0;
		float time = 0;
	};
	struct SamplerState
	{
		explicit SamplerState(ID3D11DeviceContext* a_context);
		~SamplerState();
		ID3D11DeviceContext* context;
		winrt::com_ptr<ID3D11SamplerState> previous;
	};

	void ResetResources();
	void ReloadTextures();
	bool IsWetnessEnabled() const;
	void LoadDetailTexture();
	ExtraTextures* FindExtraTextures(const RE::BSLightingShaderMaterialBase& a_material);
	float4 GetWetness(RE::BSGeometry* a_geometry, bool a_skinned);
	static float GetWaterHeight(const RE::TESObjectREFR* a_actor, const RE::NiPoint3& a_position);
	static bool IsOverlay(const RE::BSRenderPass& a_pass);
	std::unique_ptr<ConstantBuffer> perGeometryCB;
	winrt::com_ptr<ID3D11ShaderResourceView> detailTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> pendingThickness;
	winrt::com_ptr<ID3D11SamplerState> detailSampler;
	std::optional<SamplerState> samplerState;
	std::unordered_map<std::string, ExtraTextures, TexturePathHash, std::equal_to<>> extraTextures;
	std::unordered_map<uint32_t, ActorWetness> actorWetness;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> pendingViews;
	PerGeometryData previousData{};
	bool hasPreviousData = false;
	bool resourcesDirty = false;
	bool useDetailSampler = false;
	bool detailReconstructZ = false;
	uint32_t lastPruneFrame = 0;
	uint32_t lastTextureLoadFrame = 0;
	size_t pendingTextureCount = 0;
	bool hasTextureLoadFrame = false;
	std::string resourceError;
};

#pragma warning(pop)
