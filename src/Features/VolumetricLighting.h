#pragma once

#include "RE/B/BSVolumetricLightingRenderData.h"
#include "VolumetricLightingRuntime.h"
#include "VolumetricLightingTuning.h"

#include <atomic>
#include <mutex>

struct VolumetricLighting : Feature
{
public:
	using GodrayProfile = VolumetricLightingTuning::Profile;

	struct TextureSize
	{
		int32_t Width = 320;
		int32_t Height = 192;
		int32_t Depth = 90;

		bool operator==(const TextureSize&) const = default;
	};

	struct Settings
	{
		bool ExteriorEnabled = true;
		bool DisableWeatherInteractionDuringRain = false;
		int32_t ExteriorQuality = 2;
		TextureSize ExteriorCustomSize;
		bool InteriorEnabled = true;
		int32_t InteriorQuality = 2;
		TextureSize InteriorCustomSize;

		bool operator==(const Settings&) const = default;
	};

private:
	Settings settings;
	Settings runtimeSettings;
	mutable std::mutex settingsMutex;

public:
	virtual inline std::string GetName() override { return "Volumetric Lighting"; }
	std::string GetDisplayName() override { return T("feature.volumetric_lighting.name", "Volumetric Lighting"); }
	virtual inline std::string GetShortName() override { return "VolumetricLighting"; }
	virtual std::string_view GetShaderCacheAbiVersion() override { return "godray-depth-separated-haze-5"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.volumetric_lighting.adds_shafts_of_light_and_atmospheric_scatteri_44f3856c", "Adds shafts of light and atmospheric scattering through fog and dust."),
			{ T("feature.volumetric_lighting.key_feature_1", "Realistic light scattering"),
				T("feature.volumetric_lighting.key_feature_2", "God rays and atmospheric effects"),
				T("feature.volumetric_lighting.key_feature_3", "Separate interior/exterior settings"),
				T("feature.volumetric_lighting.key_feature_4", "Configurable quality levels"),
				T("feature.volumetric_lighting.key_feature_5", "Enhanced atmospheric immersion") }
		};
	}

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	/** Returns the requested enable setting for the current interior/exterior context. */
	bool IsPerformanceToggleEnabled() const override;
	virtual bool IsPerformanceCostMeasurementEnabled() const override;
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	/** Uses the native runtime control without replacing the current tuning. */
	void SetPerformanceToggleEnabled(bool a_enabled) override;
	virtual json CapturePerformanceCostMeasurementState() const override;
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override;
	/** @return The latest requested exterior setting, including changes awaiting a safe frame. */
	bool IsExteriorEnabled() const;
	/** @brief Stage an exterior preference from any thread; never touches engine or graphics state. */
	void SetExteriorEnabled(bool enabled);
	/** @brief Reconcile engine state at the next safe render boundary after a load. */
	void RequestRuntimeReset();
	bool IsPerformanceCostMeasurementReady() const override;
	const char* GetPerformanceCostMeasurementWaitText() const override { return T("feature.volumetric_lighting.waiting_for_volumetric_lighting_transition", "Waiting for volumetric lighting transition"); }
	/** @return Adaptive Balance tuning for the active renderer context, or neutral when unavailable. */
	GodrayProfile GetRuntimeGodrayProfile() const;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;
	virtual void EarlyPrepass() override;





	virtual bool IsCore() const override { return true; };

	static RE::BSImagespaceShader* CreateShader(const std::string_view& name, const std::string_view& fileName, RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateGenerateCS(RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateRaymarchCS(RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateBlurHCS(RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateBlurVCS(RE::BSComputeShader* computeShader);
	/** @brief Whether the current render area and camera are safe for replacement blur dispatch. */
	bool HasValidBlurDimensions() const { return blurDimensionsValid; }
	/** @brief Bind active blur bounds and camera data at b1 after selecting a replacement shader. */
	void SetDimensionsCB() const;
	/** @brief Set both dispatch axes within the active render area. */
	void SetGroupCountsHCS(uint32_t& threadGroupCountX, uint32_t& threadGroupCountY) const;
	/** @brief Set both active-area dispatch axes for the vertical blur. */
	void SetGroupCountsVCS(uint32_t& threadGroupCountX, uint32_t& threadGroupCountY) const;

	// hooks





private:
	using VolumetricLightingDescriptor = RE::BSVolumetricLightingRenderData;

	struct ApplyVolumetricLighting_VolumetricLightingDescriptor_Get
	{
		static VolumetricLightingDescriptor* thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static const char* FromUnits(int32_t value, int32_t unitScale);
	static VolumetricLightingDescriptor& GetVLDescriptor();
	static void SetVLQuality(VolumetricLightingDescriptor& descriptor, std::uint32_t quality);

	void DrawVolumetricLightingSettings(int32_t& quality, TextureSize& customSize, bool isInterior);
	TextureSize& FetchCurrentSizeInUnits(bool interior);
	bool TryGetActiveGodrayProfile(GodrayProfile& profile) const;
	void SanitizeSettings();
	bool IsRuntimeTransitionBlocked() const;
	void ApplyRuntimeTarget(const VolumetricLightingRuntime::Target& target, const VolumetricLightingRuntime::Changes& changes);
	void UpdateBlurDimensions();
	void ClearVolumetricLightingTargets();
	static int32_t ClampQualityIndex(int32_t quality);
	static TextureSize ClampTextureSize(const TextureSize& size);

	enum class Quality : uint8_t
	{
		Low,
		Medium,
		High,
		Custom,
		Count
	};

	const char* QualityNames[static_cast<uint8_t>(Quality::Count)] = { "Low", "Medium", "High", "Custom" };

	TextureSize exteriorSizeInUnits;
	TextureSize interiorSizeInUnits;
	TextureSize defaultSizeHigh;

	TextureSize* gVolumetricLightingSizeHigh = nullptr;
	TextureSize* gVolumetricLightingSizeMedium = nullptr;
	TextureSize* gVolumetricLightingSizeLow = nullptr;

	std::atomic_bool runtimeResetRequested{ true };
	std::atomic_bool runtimeReady{ false };
	VolumetricLightingRuntime::Controller runtimeController;
	bool runtimeEnabled = false;
	bool initialised = false;
	bool inInterior = false;
	VolumetricLightingDescriptor runtimeDescriptor{};

	struct VLData
	{
		int32_t screenX;
		int32_t screenY;
		int32_t screenXMin1;
		int32_t screenYMin1;
		float4 cameraData;
	};
	STATIC_ASSERT_ALIGNAS_16(VLData);
	static_assert(sizeof(VLData) == 32);
	VLData vlData = VLData();
	ConstantBuffer* vlDataCB = nullptr;
	bool blurDimensionsValid = false;
	int32_t fullScreenX = 0;
	int32_t fullScreenY = 0;

	static constexpr int32_t BlurThreadGroupSizeX = 256;
	static constexpr int32_t BlurThreadGroupSizeY = 256;
	static constexpr int32_t BlurWindow = 12;

	RE::BSImagespaceShader* generateCS = nullptr;
	RE::BSImagespaceShader* raymarchCS = nullptr;
	RE::BSImagespaceShader* blurHCS = nullptr;
	RE::BSImagespaceShader* blurVCS = nullptr;
};
