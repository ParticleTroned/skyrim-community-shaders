#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"

/** Provides downsampled VSM shadow maps for transparent and volumetric shadow consumers. */
struct VolumetricShadows : Feature
{
public:
	virtual inline std::string GetName() override { return "Volumetric Shadows"; }
	virtual std::string GetDisplayName() override { return T("feature.volumetric_shadows.name", "Volumetric Shadows"); }
	virtual bool IsInMenu() const override { return true; }
	virtual inline std::string GetShortName() override { return "VolumetricShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "VOLUMETRIC_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual bool IsCore() const override { return true; }
	bool HasShaderDefine(RE::BSShader::Type) override;

	struct Settings
	{
		bool Enabled = true;
	};

	Settings settings;

	static constexpr uint32_t kSharedShadowMapShaderSlot = 18;

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.volumetric_shadows.description", "Volumetric Shadows provides downsampled VSM shadow maps for use by effects like particles and decals.\nThis improves shadow quality on transparent objects with minimal performance impact."),
			{ T("feature.volumetric_shadows.key_feature_1", "Downsampled VSM shadows"),
				T("feature.volumetric_shadows.key_feature_2", "Gaussian blur filtering"),
				T("feature.volumetric_shadows.key_feature_3", "Multi-cascade support"),
				T("feature.volumetric_shadows.key_feature_4", "Optimized for effects rendering") } };
	}

	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual PerformanceTuningConfig GetPerformanceTuningConfig() const override
	{
		return { 8,
			T("menu.performance_tuning.feature.volumetric_shadows.comparison_label", "Off"),
			T("menu.performance_tuning.feature.volumetric_shadows.comparison_details", "Volumetric Shadows are switched off, so shadow-map downsampling and blur passes stop.") };
	}
	virtual json GetPerformanceTuningUserSettingsMask() const override
	{
		return { { "Enabled", true } };
	}
	virtual bool IsPerformanceTuningApplicable() const override;
	virtual const char* GetPerformanceTuningApplicabilityReason() const override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	virtual void DrawSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	/** Keeps the overview toggle independent of scene activity and resource readiness. */
	bool IsPerformanceToggleEnabled() const override { return settings.Enabled; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override;
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.Enabled = a_enabled; }
	virtual bool IsPerformanceCostMeasurementReady() const override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void PostPostLoad() override;

	void CopyShadowLightData();
	/** Refreshes possible consumers; pass None unless the selected custom pixel family is known. */
	void SetShaderResources(ID3D11DeviceContext* a_context, RE::BSShader::Type a_shaderType);
	/** Null moment maps are not a lit fallback; consumers stay disabled until ready. */
	bool HasSharedShadowMapResources() const { return linearSampler && fallbackShadowSRV; }

	Util::LazyShader<ID3D11ComputeShader> downsampleShadowMip0CS;
	Util::LazyShader<ID3D11ComputeShader> downsampleShadowMip1CS;
	Util::LazyShader<ID3D11ComputeShader> downsampleShadowGeneralMip0CS;
	Util::LazyShader<ID3D11ComputeShader> downsampleShadowGeneralMip1CS;
	Util::LazyShader<ID3D11ComputeShader> blurShadowHorizontalCS;
	Util::LazyShader<ID3D11ComputeShader> blurShadowVerticalCS;

	winrt::com_ptr<ID3D11ShaderResourceView> shadowView;
	uint32_t shadowViewCaptureFrame = UINT32_MAX;

	ID3D11Texture2D* shadowCopyTexture = nullptr;
	ID3D11ShaderResourceView* shadowCopySRV = nullptr;
	ID3D11ShaderResourceView* shadowCopyMip0SRV = nullptr;
	ID3D11ShaderResourceView* shadowCopyMip1SRV = nullptr;
	ID3D11UnorderedAccessView* shadowCopyMip0UAV = nullptr;
	ID3D11UnorderedAccessView* shadowCopyMip1UAV = nullptr;
	uint32_t shadowCopyWidth = 0;
	uint32_t shadowCopyHeight = 0;
	bool shadowCopyValid = false;
	// Retry failed allocation only after shader reset or device recreation.
	bool shadowCopyAllocationFailed = false;
	bool fixedResourceAllocationFailed = false;

	ID3D11Texture2D* fallbackShadowTexture = nullptr;
	ID3D11ShaderResourceView* fallbackShadowSRV = nullptr;

	ID3D11Texture2D* shadowBlurTempTexture = nullptr;
	ID3D11ShaderResourceView* shadowBlurTempMip0SRV = nullptr;
	ID3D11ShaderResourceView* shadowBlurTempMip1SRV = nullptr;
	ID3D11UnorderedAccessView* shadowBlurTempMip0UAV = nullptr;
	ID3D11UnorderedAccessView* shadowBlurTempMip1UAV = nullptr;

	ID3D11SamplerState* linearSampler = nullptr;

private:
	enum class RuntimeReadiness
	{
		Ready,
		NoDirectionalShadows,
		NoRuntimeResources,
		NoCapturedShadowMap,
		ShaderUnavailable,
		OutputResourcesUnavailable
	};

	struct RuntimeContext
	{
		ID3D11DeviceContext* context = nullptr;
		ID3D11ShaderResourceView* directionalShadowSRV = nullptr;
		ID3D11ShaderResourceView* esramShadowSRV = nullptr;
		D3D11_TEXTURE2D_DESC directionalShadowDesc{};
	};

	RuntimeReadiness GetRuntimeReadiness(
		ID3D11ShaderResourceView* a_capturedShadowView,
		bool a_requireOutputResources,
		RuntimeContext* a_context = nullptr) const;

	ID3D11Device* resourceDevice = nullptr;

	static void SetSharedShadowMapSRV(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_srv);

	void CompileComputeShaders();
	void EnsureFallbackShadowTexture();
	/** Creates a complete moment-buffer set or retains the lit fallback. */
	bool EnsureShadowCopyTextures();
	void ReleaseComputeShaders();
	void ReleaseShadowCopyTextures();
	void ReleaseTextures();
	void ReleaseSampler();
	void ReleaseResources();
};
