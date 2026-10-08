#pragma once

#include "Utils/LazyShader.h"

struct IBL : Feature
{
public:
	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	virtual inline std::string GetName() override { return "Image Based Lighting"; }
	virtual inline std::string GetShortName() override { return "ImageBasedLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "IBL"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Lights the scene using colours from the surrounding environment and sky.",
			{ "Projects environment and sky cubemaps into spherical harmonics (SH) for irradiance",
				"Dual IBL sources: environment cubemap (Dynamic Cubemaps) and Skyrim's native sky reflections cubemap",
				"DALC brightness matching to keep IBL consistent with the game's ambient light levels",
				"Configurable per-source intensity, saturation, fog mixing, and per-weather overrides",
				"Static IBL fallback textures for out-of-world objects (e.g. inventory items)" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	Texture2D* envIBLTexture = nullptr;
	Texture2D* skyIBLTexture = nullptr;
	Util::LazyShader<ID3D11ComputeShader> diffuseIBLCS;
	/** @brief Whether the dynamic IBL textures were written by the current pass. */
	bool dynamicIBLValid = false;

	virtual void RestoreDefaultSettings() override;
	/** Supports a cost comparison through the existing in-game enable toggle. */
	bool SupportsPerformanceCostMeasurement() const override { return true; }
	/** Returns the requested state without changing the feature's configuration. */
	bool IsPerformanceCostMeasurementEnabled() const override { return settings.EnableIBL != 0; }
	/** Uses the same weather ownership guard as the native enable checkbox. */
	const char* GetPerformanceToggleBlockReason() const override;
	/** Captures the native weather fallback after a persistent enable change. */
	void SetPerformanceToggleEnabled(bool a_enabled) override;
	/** Restores the enable state and its weather fallback after an overview toggle. */
	void RestorePerformanceToggleState(const json& a_state) override;
	/** Uses the same runtime enable path as the feature's settings panel. */
	void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.EnableIBL = a_enabled ? 1u : 0u; }

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RegisterWeatherVariables() override;

	virtual void ReflectionsPrepass() override;
	virtual void Prepass() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	struct Settings
	{
		uint EnableIBL = 0;
		uint PreserveFogLuminance = 0;
		uint UseStaticIBL = 1;
		float DALCAmount = 0.0f;
		float EnvIBLScale = 0.75f;
		float SkyIBLScale = 1.5f;
		float EnvIBLSaturation = 1.0f;
		float SkyIBLSaturation = 1.0f;
		float FogAmount = 0.0f;
		uint DALCMode = 0;  // 0: Luminance Ratio, 1: Color Ratio, 2: DALC + Sky, 3: DALC + Sky (Directional)
		uint DisableInInteriors = 1;
		uint DisableInWorldMap = 1;
		uint DisableInLoadingScreen = 1;
		bool CaptureWeatherBaselineOnSliderChange = false;
	} settings;

	struct CommonBufferData
	{
		uint EnableIBL;
		uint PreserveFogLuminance;
		uint pad0;
		float DALCAmount;
		float EnvIBLScale;
		float SkyIBLScale;
		float EnvIBLSaturation;
		float SkyIBLSaturation;
		float FogAmount;
		uint DALCMode;
		uint DisableInInteriors;
		uint EnableStaticIBL;
	};

	eastl::unique_ptr<Texture2D> staticDiffuseIBLTexture = nullptr;
	eastl::unique_ptr<Texture2D> staticSpecularIBLTexture = nullptr;

	ID3D11ComputeShader* GetDiffuseIBLCS();
	CommonBufferData GetCommonBufferData() const;
	bool IsDisabledForCurrentScene(const Settings& a_settings) const;
	bool IsRuntimeEnabled() const;
};
