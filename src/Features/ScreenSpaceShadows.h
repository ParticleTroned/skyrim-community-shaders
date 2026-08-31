#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct ScreenSpaceShadows : Feature
{
private:
	static constexpr std::string_view MOD_ID = "93209";

public:
	virtual inline std::string GetName() override { return "Screen Space Shadows"; }
	virtual inline std::string GetShortName() override { return "ScreenSpaceShadows"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline bool IsCore() const override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "SCREEN_SPACE_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Screen Space Shadows enhances shadow quality by adding detailed contact shadows and improving shadow accuracy.\n"
			"This technique adds fine-detail shadows that traditional shadow mapping might miss.",
			{ "Enhanced contact shadows",
				"Improved shadow detail",
				"Better shadow accuracy",
				"Fine-scale shadow effects",
				"Configurable shadow contrast" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct BendSettings
	{
		float SurfaceThickness = !globals::game::isVR ? 0.02f : 0.010f;
		float BilinearThreshold = 0.02f;
		float ShadowContrast = !globals::game::isVR ? 1.0f : 4.0f;
		uint Enable = 1;
		uint SampleCount = 1;
		float VRBaseSamplesAtReference = 44.0f;
		float VRCullDistance = 0.0f;  // 0 = disabled
		uint EnableFoveated = globals::game::isVR ? 1u : 0u;
	};

	BendSettings bendSettings;

	struct alignas(16) RaymarchCB
	{
		// Runtime data returned from BuildDispatchList():
		float LightCoordinate[4];  // Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
		int WaveOffset[2];         // Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

		// Renderer Specific Values:
		float FarDepthValue;   // Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
		float NearDepthValue;  // Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

		// Sampling data:
		float InvDepthTextureSize[2];  // Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
									   // If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
									   // The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

		float2 DynamicRes;
		uint DynamicSampleCount;
		uint DynamicReadCount;
		float pad0[2];
		float FoveatedData0[4];  // x=centerScale, y=centerFeather, z=centerHorizontalScale, w=enabled
		float FoveatedCenterOffset[4];

		BendSettings settings;
	};
	STATIC_ASSERT_ALIGNAS_16(RaymarchCB);

	bool enableStereoSync = false;
	// Optional VR-only path: transfer Eye 0's shadow into Eye 1 and skip the Eye 1 march.
	bool useStereoReproject = false;

	struct alignas(16) StereoSyncCB
	{
		float FrameDim[2];
		float RcpFrameDim[2];
		float DispatchBase[2];
		float DispatchExtent[2];
		float FoveatedData0[4];  // x=centerScale, y=centerFeather, z=centerHorizontalScale, w=enabled
		float FoveatedCenterOffset[4];
	};
	STATIC_ASSERT_ALIGNAS_16(StereoSyncCB);

	ID3D11SamplerState* pointBorderSampler = nullptr;

	ConstantBuffer* raymarchCB = nullptr;
	struct RaymarchShaderVariant
	{
		bool valid = false;
		uint sampleCount = 0;
		bool usesTerrainBlendingDepth = false;
		uint64_t lastUse = 0;
		Util::LazyShader<ID3D11ComputeShader> left;
		Util::LazyShader<ID3D11ComputeShader> right;
	};
	// Binaries that can run VR retain native plus every supported render-scale
	// profile. Flat-only builds need only the active variant.
#ifdef ENABLE_SKYRIM_VR
	static constexpr std::size_t kRaymarchShaderVariantCount = 8;
#else
	static constexpr std::size_t kRaymarchShaderVariantCount = 1;
#endif
	std::array<RaymarchShaderVariant, kRaymarchShaderVariantCount> raymarchShaderVariants{};
	uint64_t raymarchShaderUseCounter = 0;
	uint compiledSampleCount = 0;

	Texture2D* screenSpaceShadowsTexture = nullptr;

	// VR stereo sync resources
	Texture2D* stereoSyncCopyTex = nullptr;
	ConstantBuffer* stereoSyncCB = nullptr;
	Util::LazyShader<ID3D11ComputeShader> stereoSyncCS;
	bool stereoSyncUsesTerrainBlendingDepth = false;
	Util::LazyShader<ID3D11ComputeShader> stereoReprojectCS;
	bool stereoReprojectUsesTerrainBlendingDepth = false;

	ID3D11ComputeShader* GetStereoReprojectCS();

	virtual void SetupResources() override;
	virtual void SetupRenderTargetResources() override;

	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return bendSettings.Enable != 0; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	virtual json CapturePerformanceCostMeasurementState() const override;
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override;
	void DrawFoveationSettings();

	virtual void ClearShaderCache() override;
	void InvalidateRaymarchShaders();
	uint GetScaledSampleCount(bool a_dynamic);
	uint GetScaledSampleCountForRenderSize(float2 a_screenSize) const;
	ID3D11ComputeShader* GetOrCreateRaymarchShader(
		uint a_sampleCount,
		bool a_rightEye,
		Util::ShaderCompileTiming* a_timing = nullptr);
	ID3D11ComputeShader* GetComputeRaymarch();
	ID3D11ComputeShader* GetComputeRaymarchRight();
	/** @brief Compiles the bounded VR raymarch pair for an admitted target size. */
	bool PrewarmVRRenderScaleShaders(
		uint32_t a_combinedWidth,
		uint32_t a_height,
		Util::ShaderCompileTiming* a_timing = nullptr);

	virtual void Prepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	void DrawShadows();
	void DrawStereoSync();

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
};
