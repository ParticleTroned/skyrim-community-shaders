#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/NeuralRendering/CharacterRendering.h"
#include "Upscaling/NeuralRendering/PipelinePolicy.h"
#include "Upscaling/RCAS/RCAS.h"
#include "Upscaling/Streamline.h"
#include "Utils/LazyShader.h"
#include <atomic>
#include <cstdint>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <limits>
#include <memory>
#include <mutex>
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
	static constexpr uint32_t kDLSSGMaximumGeneratedFrames = 5;
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
		bool preferFSRFrameGeneration = true;
		bool enableDLSSG = false;
		uint dlssgFramesToGenerate = 1;
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
		bool neuralRenderingEnabled = false;
		NeuralRendering::CharacterSettings neuralCharacter{};
		bool neuralRenderingHalfRate = false;
		bool neuralRenderingResetEveryFrame = false;
		uint neuralRenderingPreset = 3;
		float neuralRenderingIntensity = 0.8f;
		float neuralRenderingLocalTone = 0.75f;
		float neuralRenderingLocalStructure = 0.9f;
		float neuralRenderingSkinStructure = 0.9f;
		uint neuralRenderingStyle = 3;
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
	bool dlssgSupportedAtBoot = false;

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	// Final-output timing for the overlay and performance measurements
	struct FrameGenerationFrameSnapshot
	{
		uint32_t frame = std::numeric_limits<uint32_t>::max();
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		uint64_t interopGeneration = 0;
		bool requested = false;
		bool inputsReady = false;
		bool dlssgFrameReady = false;
		bool uiSeparated = false;
		bool uiPreparedForOutput = false;
		bool valid = false;
	};

	bool IsFrameGenerationDx12PathActive() const;
	bool IsFrameGenerationActive() const;
	bool UsesDLSSGFrameGeneration() const;
	/** @brief Returns whether persistent game-loop state permits generation. */
	[[nodiscard]] bool IsRenderingGameFrames() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	bool AreFrameGenerationInputsReadyForCompositing() const;
	/** @brief Returns whether this frame's UI is physically in the separate target. */
	[[nodiscard]] bool IsFrameGenerationUIPhysicallySeparated() const;
	/** @brief Returns whether provider-ready inputs include separated UI. */
	[[nodiscard]] bool IsFrameGenerationUISeparated() const;
	FrameGenerationFrameSnapshot ConsumeFrameGenerationInputsForPresent();
	void MarkFrameGenerationUISeparated();
	/** @brief Cancels generation while preserving separated UI for Present fallback. */
	void CancelFrameGenerationForSeparatedUI();
	/** @brief Records that separated UI matches the final output encoding. */
	void MarkFrameGenerationUIPreparedForOutput();
	/** @brief Returns the latest validated final-presentation timing sample. */
	[[nodiscard]] DX12SwapChain::OutputPresentationTiming GetOutputPresentationTiming() const;
	bool IsUpscalingActive() const;

	/** @brief Returns whether frame generation is requested by persistent settings. */
	[[nodiscard]] bool IsFrameGenerationConfigured() const;
	/** @brief Returns whether the active backend is configured to limit game FPS. */
	[[nodiscard]] bool IsFrameRateLimitConfigured() const;

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
			{ "preferFSRFrameGeneration", true },
			{ "enableDLSSG", true },
			{ "dlssgFramesToGenerate", true },
			{ "sharpnessFSR", true },
			{ "sharpnessDLSS", true },
			{ "fsr4RuntimeEnable", true },
			{ "fsr4RuntimeSelectionSchemaVersion", true },
			{ "reflexLowLatencyMode", true },
			{ "reflexLowLatencyBoost", true },
			{ "reflexUseMarkersToOptimize", true },
			{ "reflexUseFPSLimit", true },
			{ "reflexFPSLimit", true },
			{ "neuralRenderingEnabled", true },
			{ "neuralCharacter", true },
			{ "neuralRenderingHalfRate", true },
			{ "neuralRenderingResetEveryFrame", true },
			{ "neuralRenderingPreset", true },
			{ "neuralRenderingIntensity", true },
			{ "neuralRenderingLocalTone", true },
			{ "neuralRenderingLocalStructure", true },
			{ "neuralRenderingSkinStructure", true },
			{ "neuralRenderingStyle", true }
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
	virtual void OnSettingsSaved() override;
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

	Util::LazyShader<ID3D11ComputeShader> encodeTexturesCS[4];          // One for each UpscaleMethod
	Util::LazyShader<ID3D11ComputeShader> encodeTexturesCSDepthOutput;  // FSR: converts game depth to typed R32_FLOAT
	ID3D11ComputeShader* GetEncodeTexturesCS();

	Util::LazyShader<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	Util::LazyShader<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	Util::LazyShader<ID3D11PixelShader> cameraMotionVectorsPS;
	ID3D11PixelShader* GetCameraMotionVectorsPS();
	void FillMenuCameraMotionVectors();
	void PrepareMenuCameraMotionVectors();

	Util::LazyShader<ID3D11VertexShader> upscaleVS;
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
	std::unique_ptr<Texture2D> fsrDepthTexture;
	std::unique_ptr<Texture2D> fsrOutputTexture;
	std::unique_ptr<Texture2D> sharpenerTexture;
	std::unique_ptr<Texture2D> neuralRenderingOutputTexture;
	std::unique_ptr<Texture2D> neuralCharacterCompositeTexture;
	std::unique_ptr<ConstantBuffer> neuralCharacterCompositeCB;
	Util::LazyShader<ID3D11ComputeShader> neuralCharacterCompositeCS;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> neuralCharacterMask;
	NeuralRendering::ComputeSubrect neuralCharacterSupport{};

	virtual void ClearShaderCache() override;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline Streamline streamlineDX12{ sl::RenderAPI::eD3D12, Streamline::DLSSGPluginDir };
	static inline FidelityFX fidelityFX;  ///< Only for frame generation
	static inline DX12SwapChain dx12SwapChain;
	static inline RCAS rcas;  ///< Standalone RCAS sharpening for DLSS

	Util::LazyShader<ID3D11PixelShader> copyDepthToSharedBufferPS;
	Util::LazyShader<ID3D11ComputeShader> compositeFrameGenerationUIFallbackCS;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousVendorUpscalerSelected = false;
	// FidelityFX teardown/recreation can span frames while GPU ownership drains.
	// Keep the transition pending until CheckResources observes Ready and commits
	// the previous resource and configuration state.
	bool fsrResourceTransitionPending = false;
	bool upscalingResourcesReady = false;
	bool depthUpscaleUseWideKernel = false;
	bool dlssSharpenerOutputValid = false;
	bool neuralRenderingOutputValid = false;
	bool neuralRenderingHistoryResetRequested = true;
	bool neuralRenderingWasRunnable = false;
	bool neuralRenderingPausedByFrameGeneration = false;
	bool neuralRenderingHalfRateSkipped = false;
	uint64_t neuralRenderingGeneration = 1;
	bool historyResetRequested = true;
	bool historyResetThisFrame = false;
	bool menuCameraMVsValid = false;
	uint32_t historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t historyResetStateUpdatedFrame = std::numeric_limits<uint32_t>::max();
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

	FrameGenerationFrameSnapshot frameGenerationFrame{};
	bool frameGenerationFrameConsumed = true;
	mutable std::mutex frameGenerationFrameMutex;
	enum class BackendLifecycle : uint8_t
	{
		kRunning,
		kShutdownRequested,
		kRetiring,
		kRetired
	};
	std::atomic<BackendLifecycle> backendLifecycle = BackendLifecycle::kRunning;
	std::atomic_bool backendShutdownRequiresPresent = false;

	bool CopySharedD3D12Resources(uint32_t& a_renderWidth, uint32_t& a_renderHeight);
	bool CompositeFrameGenerationUIFallback(bool& a_uiPreparedForOutput);
	void PostDisplay();
	bool PerformUpscaling();
	bool UpscaleDepth();
	void RecordFrameGenerationCopy(
		bool a_requested,
		bool a_successful,
		uint32_t a_renderWidth,
		uint32_t a_renderHeight);
	void RequestHistoryReset();
	bool ShouldResetHistoryThisFrame() const;
	void UpdateHistoryResetState(UpscaleMethod a_upscaleMethod);
	void LatchHistoryResetForCurrentFrame();
	void PrepareHistoryResetForCurrentFrame();
	bool IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const;
	bool IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const;

	/**
	 * @brief Resolves the current DLSS intermediate into the main render target.
	 *
	 * Runs in HDR space before tonemapping. Applies RCAS when requested and otherwise
	 * copies the successfully evaluated output without altering it.
	 */
	bool ApplySharpening();

	/** Returns whether actor classification and capture are needed this frame. */
	[[nodiscard]] bool IsCharacterNeuralRenderingRouteRequested() const;
	/** Returns enabled semantic material categories with nonzero strengths. */
	[[nodiscard]] uint32_t GetCharacterNeuralRenderingCategoryMask() const noexcept;
	/** Returns the sanitized policy gated by the active NR route. */
	[[nodiscard]] NeuralRendering::CharacterSettings GetCharacterNeuralRenderingSettings() const;
	/** Returns the mono SE/AE display extent used for actor admission. */
	[[nodiscard]] bool GetCharacterNeuralRenderingProjectionExtent(uint32_t& a_width, uint32_t& a_height) const;
	/** Applies SE/AE Feature 18 into a private output, with optional character regions. */
	bool ApplyNeuralRendering(
		ID3D11Resource* a_colorInput,
		ID3D11Resource* a_depthGuide,
		ID3D11ShaderResourceView* a_depthGuideSRV,
		ID3D11Resource* a_motionVectors,
		uint32_t a_colorWidth,
		uint32_t a_colorHeight,
		uint32_t a_guideWidth,
		uint32_t a_guideHeight,
		uint32_t a_outputWidth,
		uint32_t a_outputHeight);
	[[nodiscard]] bool IsNeuralRenderingRunnable(UpscaleMethod a_method) const;
	bool EnsureNeuralRenderingOutputTexture();
	bool EnsureNeuralTexture(std::unique_ptr<Texture2D>& a_texture, const char* a_name);
	bool EnsureNeuralCharacterCompositeResources();
	bool CompositeNeuralCharacters(bool a_sharpened, float a_sharpness);
	void DrawNeuralCharacterSettings();
	void DrawNeuralRenderingSettings(UpscaleMethod a_method);
	void ResetNeuralRendering(bool a_releaseBackend);
	static bool ApplyNeuralRenderingPreset(Settings& a_settings, uint32_t a_preset);

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter(bool a_frameGenerationActive);
	void UpdateReflex();
	/** @brief Defers backend shutdown to the owned Present thread. */
	void RequestBackendShutdown();
	/** @brief Returns whether new backend API work must be suppressed. */
	[[nodiscard]] bool IsBackendShutdownRequested() const;
	/** @brief Returns whether window close must wait for a backend Present boundary. */
	[[nodiscard]] bool RequiresPresentThreadBackendShutdown() const;
	/** @brief Returns whether proxy calls must stop before backend retirement. */
	[[nodiscard]] bool AreBackendsRetiringOrShutdown() const;
	/** @brief Returns whether Streamline has reached its terminal shutdown state. */
	[[nodiscard]] bool AreBackendsShutdown() const;
	/** @brief Processes a pending shutdown request at a Present boundary. */
	[[nodiscard]] bool ProcessBackendShutdownRequest();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs(
		bool a_isNvidiaAdapter,
		bool a_allowD3D12Backend);  // Loads only SDKs reachable by this graph.
	HANDLE GetFrameLatencyWaitableObject() const;

	// Backend interface methods
	bool IsBackendInitialized() const;
	void CheckBackendFeatures(IDXGIAdapter* adapter);
	[[nodiscard]] bool UpgradeBackendInterface(void** ppInterface);
	[[nodiscard]] bool SetBackendD3DDevice(ID3D11Device* device);
	void PostBackendDevice();

	// Module availability methods
	bool HasFrameGenModule() const;

	// Proxy interface methods
	void SetProxyD3D11Device(ID3D11Device* device);
	void SetProxyD3D11DeviceContext(ID3D11DeviceContext* context);
	void CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	HRESULT CreateDLSSGSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	void CreateProxyInterop();
	IDXGISwapChain* GetProxySwapChain();

	using BlurResources = DX12SwapChain::BlurResources;

	// Get all D3D11 resources needed for background blur when D3D12 swap chain is active
	BlurResources GetBlurResources() const;

private:
	void DrawSettingsPanel(bool a_showEmbeddedInfo, bool a_allowNone);
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
