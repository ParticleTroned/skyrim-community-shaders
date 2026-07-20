#pragma once

#include "Buffer.h"

/** Provides downsampled VSM shadow maps for transparent and volumetric shadow consumers. */
struct VolumetricShadows : Feature
{
public:
	virtual inline std::string GetName() override { return "Volumetric Shadows"; }
	virtual inline std::string GetShortName() override { return "VolumetricShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "VOLUMETRIC_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual bool IsCore() const override { return true; }
	virtual bool SupportsVR() override { return true; }
	virtual bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		bool Enabled = true;
	};

	Settings settings;

	static constexpr uint32_t kSharedShadowMapShaderSlot = 18;

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Volumetric Shadows provides downsampled VSM shadow maps for transparent and volumetric shadow consumers.",
			{ "Downsampled VSM shadows",
				"Gaussian blur filtering",
				"Two-cascade directional shadow support",
				"VR-aware shadow sampling" }
		};
	}

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void PostPostLoad() override;

	void CopyShadowLightData();
	void SetShaderResources(ID3D11DeviceContext* a_context);

	ID3D11ComputeShader* downsampleShadowMip0CS = nullptr;
	ID3D11ComputeShader* downsampleShadowMip1CS = nullptr;
	ID3D11ComputeShader* blurShadowHorizontalCS = nullptr;
	ID3D11ComputeShader* blurShadowVerticalCS = nullptr;

	ID3D11ShaderResourceView* shadowView = nullptr;

	ID3D11Texture2D* shadowCopyTexture = nullptr;
	ID3D11ShaderResourceView* shadowCopySRV = nullptr;
	ID3D11ShaderResourceView* shadowCopyMip0SRV = nullptr;
	ID3D11ShaderResourceView* shadowCopyMip1SRV = nullptr;
	ID3D11UnorderedAccessView* shadowCopyMip0UAV = nullptr;
	ID3D11UnorderedAccessView* shadowCopyMip1UAV = nullptr;
	uint32_t shadowCopyWidth = 0;
	uint32_t shadowCopyHeight = 0;
	bool shadowCopyValid = false;

	ID3D11Texture2D* fallbackShadowTexture = nullptr;
	ID3D11ShaderResourceView* fallbackShadowSRV = nullptr;

	ID3D11Texture2D* shadowBlurTempTexture = nullptr;
	ID3D11ShaderResourceView* shadowBlurTempMip0SRV = nullptr;
	ID3D11ShaderResourceView* shadowBlurTempMip1SRV = nullptr;
	ID3D11UnorderedAccessView* shadowBlurTempMip0UAV = nullptr;
	ID3D11UnorderedAccessView* shadowBlurTempMip1UAV = nullptr;

	ID3D11SamplerState* linearSampler = nullptr;

private:
	ID3D11Device* resourceDevice = nullptr;

	static void SetSharedShadowMapSRV(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_srv);

	void CompileComputeShaders();
	void EnsureFallbackShadowTexture();
	void EnsureShadowCopyTextures();
	void ReleaseComputeShaders();
	void ReleaseTextures();
	void ReleaseSampler();
	void ReleaseResources();
};
