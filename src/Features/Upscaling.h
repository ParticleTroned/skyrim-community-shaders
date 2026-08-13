#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/RCAS/RCAS.h"
#include "Upscaling/Streamline.h"
#include <atomic>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <limits>
#include <memory>
#include <winrt/base.h>

/**
 * @brief Provides upscaling functionality including DLSS, FSR and TAA.
 *
 * This feature handles various upscaling methods and frame generation technologies
 * to improve performance while maintaining visual quality.
 */
struct Upscaling : Feature
{
private:
	static constexpr std::string_view MOD_ID = "156952";

public:
	// Feature interface
	virtual inline std::string GetName() override { return "Upscaling"; }
	virtual std::string GetDisplayName() override { return T("feature.upscaling.name", "Upscaling"); }
	virtual inline std::string GetShortName() override { return "Upscaling"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline bool IsCore() const override { return true; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.upscaling.description", "Advanced upscaling and frame generation technologies for improved performance"),
			{ T("feature.upscaling.key_feature_1", "DLSS (Deep Learning Super Sampling) support"),
				T("feature.upscaling.key_feature_2", "FSR (FidelityFX Super Resolution) support"),
				T("feature.upscaling.key_feature_3", "TAA (Temporal Anti-Aliasing) support"),
				T("feature.upscaling.key_feature_4", "Frame generation for supported systems") } };
	};

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};

	// Shared DLSS/FSR/FSR4 render-scale presets:
	// 0=Native AA/DLAA, 1=Hoshipa, 2=Ultra Quality, 3=Quality,
	// 4=Balanced, 5=Performance, 6=Ultra Performance
	static constexpr uint32_t kQualityModeMaxIndex = 6;
	static constexpr uint32_t kQualityModeSchemaVersion = 2;
	static constexpr uint32_t kDLSSPresetJ = 0;
	static constexpr uint32_t kDLSSPresetK = 1;
	static constexpr uint32_t kDLSSPresetL = 2;
	static constexpr uint32_t kDLSSPresetM = 3;
	static constexpr uint32_t kDLSSPresetF = 4;
	static constexpr uint32_t kDLSSPresetE = 5;
	static constexpr uint32_t kDLSSPresetMaxIndex = kDLSSPresetE;
	static constexpr uint32_t kFsr4RuntimeSelectionSchemaVersion = 1;
	static constexpr float kDefaultDLSSSharpness = 0.5f;

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
		uint qualityMode = 3;            // Shared upscaler preset; defaults to Quality
		uint dlssPreset = kDLSSPresetK;  // Settings ids: J, K, L, M, F, E (default K)
		uint frameLimitMode = 1;
		uint frameGenerationMode = 0;  // Disabled by default
		uint frameGenerationForceEnable = 0;
		bool frameGenerationAllowInMenus = false;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
		float sharpnessFSR = 0.0f;
		float sharpnessDLSS = kDefaultDLSSSharpness;
		bool fsr4RuntimeEnable = true;
		uint fsr4RuntimeSelectionSchemaVersion = kFsr4RuntimeSelectionSchemaVersion;
		bool reflexLowLatencyMode = true;
		bool reflexLowLatencyBoost = false;
		bool reflexUseMarkersToOptimize = true;
		bool reflexUseFPSLimit = false;
		float reflexFPSLimit = 60.0f;
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(JitterCB);

	struct UpscalingDataCB
	{
		float2 trueSamplingDim;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(UpscalingDataCB);

	struct CameraMotionVectorsCB
	{
		float4x4 curViewProjUnjitteredInverse;
		float4x4 prevViewProjUnjittered;
	};
	STATIC_ASSERT_ALIGNAS_16(CameraMotionVectorsCB);
	static_assert(sizeof(CameraMotionVectorsCB) == 128, "CameraMotionVectorsCB layout changed; update HLSL cbuffer.");

	std::unique_ptr<ConstantBuffer> jitterCB;
	std::unique_ptr<ConstantBuffer> upscalingDataCB;
	std::unique_ptr<ConstantBuffer> cameraMotionVectorsCB;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;
	bool fidelityFXMissing = false;
	bool d3d12SwapChainActive = false;

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationDx12PathActive() const;
	bool IsFrameGenerationActive() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	bool ConsumeFrameGenerationInputsForPresent();
	float GetFrameGenerationFrameTime() const;
	bool IsUpscalingActive() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool) override;
	virtual PerformanceTuningConfig GetPerformanceTuningConfig() const override
	{
		return { 0,
			T("menu.performance_tuning.feature.upscaling.comparison_label", "None / Frame Generation Off"),
			T("menu.performance_tuning.feature.upscaling.comparison_details", "Upscaling is set to None and Frame Generation is switched off.") };
	}
	virtual json GetPerformanceTuningUserSettingsMask() const override
	{
		return {
			{ "upscaleMethod", true },
			{ "upscaleMethodNoDLSS", true },
			{ "qualityMode", true },
			{ "qualityModeSchemaVersion", true },
			{ "dlssPreset", true },
			{ "frameLimitMode", true },
			{ "frameGenerationMode", true },
			{ "frameGenerationForceEnable", true },
			{ "frameGenerationAllowInMenus", true },
			{ "sharpnessFSR", true },
			{ "sharpnessDLSS", true },
			{ "fsr4RuntimeEnable", true },
			{ "fsr4RuntimeSelectionSchemaVersion", true },
			{ "reflexLowLatencyMode", true },
			{ "reflexLowLatencyBoost", true },
			{ "reflexUseMarkersToOptimize", true },
			{ "reflexUseFPSLimit", true },
			{ "reflexFPSLimit", true }
		};
	}
	virtual bool NormalizePerformanceTuningUserSettings(json& a_settings) const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override
	{
		const auto configuredMethod =
			streamline.featureDLSS ?
				static_cast<UpscaleMethod>(settings.upscaleMethod) :
				static_cast<UpscaleMethod>(settings.upscaleMethodNoDLSS);
		return configuredMethod != UpscaleMethod::kNONE ||
		       (d3d12SwapChainActive && settings.frameGenerationMode != 0);
	}
	virtual bool IsPerformanceCostMeasurementReady() const override;
	virtual const char* GetPerformanceCostMeasurementWaitText() const override
	{
		return T(
			"menu.performance_tuning.feature.upscaling.wait",
			"Waiting for upscaling and frame pacing to settle");
	}
	virtual uint64_t GetPerformanceCostMeasurementFreshPresentCount(bool) const override
	{
		// Flush the same 60-frame rolling window shown by Performance Tuning.
		return 60;
	}
	virtual double GetPerformanceCostMeasurementPostFreshSoakSeconds(bool) const override
	{
		// Match the time for which the post-edit timing comparison remains visible.
		return 4.0;
	}
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override
	{
		if (a_enabled) {
			settings = Settings{};
			return;
		}

		settings.upscaleMethod = static_cast<uint>(UpscaleMethod::kNONE);
		settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kNONE);
		settings.frameGenerationMode = 0;
	}
	virtual json CapturePerformanceCostMeasurementState() const override { return CapturePerformanceSettingsState(); }
	virtual json CapturePerformanceSettingsState() const override;
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override
	{
		auto state = a_state;
		LoadSettings(state);
	}
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

	UpscaleMethod GetUpscaleMethod() const;

	bool CheckResources(UpscaleMethod a_upscalemethod);
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyAllUpscalingTextureResources();

	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[4];          // One for each UpscaleMethod
	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCSDepthOutput;  // Runtime FSR: converts game depth to typed R32_FLOAT
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> cameraMotionVectorsPS;
	ID3D11PixelShader* GetCameraMotionVectorsPS();
	void FillMenuCameraMotionVectors();
	void PrepareMenuCameraMotionVectors();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	bool Upscale();

	// D3D11 textures
	std::unique_ptr<Texture2D> reactiveMaskTexture;
	std::unique_ptr<Texture2D> transparencyCompositionMaskTexture;
	std::unique_ptr<Texture2D> motionVectorCopyTexture;
	std::unique_ptr<Texture2D> runtimeFsrDepthTexture;
	std::unique_ptr<Texture2D> sharpenerTexture;

	virtual void ClearShaderCache() override;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline FidelityFX fidelityFX;  ///< Only for frame generation
	static inline DX12SwapChain dx12SwapChain;
	static inline RCAS rcas;  ///< Standalone RCAS sharpening for DLSS

	winrt::com_ptr<ID3D11PixelShader> copyDepthToSharedBufferPS;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousVendorUpscalerSelected = false;
	// FidelityFX teardown/recreation can span frames while GPU ownership drains.
	// Keep the transition pending until CheckResources observes Ready and commits
	// its previous/applied configuration snapshots.
	bool fsrResourceTransitionPending = false;
	bool upscalingResourcesReady = false;
	bool depthUpscaleUseWideKernel = false;
	bool dlssSharpenerOutputValid = false;
	bool historyResetRequested = true;
	bool historyResetThisFrame = false;
	bool menuCameraMVsValid = false;
	uint32_t historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t menuCameraMVsPreparedFrame = std::numeric_limits<uint32_t>::max();
	bool historyResetTrackingInitialized = false;
	float2 previousHistoryScreenSize = { 0.0f, 0.0f };
	float2 previousHistoryResolutionScale = { 1.0f, 1.0f };
	uint32_t previousHistoryQualityMode = std::numeric_limits<uint32_t>::max();
	bool previousHistoryInWorld = false;
	bool previousHistoryInMapMenu = false;
	UpscaleMethod previousHistoryUpscaleMethod = UpscaleMethod::kNONE;
	bool previousHistoryFSRRuntimePathActive = false;
	bool previousHistoryFSRRuntimeFsr4Active = false;

	// Last configuration observed by CheckResources. Performance cost sampling
	// must not begin while menu settings are still pending on the render path.
	bool performanceCostAppliedStateValid = false;
	UpscaleMethod performanceCostAppliedUpscaleMethod = UpscaleMethod::kNONE;
	uint32_t performanceCostAppliedQualityMode = 0;
	uint32_t performanceCostAppliedDLSSPreset = 0;
	bool performanceCostAppliedFrameGenerationMode = false;
	bool performanceCostAppliedFSRRuntimePathActive = false;
	bool performanceCostAppliedFSRRuntimeFsr4Configured = false;
	bool performanceCostAppliedFSRRuntimeFsr4Active = false;
	float2 performanceCostAppliedResolutionScale = { 1.0f, 1.0f };
	uint32_t performanceCostAppliedFrame = std::numeric_limits<uint32_t>::max();
	bool performanceCostExecutedPathValid = false;
	bool performanceCostExecutedPathSuccessful = false;
	UpscaleMethod performanceCostExecutedUpscaleMethod = UpscaleMethod::kNONE;
	uint32_t performanceCostExecutedFrame = std::numeric_limits<uint32_t>::max();
	bool frameGenerationCopyValid = false;
	bool frameGenerationCopyRequested = false;
	bool frameGenerationCopySuccessful = false;
	bool frameGenerationCopyConsumed = true;
	bool performanceCostFrameGenerationPresentValid = false;
	bool performanceCostFrameGenerationPresentRequested = false;
	bool performanceCostFrameGenerationPresentSuccessful = false;
	bool performanceCostFrameGenerationPresentActive = false;
	uint32_t performanceCostFrameGenerationPresentFrame = std::numeric_limits<uint32_t>::max();

	bool CopySharedD3D12Resources();
	void PostDisplay();
	bool PerformUpscaling();
	bool UpscaleDepth();
	void RecordPerformanceCostExecutedPath(UpscaleMethod a_method, bool a_successful);
	void RecordFrameGenerationCopy(bool a_requested, bool a_successful);
	void RecordPerformanceCostFrameGenerationPresent(
		bool a_requested,
		bool a_successful,
		bool a_active);
	void RequestHistoryReset();
	bool ShouldResetHistoryThisFrame() const;
	void UpdateHistoryResetState(UpscaleMethod a_upscaleMethod);
	void LatchHistoryResetForCurrentFrame();
	bool IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const;
	bool IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const;

	/**
	 * @brief Resolves the current DLSS intermediate into the main render target.
	 *
	 * Runs in HDR space before tonemapping. Applies RCAS when requested and otherwise
	 * copies the successfully evaluated output without altering it.
	 */
	bool ApplySharpening();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter(bool a_frameGenerationActive);

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs();  // Loads all SDKs at once
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

	using BlurResources = DX12SwapChain::BlurResources;

	// Get all D3D11 resources needed for background blur when D3D12 swap chain is active
	BlurResources GetBlurResources() const;

private:
	void DrawSettingsPanel(bool a_showEmbeddedInfo);
	bool renderDocUpscalingBackendSkipLogged = false;

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state);
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
};
