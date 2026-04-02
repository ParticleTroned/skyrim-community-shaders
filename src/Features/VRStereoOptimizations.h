#pragma once

#include "Buffer.h"

struct VRStereoOptimizationSettings
{
	enum class Preset : uint32_t
	{
		Quality = 0,
		Performance = 1
	};

	bool Enabled = false;
	Preset Mode = Preset::Quality;

	// Optional near-field full-blend band to preserve strong stereo depth on close geometry.
	bool EnableNearFieldFullBlend = true;
	float NearFieldBlendDistance = 300.0f;
	float NearFieldBlendRange = 180.0f;

	// Safety gate for the known Terrain Blending + Wetness interaction.
	bool DisableWhenTerrainBlendingAndWetness = false;

	void ClampToValidRanges()
	{
		NearFieldBlendDistance = std::clamp(NearFieldBlendDistance, 1.0f, 5000.0f);
		NearFieldBlendRange = std::clamp(NearFieldBlendRange, 1.0f, 5000.0f);
	}
};

NLOHMANN_JSON_SERIALIZE_ENUM(
	VRStereoOptimizationSettings::Preset,
	{
		{ VRStereoOptimizationSettings::Preset::Quality, "Quality" },
		{ VRStereoOptimizationSettings::Preset::Performance, "Performance" },
	})

inline void to_json(nlohmann::json& j, const VRStereoOptimizationSettings& settings)
{
	j = nlohmann::json{
		{ "Enabled", settings.Enabled },
		{ "Mode", settings.Mode },
		{ "EnableNearFieldFullBlend", settings.EnableNearFieldFullBlend },
		{ "NearFieldBlendDistance", settings.NearFieldBlendDistance },
		{ "NearFieldBlendRange", settings.NearFieldBlendRange },
		{ "DisableWhenTerrainBlendingAndWetness", settings.DisableWhenTerrainBlendingAndWetness }
	};
}

inline void from_json(const nlohmann::json& j, VRStereoOptimizationSettings& settings)
{
	settings = VRStereoOptimizationSettings{};
	if (!j.is_object())
		return;

	settings.Enabled = j.value("Enabled", settings.Enabled);
	settings.Mode = j.value("Mode", settings.Mode);
	settings.EnableNearFieldFullBlend = j.value("EnableNearFieldFullBlend", settings.EnableNearFieldFullBlend);
	settings.NearFieldBlendDistance = j.value("NearFieldBlendDistance", settings.NearFieldBlendDistance);
	settings.NearFieldBlendRange = j.value("NearFieldBlendRange", settings.NearFieldBlendRange);
	settings.DisableWhenTerrainBlendingAndWetness = j.value("DisableWhenTerrainBlendingAndWetness", settings.DisableWhenTerrainBlendingAndWetness);
	settings.ClampToValidRanges();
}

class VRStereoOptimizations
{
public:
	void SetupResources();
	void ClearShaderCache();

	bool Prepare(const VRStereoOptimizationSettings& settings, ID3D11ShaderResourceView* depthSRV, uint32_t renderWidth, uint32_t renderHeight);
	void DispatchBlend(const VRStereoOptimizationSettings& settings, ID3D11UnorderedAccessView* mainUAV, ID3D11UnorderedAccessView* normalUAV, ID3D11UnorderedAccessView* motionUAV, ID3D11ShaderResourceView* depthSRV);

	ID3D11ShaderResourceView* GetModeSRV() const
	{
		return modeTexture ? modeTexture->srv.get() : nullptr;
	}
	ID3D11ShaderResourceView* GetFallbackModeSRV() const
	{
		return fallbackModeTexture ? fallbackModeTexture->srv.get() : nullptr;
	}

	bool IsPrepared() const { return preparedThisFrame; }
	bool IsCompatibilityBlocked(const VRStereoOptimizationSettings& settings) const;

private:
	struct PresetTuning
	{
		float disocclusionThreshold = 0.0025f;
		float edgeDepthThreshold = 0.0015f;
		float edgeBandPixels = 2.0f;
		float centerProtection = 0.75f;
		float centerFullBlendThreshold = 0.5f;
	};

	struct alignas(16) StereoOptimizationCB
	{
		float2 renderDim = { 1.0f, 1.0f };
		float2 invRenderDim = { 1.0f, 1.0f };
		float2 centerOffsetLeft = { 0.0f, 0.0f };
		float2 centerOffsetRight = { 0.0f, 0.0f };
		float centerArea = 0.6f;
		float disocclusionThreshold = 0.0025f;
		float edgeDepthThreshold = 0.0015f;
		float edgeBandPixels = 2.0f;
		float centerProtection = 0.75f;
		float centerFullBlendThreshold = 0.5f;
		float nearFieldBlendStart = 220.0f;
		float nearFieldBlendEnd = 380.0f;
		uint32_t enableNearFieldFullBlend = 1u;
		float3 pad0 = { 0.0f, 0.0f, 0.0f };
	};
	STATIC_ASSERT_ALIGNAS_16(StereoOptimizationCB);

	bool EnsureModeTexture(uint32_t renderWidth, uint32_t renderHeight);
	void EnsureFallbackModeTexture();
	void UpdateConstantBuffer(const VRStereoOptimizationSettings& settings, uint32_t renderWidth, uint32_t renderHeight);
	static PresetTuning GetPresetTuning(VRStereoOptimizationSettings::Preset preset);
	void ResetFrameState();

	ID3D11ComputeShader* classifyCS = nullptr;
	ID3D11ComputeShader* blendCS = nullptr;

	eastl::unique_ptr<Texture2D> modeTexture;
	eastl::unique_ptr<Texture2D> fallbackModeTexture;
	eastl::unique_ptr<ConstantBuffer> stereoOptimizationCB;

	bool preparedThisFrame = false;
	bool warnedDepthLayout = false;
	bool warnedCompatibility = false;
	uint32_t dispatchGroupsX = 0;
	uint32_t dispatchGroupsY = 0;
	bool fallbackModeCleared = false;
};
