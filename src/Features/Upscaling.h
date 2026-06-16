#pragma once

#include "Feature.h"
#include "Upscaling/AAVRSController.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/FoveatedRegionPlan.h"
#include "Upscaling/RCAS/RCAS.h"
#include "Upscaling/Streamline.h"
#include <atomic>
#include <array>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <functional>
#include <limits>
#include <openvr.h>
#include <string>
#include <vector>
#include <winrt/base.h>

namespace RE
{
	class BSRenderPass;
}

/**
 * @brief Provides upscaling functionality including DLSS, FSR and TAA.
 *
 * This feature handles various upscaling methods and frame generation technologies
 * to improve performance while maintaining visual quality.
 */
struct Upscaling : Feature
{
public:
	// Feature interface
	virtual inline std::string GetName() override { return "Upscaling"; }
	virtual inline std::string GetShortName() override { return "Upscaling"; }
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return true; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Advanced upscaling and frame generation technologies for improved performance",
			{ "DLSS (Deep Learning Super Sampling) support",
				"FSR (FidelityFX Super Resolution) support",
				"TAA (Temporal Anti-Aliasing) support",
				"Frame generation for supported systems" }
		};
	}

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};

	enum class ResolutionOwner : uint8_t
	{
		Native,
		VendorDynamicResolution,
		VRRenderScaleMode,
		PerfMode = VRRenderScaleMode  // Legacy alias for VRAPI/source compatibility.
	};

	enum class UpscalingOutputTarget : uint8_t
	{
		Main,
		Sharpener,
		SubmitStageIntermediate
	};

	enum class VRRenderScaleStatus : uint8_t
	{
		Disabled,
		IneligibleMethod,
		NativeQuality,
		RuntimeBlocked,
		SubmitStageOnly,
		PendingRelatch,
		Active,
		RestartRequired
	};
	enum class VRUpscalingTransitionOrigin : uint8_t
	{
		CSMenu,
		VRAPI,
		PostLoadSync
	};

	// Shared DLSS/FSR/FSR4 render-scale presets:
	// 0=Native AA/DLAA, 1=Hoshipa, 2=Ultra Quality, 3=Quality,
	// 4=Balanced, 5=Performance, 6=Ultra Performance
	static constexpr uint32_t kQualityModeMaxIndex = 6;
	static constexpr uint32_t kDLSSPresetMaxIndex = 4;  // 0=J, 1=K, 2=L, 3=M, 4=F
	static constexpr uint32_t kPendingVRUpscalingSettingUnset = std::numeric_limits<uint32_t>::max();

	static constexpr float GetQualityModeResolutionScale(uint32_t a_qualityMode)
	{
		switch (a_qualityMode) {
		case 1:
			return 0.85f;
		case 2:
			return 1.0f / 1.3f;
		case 3:
			return 1.0f / 1.5f;
		case 4:
			return 1.0f / 1.7f;
		case 5:
			return 0.5f;
		case 6:
			return 1.0f / 3.0f;
		default:
			return 1.0f;
		}
	}

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 0;  // Shared upscaler preset; defaults to DLAA / Native AA
		uint dlssPreset = 1;   // 0=J, 1=K, 2=L, 3=M, 4=F (default K)
		uint renderScaleMode = 0;
		bool vrFpsStabilizerSync = false;
		uint perfMode = 0;
		bool aaVrs = false;
		bool aaVrsVisualization = false;
		bool aaVrsPerformanceMode = false;
		uint aaVrsPerformanceAnisotropy = 0;  // 0=auto, 1=2x1, 2=1x2
		bool aaVrsPassAware = true;
		bool aaVrsContentAware = true;
		bool aaVrsProtectWater = true;
		bool aaVrsSafeOpaqueOnly = false;
		uint aaVrsMaxRate = 0;  // 0=2x2, 1=4x4
		bool aaVrsPassTelemetry = false;
		bool experimentalDeferredCompositePS = false;
		bool aaVrsDeferredComposite = false;
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
		bool frameGenerationAllowInMenus = false;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
		float sharpnessFSR = 0.0f;
		float sharpnessDLSS = 0.1f;
		bool fsr4RuntimeEnable = true;
		bool foveatedVendorDispatch = false;
		float foveatedCenterArea = 0.6f;
		float foveatedCenterHorizontalScale = 1.0f;
		float foveatedLeftEyeMaskOffsetX = 0.0f;
		float foveatedLeftEyeMaskOffsetY = 0.0f;
		float foveatedRightEyeMaskOffsetX = 0.0f;
		float foveatedRightEyeMaskOffsetY = 0.0f;
		float periphery_taa_center_area = 0.6f;
		bool foveatedPeripheryMaskVisualization = false;
		bool periphery_taa_enable = false;
		float periphery_taa_outer_scale = 0.70f;
		float periphery_taa_center_blend_feather = 0.05f;
		bool reflexLowLatencyMode = true;
		bool reflexLowLatencyBoost = false;
		bool reflexUseMarkersToOptimize = true;
		bool reflexUseFPSLimit = false;
		float reflexFPSLimit = 60.0f;
	};

	Settings settings;

	enum class AAVRSPassPolicyReason : uint8_t
	{
		None,
		AlphaTest,
		ShaderPropertyAlpha,
		ShaderPropertyDecal,
		ShaderPropertyEmissive,
		ShaderPropertyHighFrequency,
		EffectShader,
		ParticleShader,
		WaterShader,
		GrassShader,
		DistantTreeShader,
		BloodSplatterShader,
		SkyShader,
		LightingTechnique,
		LightingDescriptor,
		UtilityDescriptor,
		SafeOpaqueOnly,
		DecalPhase,
		Count
	};
	static constexpr size_t kAAVRSPassPolicyReasonCount = static_cast<size_t>(AAVRSPassPolicyReason::Count);

	struct RuntimeResolutionPlan
	{
		UpscaleMethod upscaleMethod = UpscaleMethod::kNONE;
		ResolutionOwner owner = ResolutionOwner::Native;
		UpscalingOutputTarget outputTarget = UpscalingOutputTarget::Main;
		uint32_t qualityMode = 0;
		float2 trueHMDDisplaySize{ 0.0f, 0.0f };
		float2 engineRenderSize{ 0.0f, 0.0f };
		float2 finalOutputSize{ 0.0f, 0.0f };
		bool vendorMethod = false;
		bool foveatedActive = false;
		bool peripheryTAAActive = false;
		bool menuContextActive = false;
		bool knownMenuContextActive = false;
		bool loadingMenuActive = false;
		bool perfModeRestartRequired = false;
		FoveatedRegionPlan foveatedRegion{};
	};

	struct PerfModeState
	{
		struct BootSnapshot
		{
			bool valid = false;
			bool active = false;
			UpscaleMethod method = UpscaleMethod::kNONE;
			uint32_t qualityMode = 0;
			float renderScale = 1.0f;
			uint32_t displayEyeWidth = 0;
			uint32_t displayEyeHeight = 0;
			uint32_t renderEyeWidth = 0;
			uint32_t renderEyeHeight = 0;
		};

		void ResetBootLatch();
		void RecordTrueHMDSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight);
		bool IsRequested(const Settings& a_settings) const;
		bool IsEligible(const Settings& a_settings, UpscaleMethod a_method) const;
		void UpdateRestartRequiredState(const Settings& a_settings, UpscaleMethod a_method);
		bool EnsureBootLatch(const Settings& a_settings, UpscaleMethod a_method, bool a_allowCreate);
		bool IsActive(const Settings& a_settings, UpscaleMethod a_method) const;
		bool TryGetOpenVRRenderTargetSize(const Settings& a_settings, UpscaleMethod a_method, uint32_t& a_width, uint32_t& a_height, bool a_allowCreate);
		float2 GetDisplayScreenSize() const;
		float2 GetRenderScreenSize() const;
		const BootSnapshot& GetBootSnapshot() const { return boot; }
		bool HasKnownHMDSize() const { return trueHMDEyeWidth != 0 && trueHMDEyeHeight != 0; }
		bool HasRestartRequiredChange() const { return restartRequired; }

		uint32_t trueHMDEyeWidth = 0;
		uint32_t trueHMDEyeHeight = 0;
		BootSnapshot boot{};
		bool restartRequired = false;
		bool displaySizeChanged = false;
	};

	PerfModeState perfMode;
	RuntimeResolutionPlan runtimeResolutionPlan;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	struct UpscalingDataCB
	{
		float2 dispatchDim;      // Current dispatch/output dimensions (per-eye in VR, full in flat)
		float2 trueSamplingDim;  // BufferDim.xy * ResolutionScale
		float2 invTrueSamplingDim;
		float seamCenterX;
		float seamHalfWidthPx;
		float maskDepthThreshold;
		float vrSeamHardening;
		float2 sourceOffset;  // Source offset in combined stereo inputs
		float2 outputOffset;  // Output offset in per-eye intermediates
		float2 pad;
	};

	struct DynamicResolutionStretchCB
	{
		float2 inputSize;
		float2 outputSize;
		float2 sourceTextureSize;
		float2 padding;
	};

	struct VRMenuCompositeCB
	{
		float2 sourceScale;
		float2 sourceOffset;
	};

	struct FoveatedPeripheryCB
	{
		float2 outputDim;
		float2 invOutputDim;
		float2 invSourceDim;
		float2 sourceScale;
		float2 sourceOffset;
		float2 dispatchDim;
		float2 outputOffset;
		float2 jitter;
		float4 centerAndMask;  // xy=centerOffset, z=visualizeMask, w=showThreeZoneMask
		float4 tuning0;        // x=centerScale, y=centerFeather, z=centerHorizontalScale, w=taaOuterScale
	};

	struct FoveatedCenterBlendCB
	{
		float2 invOutputDim;
		float centerScale;
		float centerFeather;
		float2 centerOffset;
		float2 outputOffset;
		float2 dispatchDim;
		float2 sourceOffset;
		float2 invSourceDim;
		float centerHorizontalScale;
		float centerHorizontalScalePadding;
	};

	struct PeripheryTAACB
	{
		float2 outputDim;
		float2 invOutputDim;
		float2 inputDim;
		float2 invInputDim;
		float2 inputTextureScale;
		float2 inputTextureOffset;
		float2 dispatchDim;
		float2 outputOffset;
		float2 jitter;
		float2 centerOffset;
		float4 tuning0;  // x=centerScale, y=centerFeather, z=resetHistory, w=taaOuterScale
		float4 tuning1;  // x=historyValid, y=centerHorizontalScale, z=tileDispatch, w=tileDispatchWidth
		float4 tuning2;  // x=reactivityScale, y=instabilityScale, z=velocityScale, w=lockDecay
		float4 tuning3;  // xy=min output color-write bounds, zw=max output color-write bounds
		float4x4 currentViewProjInverse;
		float4x4 previousViewProj;
		float4 currentCameraPosAdjust;
		float4 previousCameraPosAdjust;
	};

	struct AAVRSVisualizationCB
	{
		float4 renderInfo;     // xy=render dim, zw=1/render dim
		float4 displayInfo;    // xy=display dim, z=eye count, w=coarseOutsideMask
		float4 maskInfo;       // x=center scale, y=protected outer scale, z=center horizontal scale
		float4 centerOffsets;  // xy=left eye, zw=right eye
		float4 coarseColor;
		float4 centerColor;
		float4 pad;            // xy=VRS tile size, z=performance mode, w=performance anisotropy
	};

	struct AAVRSRefinementCB
	{
		float4 renderInfo;     // xy=render dim, zw=1/render dim
		float4 rateInfo;       // xy=rate image dim, zw=VRS tile size
		float4 thresholds;     // x=luma range, y=bright luma, z=motion pixels, w=depth range
	};

	static_assert(sizeof(JitterCB) == 16, "JitterCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(UpscalingDataCB) == 64, "UpscalingDataCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(DynamicResolutionStretchCB) == 32, "DynamicResolutionStretchCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(VRMenuCompositeCB) == 16, "VRMenuCompositeCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(FoveatedPeripheryCB) == 96, "FoveatedPeripheryCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(FoveatedCenterBlendCB) == 64, "FoveatedCenterBlendCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(PeripheryTAACB) == 304, "PeripheryTAACB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(AAVRSVisualizationCB) == 112, "AAVRSVisualizationCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(AAVRSRefinementCB) == 48, "AAVRSRefinementCB layout changed; update HLSL cbuffer.");

	struct FoveatedDispatchRect
	{
		uint outputOffsetX = 0;
		uint outputOffsetY = 0;
		uint outputWidth = 0;
		uint outputHeight = 0;
		uint inputOffsetX = 0;
		uint inputOffsetY = 0;
		uint inputWidth = 0;
		uint inputHeight = 0;
	};

	struct PeripheryTAATile
	{
		uint32_t x = 0;
		uint32_t y = 0;
	};

	struct PeripheryTAATileCacheKey
	{
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t coveragePadding = 0;
		int32_t centerScaleQ = 0;
		int32_t taaOuterScaleQ = 0;
		int32_t centerHorizontalScaleQ = 0;
		int32_t centerOffsetXQ = 0;
		int32_t centerOffsetYQ = 0;
	};

	struct PeripheryTAATileCacheState
	{
		bool valid = false;
		bool uploaded = false;
		uint32_t tileCount = 0;
		PeripheryTAATileCacheKey key{};
		std::vector<PeripheryTAATile> tiles;
	};

	struct FoveatedRectCacheState
	{
		uint inputWidthPerEye = 0;
		uint inputHeight = 0;
		uint outputWidthPerEye = 0;
		uint outputHeight = 0;
		bool isVR = false;
		float centerScale = -1.0f;
		float centerFeather = -1.0f;
		float centerHorizontalScale = 1.0f;
		float peripheryTAAOuterScale = 0.0f;
		std::array<float2, 2> centerOffsets{};
		std::array<FoveatedDispatchRect, 2> rects{};
		FoveatedRegionPlan plan{};
	} foveatedRectCache;

	ConstantBuffer* jitterCB = nullptr;
	ConstantBuffer* upscalingDataCB = nullptr;
	ConstantBuffer* dynamicResolutionStretchCB = nullptr;
	ConstantBuffer* vrMenuCompositeCB = nullptr;
	ConstantBuffer* foveatedPeripheryCB = nullptr;
	ConstantBuffer* foveatedCenterBlendCB = nullptr;
	ConstantBuffer* peripheryTAACB = nullptr;
	ConstantBuffer* aaVrsVisualizationCB = nullptr;
	ConstantBuffer* aaVrsRefinementCB = nullptr;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;
	bool d3d12SwapChainActive = false;

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationDx12PathActive() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	bool IsFrameGenerationActive() const;
	float GetFrameGenerationFrameTime() const;
	bool IsUpscalingActive() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	void DrawFoveatedSetupInstructions();
	void DrawFoveatedSettings();
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	/**
	 * @brief Installs Direct3D-related hooks for device and factory creation.
	 *
	 * Loads FidelityFX support and patches the import address table (IAT) to redirect D3D11 device and DXGI factory creation functions to custom hook implementations.
	**/
	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;
	virtual void SetupRenderTargetResources() override;
	void InstallD3DDeviceDiagnostics(ID3D11Device* a_device);
	void InstallD3DContextDiagnostics(ID3D11DeviceContext* a_context);

	UpscaleMethod GetUpscaleMethod() const;
	UpscaleMethod GetConfiguredUpscaleMethodForTransition() const;
	UpscaleMethod GetLegacyDLSSPreferredUpscaleMethodForAPI() const;
	UpscaleMethod GetRuntimeUpscaleMethod() const;
	uint32_t GetRuntimeQualityMode() const;
	const RuntimeResolutionPlan& GetRuntimeResolutionPlan() const;
	// Refresh both the cached plan and restart-required state derived from the current VR render-scale settings.
	void RefreshRuntimeResolutionState();
	// Rebuild only the cached plan from already-latched state. Most callers want RefreshRuntimeResolutionState().
	void RefreshRuntimeResolutionPlan();
	bool IsRenderScaleModeRequested() const;
	bool GetVRRenderScaleModeRequested() const;
	bool CanUseVRRenderScaleMode() const;
	bool IsVRRenderScaleModeActive() const;
	VRRenderScaleStatus GetVRRenderScaleModeStatus() const;
	static const char* GetVRRenderScaleModeStatusName(VRRenderScaleStatus a_status);
	void SetVRRenderScaleModeRequested(bool a_enabled, const char* a_reason = nullptr, bool a_allowDefer = false, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	bool IsPerfModeActive() const;
	bool IsPerfModePresentationActive() const;
	bool IsPresentationUpscalingActive() const;
	bool GetPerfModeRequested() const;
	bool IsDeferredCompositePSRuntimeEnabled() const;
	bool IsDeferredCompositePSPending() const;
	bool IsAAVRSDeferredCompositeRuntimeEnabled() const;
	bool IsAAVRSDeferredCompositePending() const;
	bool IsDeferredCompositePSActive() const;
	bool ShouldUseAAVRSForDeferredComposite() const;
	void SetPerfModeRequested(bool a_enabled, const char* a_reason = nullptr, bool a_allowDefer = false, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void ApplyCSMenuUpscalingTransition(UpscaleMethod a_targetMethod, bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason = nullptr, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void SetVRUpscalingTransitionProfile(bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason = nullptr, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void RequestPerfModeRenderTargetRecreate(const char* a_reason = nullptr, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	bool ApplyPendingPerfModeRenderTargetRecreate(const char* a_caller = nullptr);
	void RecordTrueHMDRenderTargetSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight);
	bool TryGetPerfModeOpenVRRenderTargetSize(uint32_t& a_width, uint32_t& a_height, bool a_allowCreate = false);
	bool ConsumePerfModeBootLatchCreate();
	bool AdjustVRRenderScaleRenderTargetProperties(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties) const;
	enum class FoveatedUpscalingMode : uint8_t
	{
		Disabled,
		CenterOnly,
		PeripheralTAA
	};
	struct ActiveUpscalingFoveatedProfile
	{
		bool available = false;
		FoveatedUpscalingMode mode = FoveatedUpscalingMode::Disabled;
		bool usesPeripheryTAAOuterMask = false;
		// Actual DLSS/FSR foveated center dispatch scale.
		float vendorCenterScale = 1.0f;
		// Shared HMD-visible/protected boundary used by VR foveation consumers.
		float sharedVisibleScale = 1.0f;
		float centerHorizontalScale = 1.0f;
		std::array<float2, 2> centerOffsets{};
	};
	static const char* GetFoveatedUpscalingModeName(FoveatedUpscalingMode a_mode);
	ActiveUpscalingFoveatedProfile GetActiveUpscalingFoveatedProfile() const;
	float GetActiveFoveatedSharedVisibleScale() const;
	float GetActiveFoveatedCenterHorizontalScale() const;
	std::array<float2, 2> GetActiveResolvedFoveatedMaskCenterOffsets() const;

	void CheckResources(UpscaleMethod a_upscalemethod);
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);

	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[5];  // One for each UpscaleMethod
	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCSDepthOutput;  // FSR + VR: converts copied per-eye depth to R32_FLOAT for FidelityFX
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscaleRawDepthNoStencilPS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS(bool a_useRawSceneDepth = false);

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11PixelShader> vrMenuCompositePS;
	ID3D11PixelShader* GetVRMenuCompositePS();
	winrt::com_ptr<ID3D11PixelShader> vrMenuSceneDeltaCompositePS;
	ID3D11PixelShader* GetVRMenuSceneDeltaCompositePS();

	winrt::com_ptr<ID3D11ComputeShader> foveatedPeripheryCS;
	ID3D11ComputeShader* GetFoveatedPeripheryCS();

	winrt::com_ptr<ID3D11ComputeShader> foveatedCenterBlendCS;
	ID3D11ComputeShader* GetFoveatedCenterBlendCS();

	winrt::com_ptr<ID3D11ComputeShader> peripheryTAACS;
	ID3D11ComputeShader* GetPeripheryTAACS();

	winrt::com_ptr<ID3D11ComputeShader> aaVrsVisualizationCS;
	winrt::com_ptr<ID3D11ComputeShader> aaVrsRefinementCS;
	ID3D11ComputeShader* GetAAVRSVisualizationCS();
	ID3D11ComputeShader* GetAAVRSRefinementCS();

	winrt::com_ptr<ID3D11ComputeShader> submitStageStretchCS;
	ID3D11ComputeShader* GetSubmitStageStretchCS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11BlendState> vrMenuCompositeBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	// Shared VR HMD Mask Clearing
	winrt::com_ptr<ID3D11ComputeShader> vrClearHMDMaskCS;
	winrt::com_ptr<ID3D11Buffer> vrClearHMDMaskCB;
	// Helper to dispatch mask clearing for a single eye region
	void ClearHMDMask(ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
		uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight,
		uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY = 0, uint32_t colorOffsetY = 0,
		const char* phaseName = nullptr);

	// Shared VR Per-Eye Intermediate Buffers
	// Owned here so both Streamline (DLSS) and FidelityFX (FSR) can use them.
	eastl::unique_ptr<Texture2D> vrIntermediateColorIn[2];           // per-eye render resolution
	eastl::unique_ptr<Texture2D> vrIntermediateColorOut[2];          // per-eye output resolution
	eastl::unique_ptr<Texture2D> vrIntermediateDepth[2];             // per-eye render resolution (R24G8_TYPELESS, shared depth copy)
	eastl::unique_ptr<Texture2D> vrIntermediateLinearDepth[2];       // per-eye render resolution (R32_FLOAT, FSR input)
	eastl::unique_ptr<Texture2D> vrIntermediateMotionVectors[2];     // per-eye render resolution
	eastl::unique_ptr<Texture2D> vrIntermediateReactiveMask[2];      // per-eye render resolution
	eastl::unique_ptr<Texture2D> vrIntermediateTransparencyMask[2];  // per-eye render resolution
	eastl::unique_ptr<Texture2D> submitStageDLSSSharpenerTexture[2]; // per-eye output resolution
	struct RetiredVRIntermediateTextures
	{
		uint32_t retireFrame = 0;
		eastl::unique_ptr<Texture2D> colorIn[2];
		eastl::unique_ptr<Texture2D> colorOut[2];
		eastl::unique_ptr<Texture2D> depth[2];
		eastl::unique_ptr<Texture2D> linearDepth[2];
		eastl::unique_ptr<Texture2D> motionVectors[2];
		eastl::unique_ptr<Texture2D> reactiveMask[2];
		eastl::unique_ptr<Texture2D> transparencyMask[2];
		eastl::unique_ptr<Texture2D> submitStageDLSSSharpener[2];
	};
	std::vector<RetiredVRIntermediateTextures> retiredVRIntermediateTextures;

	struct VRIntermediateTextureCache
	{
		uint32_t inWidth = 0;
		uint32_t inHeight = 0;
		uint32_t outWidth = 0;
		uint32_t outHeight = 0;
		eastl::unique_ptr<Texture2D> colorIn[2];
		eastl::unique_ptr<Texture2D> colorOut[2];
		eastl::unique_ptr<Texture2D> depth[2];
		eastl::unique_ptr<Texture2D> linearDepth[2];
		eastl::unique_ptr<Texture2D> motionVectors[2];
		eastl::unique_ptr<Texture2D> reactiveMask[2];
		eastl::unique_ptr<Texture2D> transparencyMask[2];
	};
	VRIntermediateTextureCache cachedVRIntermediateTextures;

	// Helper to create/resize per-eye buffers matching source formats
	void CreateVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
		ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc);
	void EnsureVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
		ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc);
	bool EnsureVRPresentationTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
		ID3D11Resource* colorSrc);

	// Helper: Create a Texture2D matching source format at a given size
	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr, bool createRTV = false);

	// Shared Pipeline Steps
	bool PreparePerEyeInputs(ID3D11Resource* colorSrc, ID3D11Resource* depthSrc, ID3D11Resource* mvecSrc,
		ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc, bool copyAuxiliaryInputs = true, bool copyDepthInput = true);
	bool AreVRPerEyeUpscalingResourcesReady(bool requireDepth, bool requireLinearDepth) const;
	void FinalizePerEyeOutputs(ID3D11Resource* colorDst);
	bool EnsureSubmitStageDLSSSharpenerTexture(uint32_t eyeIndex, const Texture2D& colorOutput);
	bool ApplySubmitStageDLSSSharpening(uint32_t eyeIndex, const Texture2D& sharpenInput);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	bool IsAAVRSEligible(UpscaleMethod a_upscaleMethod) const;
	bool IsAAVRSAdapterEligible() const;
	bool BuildAAVRSSettings(AAVRSController::Settings& a_outSettings) const;
	void UpdateAAVRSState();
	bool ApplyAAVRSContentAwareRefinement(const AAVRSController::Settings& a_settings);
	void ApplyAAVRSVisualization();
	void DisableAAVRSState(const char* a_reason = "Disabled");
	void SuspendAAVRS();
	void ResumeAAVRS();
	bool ShouldForceFullRateForAAVRSPass(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest);
	bool ShouldForceFullRateForAAVRSPhase(AAVRSPassPolicyReason a_reason);
	bool GuardAAVRSRenderTarget();
	void BeginAAVRSFullRateOverride();
	void EndAAVRSFullRateOverride();
	void ReportAAVRSTelemetry(bool a_requested, bool a_preserveRuntimeActiveState = false);
	void ResetAAVRSTelemetry();
	void ResetAAVRSPassTelemetry();
	void DrawAAVRSPassTelemetry();

	class ScopedAAVRSFullRateOverride
	{
	public:
		ScopedAAVRSFullRateOverride(Upscaling& a_upscaling, bool a_active);
		~ScopedAAVRSFullRateOverride();

		ScopedAAVRSFullRateOverride(const ScopedAAVRSFullRateOverride&) = delete;
		ScopedAAVRSFullRateOverride& operator=(const ScopedAAVRSFullRateOverride&) = delete;

	private:
		Upscaling* upscaling = nullptr;
	};

	class ScopedAAVRSSuspension
	{
	public:
		ScopedAAVRSSuspension(Upscaling& a_upscaling, bool a_active);
		~ScopedAAVRSSuspension();

		ScopedAAVRSSuspension(const ScopedAAVRSSuspension&) = delete;
		ScopedAAVRSSuspension& operator=(const ScopedAAVRSSuspension&) = delete;

	private:
		Upscaling* upscaling = nullptr;
	};

	void ApplyDynamicResolutionState(RE::BSGraphics::State* a_state);
	void PrepareFullResolutionPostProcessing();
	bool ResetVRSubmitStageState(bool a_destroyDLSSResources = true);
	void RequestVRSubmitStageHistoryReset();
	bool IsSubmitStageUpscalingActive() const;
	bool IsSubmitStageDeviceLost() const;
	bool ShouldSuppressVRInSceneOverlaySubmit() const;
	bool IsVRNativeLayoutSubmitProtectedTexture(const vr::Texture_t* a_texture) const;
	void LogVRCompositorSubmitPath(vr::EVREye a_eye, const char* a_path, const vr::Texture_t* a_inputTexture,
		const vr::VRTextureBounds_t* a_inputBounds, const vr::Texture_t* a_outputTexture = nullptr,
		const vr::VRTextureBounds_t* a_outputBounds = nullptr, vr::EVRSubmitFlags a_submitFlags = vr::Submit_Default) const;
	bool SubmitVRUpscaledFrame(vr::EVREye a_eye, const vr::Texture_t* a_inputTexture, const vr::VRTextureBounds_t* a_inputBounds,
		vr::Texture_t& a_outputTexture, vr::VRTextureBounds_t& a_outputBounds);
	enum class DynamicResolutionUpsampleStage : uint8_t
	{
		Render,
		Dispatch
	};
	bool TryReplaceVanillaDynamicResolutionUpsample(const char* a_passName, DynamicResolutionUpsampleStage a_stage);
	void Upscale();
	void RequestPostLoadRuntimeReset();
	bool ApplyPendingPostLoadRuntimeReset(UpscaleMethod a_upscaleMethod);

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;
	Texture2D* motionVectorCopyTexture = nullptr;
	Texture2D* sharpenerTexture = nullptr;
	bool dlssUpscaleOutputInSharpenerTexture = false;
	eastl::unique_ptr<Texture2D> foveatedCenterColorIn[2];
	eastl::unique_ptr<Texture2D> foveatedCenterColorOut[2];
	eastl::unique_ptr<Texture2D> foveatedCenterDepth[2];
	eastl::unique_ptr<Texture2D> foveatedCenterMotionVectors[2];
	eastl::unique_ptr<Texture2D> foveatedCenterReactiveMask[2];
	eastl::unique_ptr<Texture2D> foveatedCenterTransparencyMask[2];
	eastl::unique_ptr<Texture2D> peripheryTAAHistoryColor[2][2];
	eastl::unique_ptr<Texture2D> peripheryTAAVelocityHistory[2][2];
	eastl::unique_ptr<Texture2D> peripheryTAALockHistory[2][2];
	eastl::unique_ptr<Buffer> peripheryTAATileBuffer[2];
	uint32_t peripheryTAATileCapacity[2] = {};
	std::array<PeripheryTAATileCacheState, 2> peripheryTAATileCache{};
	uint32_t peripheryTAAHistoryReadIndex = 0;
	bool peripheryTAAHistoryValid = false;
	AAVRSController aaVrsController;
	bool deferredCompositePSRuntimeEnabled = false;
	bool aaVrsDeferredCompositeRuntimeEnabled = false;
	bool aaVrsRuntimeActive = false;
	bool aaVrsRuntimeContentAware = false;
	bool aaVrsTelemetryLoggedActive = false;
	uint32_t aaVrsTelemetryMaskWidth = 0;
	uint32_t aaVrsTelemetryMaskHeight = 0;
	uint32_t aaVrsTelemetryRenderWidth = 0;
	uint32_t aaVrsTelemetryRenderHeight = 0;
	uint32_t aaVrsTelemetryMaxRate = 0;
	bool aaVrsTelemetryPerformanceMode = false;
	uint32_t aaVrsTelemetryPerformanceAnisotropy = 0;
	bool aaVrsTelemetryContentAware = false;
	std::string aaVrsTelemetryInactiveReason;
	std::array<std::atomic<uint32_t>, kAAVRSPassPolicyReasonCount> aaVrsPassPolicyCounters{};

	virtual void ClearShaderCache() override;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline FidelityFX fidelityFX;  ///< AMD FidelityFX runtime for FSR upscaling and frame generation
	static inline DX12SwapChain dx12SwapChain;
	static inline RCAS rcas;  ///< Standalone RCAS sharpening for DLSS

	winrt::com_ptr<ID3D11PixelShader> copyDepthToSharedBufferPS;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousVendorUpscalerSelected = false;
	bool d3dDeviceDiagnosticsInstalled = false;
	bool d3dContextDiagnosticsInstalled = false;
	bool depthUpscaleUseWideKernel = false;
	bool historyResetRequested = true;
	bool historyResetThisFrame = false;
	uint32_t historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	bool historyResetTrackingInitialized = false;
	float2 previousHistoryScreenSize = { 0.0f, 0.0f };
	float2 previousHistoryResolutionScale = { 1.0f, 1.0f };
	float2 previousHistoryEngineRenderSize = { 0.0f, 0.0f };
	float2 previousHistoryFinalOutputSize = { 0.0f, 0.0f };
	ResolutionOwner previousHistoryResolutionOwner = ResolutionOwner::Native;
	uint32_t previousHistoryQualityMode = std::numeric_limits<uint32_t>::max();
	bool previousHistoryInWorld = false;
	bool previousHistoryInMapMenu = false;
	UpscaleMethod previousHistoryUpscaleMethod = UpscaleMethod::kNONE;
	bool previousHistoryFoveatedDispatch = false;
	float previousHistoryFoveatedCenterScale = 1.0f;
	float previousHistoryFoveatedCenterHorizontalScale = 1.0f;
	std::array<float2, 2> previousHistoryFoveatedCenterOffsets = {};
	bool previousHistoryPeripheryTAA = false;
	bool previousHistoryPeripheryTAAPathActive = false;
	float previousHistoryPeripheryTAAOuterScale = 0.70f;
	float previousHistoryPeripheryTAACenterBlendFeather = 0.05f;
	bool previousHistoryFSRRuntimePathActive = false;
	bool previousHistoryFSRRuntimeFsr4Active = false;
	std::atomic<bool> postLoadRuntimeResetPending{ false };
	std::atomic<bool> pendingDLSSHistoryReset{ false };
	std::atomic<uint32_t> pendingVRUpscalingQualityMode{ kPendingVRUpscalingSettingUnset };
	std::atomic<uint32_t> pendingVRRenderScaleMode{ kPendingVRUpscalingSettingUnset };
	std::atomic<uint32_t> pendingVRDLSSPreset{ kPendingVRUpscalingSettingUnset };
	std::atomic<uint32_t> pendingVRPerfMode{ kPendingVRUpscalingSettingUnset };
	std::atomic<uint32_t> pendingVRUpscalingTransitionFrame{ 0 };
	std::atomic<uint32_t> pendingVRUpscalingTransitionOrigin{ static_cast<uint32_t>(VRUpscalingTransitionOrigin::CSMenu) };
	std::atomic<uint32_t> pendingVRFpsStabilizerSyncFrame{ 0 };
	std::atomic<uint32_t> pendingVRFpsStabilizerSyncLastWaitLogFrame{ 0 };
	std::atomic<bool> delayedVRPerfModeBootLatchForDLSS{ false };
	std::atomic<bool> pendingDLSSReset{ false };
	std::atomic<bool> pendingFSRReset{ false };
	std::atomic<bool> pendingPerfModeRenderTargetRecreate{ false };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateFrame{ 0 };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateDelayFrames{ 0 };
	std::atomic<bool> pendingPerfModeRenderTargetRecreatePostLoadSettle{ false };
	std::atomic<bool> perfModeRenderTargetRecreateInProgress{ false };
	std::atomic<bool> perfModeAllowBootLatchCreate{ true };
	std::atomic<bool> vrDLSSSettingsRelatched{ false };
	mutable std::atomic_bool submitStageDeviceLost{ false };
	uint32_t submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	bool submitStagePreparedFramePresentationOnly = false;
	bool submitStagePreparedFrameCleanMenuScene = false;
	uint32_t submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	std::array<bool, 2> submitStageMirrorEyeReady = {};
	ID3D11Texture2D* submitStageMirrorSourceTexture = nullptr;
	eastl::unique_ptr<Texture2D> vrMenuCleanSceneAtMainPost;
	eastl::unique_ptr<Texture2D> vrMenuBakedSceneAtSubmit;
	std::array<eastl::unique_ptr<Texture2D>, 2> vrMenuSceneDeltaFinalSceneCopy;
	uint32_t vrMenuSceneDeltaFrame = std::numeric_limits<uint32_t>::max();
	uint32_t vrMenuSceneDeltaWidth = 0;
	uint32_t vrMenuSceneDeltaHeight = 0;
	DXGI_FORMAT vrMenuSceneDeltaFormat = DXGI_FORMAT_UNKNOWN;
	ID3D11Texture2D* vrMenuSceneDeltaSourceTexture = nullptr;
	bool vrMenuSceneDeltaCleanReady = false;
	bool vrMenuSceneDeltaBakedReady = false;
	eastl::unique_ptr<Texture2D> vrMenuHiResCleanScene;
	eastl::unique_ptr<Texture2D> vrMenuHiResLayer;
	uint32_t vrMenuHiResFrame = std::numeric_limits<uint32_t>::max();
	uint32_t vrMenuHiResCleanWidth = 0;
	uint32_t vrMenuHiResCleanHeight = 0;
	uint32_t vrMenuHiResLayerWidth = 0;
	uint32_t vrMenuHiResLayerHeight = 0;
	DXGI_FORMAT vrMenuHiResCleanFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT vrMenuHiResLayerFormat = DXGI_FORMAT_UNKNOWN;
	ID3D11Texture2D* vrMenuHiResCleanSourceTexture = nullptr;
	bool vrMenuHiResCleanReady = false;
	bool vrMenuHiResLayerCleared = false;
	bool vrMenuHiResLayerReady = false;
	bool vrMenuHiResUsingCleanSceneThisFrame = false;
	uint32_t submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	std::array<bool, 2> submitStageFoveatedPeripheryTAAEyeReady = {};
	mutable std::atomic_bool submitStageRuntimeActive{ false };
	std::atomic<uint32_t> submitStageVendorResumeFrame{ 0 };
	std::atomic<uint32_t> submitStageVendorResumeStartFrame{ 0 };
	std::atomic<uint32_t> submitStageVendorResumeStableFrames{ 0 };
	std::atomic<uint32_t> submitStageVendorResumeLastStableFrame{ 0 };
	std::atomic_bool vrRenderScaleResourceTrackingSyncPending{ false };

	void CopySharedD3D12Resources();
	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();
	void RefreshSubmitStageUnderwaterMask();
	void RequestHistoryReset();
	uint32_t GetEffectiveUpscalingQualityMode() const;
	uint32_t GetEffectiveDLSSQualityMode() const;
	uint32_t GetEffectiveDLSSPreset() const;
	void QueueVRUpscalingQualityMode(uint32_t a_qualityMode, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void QueueVRRenderScaleModeRequest(bool a_enabled, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void QueueVRDLSSPreset(uint32_t a_dlssPreset, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void QueueVRPerfModeRequest(bool a_enabled, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void MarkVRUpscalingTransitionQueued(VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void ClearPendingVRUpscalingTransition();
	bool HasPendingVRUpscalingTransition() const;
	bool HasPendingVRRenderScaleTransition() const;
	void QueueVRFpsStabilizerLoadSync(uint32_t a_frame);
	void ApplyPendingVRFpsStabilizerLoadSync();
	bool ShouldStageVRRenderScaleTransition(bool a_renderScaleModeEnabled, uint32_t a_qualityMode) const;
	bool ShouldDeferVRUpscalingTransitionSettings() const;
	bool ShouldWaitForVRUpscalingTransitionDelay() const;
	void MarkPerfModeRenderTargetRecreateQueued(uint32_t a_delayFrames = 0);
	bool ShouldWaitForPerfModeRenderTargetRecreateDelay() const;
	void ApplyPendingVRUpscalingTransition(UpscaleMethod a_upscaleMethod);
	bool ShouldResetHistoryThisFrame() const;
	void UpdateHistoryResetState(UpscaleMethod a_upscaleMethod);
	void LatchHistoryResetForCurrentFrame();
	bool IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const;
	bool IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const;
	bool IsFoveatedVendorDispatchEnabled(UpscaleMethod a_upscaleMethod) const;
	bool IsPeripheryTAAEnabled(UpscaleMethod a_upscaleMethod) const;
	bool IsPeripheryTAAPathActive(UpscaleMethod a_upscaleMethod) const;
	float2 GetDefaultFoveatedMaskCenterOffset(uint32_t eyeIndex) const;
	float2 GetResolvedFoveatedMaskCenterOffset(uint32_t eyeIndex, bool usePeripheryTAAProfile = false) const;
	std::array<float2, 2> GetResolvedFoveatedMaskCenterOffsets(bool usePeripheryTAAProfile = false) const;
	bool GetRuntimeFoveatedRegionDimensions(uint32_t& a_inputWidthPerEye, uint32_t& a_inputHeight, uint32_t& a_outputWidthPerEye, uint32_t& a_outputHeight) const;
	bool BuildFoveatedDispatchRects(uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool isVR, float centerScale, float centerFeather, float centerHorizontalScale, bool usePeripheryTAAProfile = false);
	bool EncodeSubmitStageVRInputs(ID3D11Resource* colorSource, ID3D11Resource* motionVectors, ID3D11Resource* depthSource, uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight);
	bool StretchSubmitStageEyeOutput(uint32_t eyeIndex, uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight);
	bool CompositeKnownGameMenuAfterSubmitStageUpscale(uint32_t eyeIndex, uint32_t eyeWidthOut, uint32_t eyeHeightOut);
	void CaptureVRMenuCleanSceneAtMainPostProcessingEntry();
	bool ResolveVRMenuCleanSceneForSubmit(uint32_t frame, ID3D11Texture2D* sourceTexture, ID3D11Texture2D*& cleanSceneTexture);
	bool CompositeVRMenuSceneDeltaAfterSubmitStageUpscale(uint32_t eyeIndex, uint32_t eyeWidthIn, uint32_t eyeHeightIn, uint32_t eyeWidthOut, uint32_t eyeHeightOut);
	void ResetVRMenuSceneDeltaState();
	void CaptureVRMenuHiResCleanSceneAtMainPostProcessingEntry();
	bool TryDuplicateVRMenuHiResBgDraw(ID3D11DeviceContext* context, const char* hook, const std::string& args, const std::function<void()>& drawFunc);
	bool ResolveVRMenuHiResCleanSceneForSubmit(uint32_t frame, ID3D11Texture2D* sourceTexture, ID3D11Texture2D*& cleanSceneTexture);
	bool CompositeVRMenuHiResBgAfterSubmitStageUpscale(uint32_t eyeIndex, uint32_t eyeWidthOut, uint32_t eyeHeightOut);
	void ResetVRMenuHiResBgFrame(uint32_t frame);
	void ResetVRMenuHiResBgState();
	bool EnsureFoveatedTexture(eastl::unique_ptr<Texture2D>& texture, ID3D11Resource* source, uint32_t width, uint32_t height, bool copyBindFlags, bool createSRV, bool createUAV, bool createRTV, const char* name);
	void DestroySubmitStageDLSSSharpenerTextures();
	void DestroyCommonUpscalingTextures();
	void DestroyVRIntermediateTextures();
	void UnbindUpscalingResources();
	void DestroyFoveatedResources();
	bool EnsurePeripheryTAAResources(uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* colorSource);
	bool EnsurePeripheryTAATileBuffer(uint32_t eyeIndex, uint32_t tileCapacity);
	bool BuildPeripheryTAATileList(uint32_t eyeIndex, uint32_t outputWidth, uint32_t outputHeight, float centerScale, float taaOuterScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY, uint32_t coveragePadding, uint32_t& outTileCount);
	void DestroyPeripheryTAAResources();
	bool DispatchFoveatedVendorUpscaling(UpscaleMethod a_upscaleMethod, ID3D11Resource* colorTexture, ID3D11Resource* depthTexture, ID3D11Resource* motionVectors, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask, ID3D11Resource* colorOutput = nullptr);
	bool DispatchSubmitStageFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* outputResource = nullptr, ID3D11UnorderedAccessView* outputUAV = nullptr);
	struct FoveatedEyeDispatchParams
	{
		uint32_t inputWidthPerEye = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidthPerEye = 0;
		uint32_t outputHeight = 0;
		float centerScale = 1.0f;
		float centerHorizontalScale = 1.0f;
		float centerBlendFeather = 0.0f;
		bool usePeripheryTAA = false;
		bool usePeripheryTAAProfile = false;
		bool visualizeMask = false;
		bool resetPeripheryTAA = false;
		uint32_t peripheryTAAHistoryReadIndex = 0;
		uint32_t peripheryTAAHistoryWriteIndex = 0;
		ID3D11ShaderResourceView* peripherySourceSRV = nullptr;
		uint32_t peripherySourceWidth = 0;
		uint32_t peripherySourceHeight = 0;
		float peripherySourceScaleX = 1.0f;
		float peripherySourceScaleY = 1.0f;
		float peripherySourceOffsetX = 0.0f;
		float peripherySourceOffsetY = 0.0f;
		ID3D11Resource* centerColorInput = nullptr;
		ID3D11Resource* centerDepthInput = nullptr;
		ID3D11Resource* centerMotionVectorsInput = nullptr;
		ID3D11Resource* centerReactiveMaskInput = nullptr;
		ID3D11Resource* centerTransparencyMaskInput = nullptr;
		ID3D11UnorderedAccessView* outputUAV = nullptr;
		uint32_t centerColorInputBaseOffsetX = 0;
		uint32_t centerDepthInputBaseOffsetX = 0;
		uint32_t centerAuxInputBaseOffsetX = 0;
	};
	void ConfigureFoveatedPeripherySourceRegion(FoveatedEyeDispatchParams& params, const eastl::unique_ptr<Texture2D>& sourceTexture, uint32_t validWidth, uint32_t validHeight) const;
	bool DispatchFoveatedVendorEyeComposite(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, const FoveatedEyeDispatchParams& params);
	bool DispatchSingleFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* depthIn, ID3D11Resource* motionVectorsIn, ID3D11Resource* reactiveMaskIn, ID3D11Resource* transparencyMaskIn, uint32_t outputWidthPerEye, uint32_t outputHeight, uint32_t inputWidthPerEye, uint32_t inputHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather, uint32_t colorInputBaseOffsetX = 0, uint32_t depthInputBaseOffsetX = 0, uint32_t auxInputBaseOffsetX = 0, ID3D11UnorderedAccessView* outputUAV = nullptr);
	void DispatchFoveatedPeripheryPass(ID3D11ShaderResourceView* sourceSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t outputWidth, uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, bool keepBindingsBound = false, float sourceScaleX = 1.0f, float sourceScaleY = 1.0f, float sourceOffsetX = 0.0f, float sourceOffsetY = 0.0f, float centerOffsetX = 0.0f, float centerOffsetY = 0.0f);
	void DispatchPeripheryTAAPass(ID3D11ShaderResourceView* currentColorSRV, ID3D11ShaderResourceView* currentDepthSRV, ID3D11ShaderResourceView* currentMotionVectorSRV,
		ID3D11ShaderResourceView* currentReactiveSRV, ID3D11ShaderResourceView* currentTransparencySRV, ID3D11ShaderResourceView* historyColorSRV,
		ID3D11ShaderResourceView* historyVelocitySRV, ID3D11ShaderResourceView* historyLockSRV, ID3D11UnorderedAccessView* outputColorUAV, ID3D11UnorderedAccessView* outputHistoryColorUAV,
		ID3D11UnorderedAccessView* outputVelocityUAV, ID3D11UnorderedAccessView* outputLockUAV, ID3D11ShaderResourceView* tileListSRV, uint32_t tileCount,
		uint32_t inputWidth, uint32_t inputHeight,
		uint32_t outputWidth, uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight,
		const float4x4& currentViewProjInverse, const float4x4& previousViewProj, const float4& currentCameraPosAdjust, const float4& previousCameraPosAdjust,
		bool resetHistory, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY,
		float inputTextureScaleX = 1.0f, float inputTextureScaleY = 1.0f, float inputTextureOffsetX = 0.0f, float inputTextureOffsetY = 0.0f);
	void DispatchFoveatedBlendPass(ID3D11ShaderResourceView* centerSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t outputWidthPerEye, uint32_t outputHeight, const FoveatedDispatchRect& rect, uint32_t dispatchOffsetX, uint32_t dispatchOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather);

	/**
	 * @brief Applies RCAS sharpening to the main render target after DLSS upscaling.
	 *
	 * Runs in HDR space before tonemapping. Only called when DLSS is active and sharpness > 0.
	 */
	void ApplySharpening();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs();  // Loads all SDKs at once
	void SetUIBuffer();
	HANDLE GetFrameLatencyWaitableObject() const;
	float GetFrameTime() const;

	// Backend interface methods
	bool IsBackendInitialized() const;
	void CheckBackendFeatures(IDXGIAdapter* adapter);
	void UpgradeBackendInterface(void** ppInterface);
	void SetBackendD3DDevice(ID3D11Device* device);
	void PostBackendDevice();

	// Module availability methods
	bool HasFrameGenModule() const;

	// Proxy interface methods
	void SetProxyD3D11Device(ID3D11Device* device);
	void SetProxyD3D11DeviceContext(ID3D11DeviceContext* context);
	void CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	void CreateProxyInterop();
	IDXGISwapChain* GetProxySwapChain();
	bool IsOpenCompositeUpscalingBlocked(bool a_forceRefresh = false) const;

private:
	bool IsDeferredCompositePSRequested() const;
	bool IsAAVRSDeferredCompositeRequested() const;
	void ApplyDeferredCompositeVRSRuntimeSettings(const char* a_reason = nullptr);
	void ClearSubmitStageVendorResumeCooldown();
	void MarkSubmitStageDeviceLost(HRESULT a_result, const char* a_context);
	bool MarkSubmitStageDeviceLostIfNeeded(const std::exception& a_exception, const char* a_context);
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char* a_context);
	bool ResetVRVendorRuntimeResources(bool a_destroyDLSSResources, bool a_destroyPeripheryTAAResources);
	void RecreateVendorRuntimeResources(UpscaleMethod a_upscaleMethod, bool a_recreateTemporalResources);
	bool AreCommonVendorTexturesReady(UpscaleMethod a_upscaleMethod) const;
	bool ApplyPendingVendorRuntimeReset(UpscaleMethod a_upscaleMethod, const char* a_context);
	void UpdateDepthUpscaleKernelState(JitterCB& a_jitterData, bool a_enableWideKernelLogic);
	enum class HMDMaskClearPhase : uint8_t
	{
		PerEyeInput,
		PerEyeOutput,
		SubmitStageOutput,
		SubmitStageFoveatedOutput
	};
	bool ShouldClearHMDMaskInPhase(HMDMaskClearPhase a_phase) const;
	void ClearHMDMaskForEye(HMDMaskClearPhase a_phase, ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
		uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight,
		uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY = 0, uint32_t colorOffsetY = 0);
	struct VendorEyeDispatchParams
	{
		uint32_t eyeIndex = 0;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
		float pinholeOffsetX = 0.0f;
		float pinholeOffsetY = 0.0f;
		ID3D11Resource* colorIn = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* reactiveMask = nullptr;
		ID3D11Resource* transparencyMask = nullptr;
		ID3D11Resource* colorOut = nullptr;
		const char* label = "vendor eye dispatch";
	};
	bool DispatchVendorEyeRegion(UpscaleMethod a_upscaleMethod, const VendorEyeDispatchParams& params);
	bool EnsureHMDMaskClearResources();
	bool EnsureFoveatedDispatchShaders(bool usePeripheryTAA, bool visualizeMask, const char* context, const char* fallbackAction);

	struct OpenCompositeUpscalingBlocker
	{
		bool active = false;
		std::string settingName;
		std::string configPath;
	};

	const OpenCompositeUpscalingBlocker& GetOpenCompositeUpscalingBlocker(bool a_forceRefresh = false) const;
	void ApplyOpenCompositeUpscalingBlocker(bool a_forceRefresh = false);

	mutable OpenCompositeUpscalingBlocker openCompositeUpscalingBlocker;
	mutable bool openCompositeUpscalingBlockerCacheValid = false;
	mutable ULONGLONG openCompositeUpscalingBlockerLastRefresh = 0;
	bool openCompositeUpscalingBackendSkipLogged = false;
	bool renderDocUpscalingBackendSkipLogged = false;
	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11Device_CreateTexture2D
	{
		static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* a_device, const D3D11_TEXTURE2D_DESC* a_desc, const D3D11_SUBRESOURCE_DATA* a_initialData, ID3D11Texture2D** a_texture);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11Device_CreateShaderResourceView
	{
		static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* a_device, ID3D11Resource* a_resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* a_desc, ID3D11ShaderResourceView** a_srv);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11Device_CreateRenderTargetView
	{
		static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* a_device, ID3D11Resource* a_resource, const D3D11_RENDER_TARGET_VIEW_DESC* a_desc, ID3D11RenderTargetView** a_rtv);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Draw
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCount, UINT a_startVertexLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Map
	{
		static HRESULT thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_resource, UINT a_subresource, D3D11_MAP a_mapType, UINT a_mapFlags, D3D11_MAPPED_SUBRESOURCE* a_mappedResource);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstanced
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance, UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawAuto
	{
		static void thunk(ID3D11DeviceContext* a_context);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Dispatch
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_threadGroupCountX, UINT a_threadGroupCountY, UINT a_threadGroupCountZ);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DispatchIndirect
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_OMSetRenderTargets
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_numViews, ID3D11RenderTargetView* const* a_renderTargetViews, ID3D11DepthStencilView* a_depthStencilView);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_numRTVs, ID3D11RenderTargetView* const* a_renderTargetViews, ID3D11DepthStencilView* a_depthStencilView, UINT a_uavStartSlot, UINT a_numUAVs, ID3D11UnorderedAccessView* const* a_unorderedAccessViews, const UINT* a_uavInitialCounts);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_CopySubresourceRegion
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_dstResource, UINT a_dstSubresource, UINT a_dstX, UINT a_dstY, UINT a_dstZ, ID3D11Resource* a_srcResource, UINT a_srcSubresource, const D3D11_BOX* a_srcBox);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_CopyResource
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_dstResource, ID3D11Resource* a_srcResource);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_UpdateSubresource
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_dstResource, UINT a_dstSubresource, const D3D11_BOX* a_dstBox, const void* a_srcData, UINT a_srcRowPitch, UINT a_srcDepthPitch);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_ResolveSubresource
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_dstResource, UINT a_dstSubresource, ID3D11Resource* a_srcResource, UINT a_srcSubresource, DXGI_FORMAT a_format);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_ClearRenderTargetView
	{
		static void thunk(ID3D11DeviceContext* a_context, ID3D11RenderTargetView* a_renderTargetView, const FLOAT a_colorRGBA[4]);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct UpsampleDynamicResolution_Render
	{
		static void thunk(void* a_imageSpaceShader, void* a_shape, void* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct FullScreenVR_Render
	{
		static void thunk(void* a_imageSpaceShader, void* a_shape, void* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CopyDynamicFetchDisabled_Render
	{
		static void thunk(void* a_imageSpaceShader, void* a_shape, void* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct HDRTonemapBlendCinematicFade_Render
	{
		static void thunk(void* a_imageSpaceShader, void* a_shape, void* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TemporalAAUI_Render
	{
		static void thunk(void* a_imageSpaceShader, void* a_shape, void* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct LightingCompositeMenu_Render
	{
		static void thunk(void* a_imageSpaceShader, void* a_shape, void* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct UpsampleDynamicResolution_Dispatch
	{
		static void thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct FullScreenVR_Dispatch
	{
		static void thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CopyDynamicFetchDisabled_Dispatch
	{
		static void thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct HDRTonemapBlendCinematicFade_Dispatch
	{
		static void thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TemporalAAUI_Dispatch
	{
		static void thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct LightingCompositeMenu_Dispatch
	{
		static void thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetScissorRect
	{
		static void thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPrecipitation
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFaceGenManager_UpdatePendingCustomizationTextures
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		static bool Register();
	};
};
