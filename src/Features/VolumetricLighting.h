#pragma once

#include "RE/B/BSVolumetricLightingRenderData.h"
#include "VolumetricLightingTuning.h"

struct VolumetricLighting : Feature
{
public:
	using GodrayProfile = VolumetricLightingTuning::Profile;

	struct TextureSize
	{
		int32_t Width = 320;
		int32_t Height = 192;
		int32_t Depth = 90;
	};

	struct Settings
	{
		bool ExteriorEnabled = true;
		bool DisableWeatherInteractionDuringRain = false;
		GodrayProfile ExteriorGodrays;
		int32_t ExteriorQuality = 2;
		TextureSize ExteriorCustomSize;
		bool InteriorEnabled = true;
		GodrayProfile InteriorGodrays;
		int32_t InteriorQuality = 2;
		TextureSize InteriorCustomSize;
	};

	Settings settings;

	bool enabledAtBoot = false;

	virtual inline std::string GetName() override { return "Volumetric Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.volumetric_lighting.name", "Volumetric Lighting"); }
	virtual inline std::string GetShortName() override { return "VolumetricLighting"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.volumetric_lighting.description", "Volumetric Lighting creates realistic light scattering effects through fog, dust, and atmospheric particles.\nThis adds dramatic god rays and atmospheric depth to both interior and exterior environments."),
			{ T("feature.volumetric_lighting.key_feature_1", "Realistic light scattering"),
				T("feature.volumetric_lighting.key_feature_2", "God rays and atmospheric effects"),
				T("feature.volumetric_lighting.key_feature_3", "Separate interior/exterior settings"),
				T("feature.volumetric_lighting.key_feature_4", "Configurable quality levels"),
				T("feature.volumetric_lighting.key_feature_5", "Enhanced atmospheric immersion") } };
	};

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void ApplyPerformanceSettings() override
	{
		if (initialised)
			SetupVL();
	}
	virtual PerformanceTuningConfig GetPerformanceTuningConfig() const override
	{
		return { 7,
			T("menu.performance_tuning.feature.volumetric_lighting.comparison_label", "Off"),
			T("menu.performance_tuning.feature.volumetric_lighting.comparison_details", "Volumetric Lighting is switched off for the current interior/exterior context.") };
	}
	virtual bool IsPerformanceTuningApplicable() const override;
	virtual const char* GetPerformanceTuningApplicabilityReason() const override;
	virtual json GetPerformanceTuningUserSettingsMask() const override
	{
		return {
			{ "ExteriorEnabled", true },
			{ "DisableWeatherInteractionDuringRain", true },
			{ "ExteriorGodrays", { { "ShaftIntensity", true },
									 { "Opacity", true },
									 { "Saturation", true },
									 { "CustomColorContribution", true },
									 { "CustomColorRed", true },
									 { "CustomColorGreen", true },
									 { "CustomColorBlue", true } } },
			{ "ExteriorQuality", true },
			{ "ExteriorCustomSize", true },
			{ "InteriorEnabled", true },
			{ "InteriorGodrays", { { "ShaftIntensity", true },
									 { "Opacity", true },
									 { "Saturation", true },
									 { "CustomColorContribution", true },
									 { "CustomColorRed", true },
									 { "CustomColorGreen", true },
									 { "CustomColorBlue", true } } },
			{ "InteriorQuality", true },
			{ "InteriorCustomSize", true }
		};
	}
	virtual bool NormalizePerformanceTuningUserSettings(json& a_settings) const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override
	{
		return inInterior ? settings.InteriorEnabled : settings.ExteriorEnabled;
	}
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override
	{
		const Settings defaults{};
		if (a_enabled) {
			settings.ExteriorEnabled = defaults.ExteriorEnabled;
			settings.InteriorEnabled = defaults.InteriorEnabled;
		} else {
			settings.ExteriorEnabled = false;
			settings.InteriorEnabled = false;
		}

		if (initialised)
			SetupVL();
	}
	virtual json CapturePerformanceCostMeasurementState() const override { return CapturePerformanceSettingsState(); }
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override
	{
		auto state = a_state;
		LoadSettings(state);
		if (initialised)
			SetupVL();
	}
	bool IsExteriorEnabled() const;
	void SetExteriorEnabled(bool enabled);
	/** @return The active context's sanitized tuning, or a neutral profile when unavailable. */
	GodrayProfile GetRuntimeGodrayProfile() const;
	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;
	virtual void EarlyPrepass() override;

	virtual bool IsCore() const override { return true; };

	static RE::BSImagespaceShader* CreateShader(const std::string_view& name, const std::string_view& fileName, RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateGenerateCS(RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateRaymarchCS(RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateBlurHCS(RE::BSComputeShader* computeShader);
	RE::BSImagespaceShader* GetOrCreateBlurVCS(RE::BSComputeShader* computeShader);
	/** @brief Whether the current render area is safe for replacement blur dispatch. */
	bool HasValidBlurDimensions() const { return blurDimensionsValid; }
	/** @brief Bind active blur bounds at b1 after selecting a replacement shader. */
	void SetDimensionsCB() const;
	/** @brief Set both active-area dispatch axes for the horizontal blur. */
	void SetGroupCountsHCS(uint32_t& threadGroupCountX, uint32_t& threadGroupCountY) const;
	/** @brief Set both active-area dispatch axes for the vertical blur. */
	void SetGroupCountsVCS(uint32_t& threadGroupCountX, uint32_t& threadGroupCountY) const;

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

	void DrawGodrayTuningSettings();
	void DrawGodrayProfileSettings(const char* label, GodrayProfile& profile);
	void DrawVolumetricLightingSettings(int32_t& quality, TextureSize& customSize, bool isInterior, bool inLocationType);
	TextureSize& FetchCurrentSizeInUnits(bool interior);
	bool TryGetActiveGodrayProfile(GodrayProfile& profile) const;
	void SanitizeSettings();
	void SetupVL();
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

	bool initialised = false;
	bool inInterior = false;
	bool inInteriorWithSun = false;
	bool rainOnlySuppressionActive = false;
	VolumetricLightingDescriptor runtimeDescriptor{};

	struct VLData
	{
		int32_t screenX;
		int32_t screenY;
		int32_t screenXMin1;
		int32_t screenYMin1;
	};
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
