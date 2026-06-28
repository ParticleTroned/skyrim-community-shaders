#pragma once

#include "Feature.h"
#include "Upscaling/RCAS/RCAS.h"
#include <d3d11_4.h>
#include <winrt/base.h>

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
	virtual inline bool IsCore() const override { return false; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.upscaling.description", "Advanced upscaling technologies for improved performance"),
			{ T("feature.upscaling.key_feature_2", "FSR (FidelityFX Super Resolution) support"),
				T("feature.upscaling.key_feature_3", "TAA (Temporal Anti-Aliasing) support") } };
	};

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS,
		kXeSS,
	};

	enum class FrameGenMethod
	{
		kFSR,
		kDLSSG,
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kFSR;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 1;
		float sharpnessFSR = 0.0f;
		bool reflexEnabled = false;
		bool reflexBoost = false;
		// Legacy fields kept for JSON backward compatibility.
		bool reflexLowLatencyMode = false;
		bool reflexLowLatencyBoost = false;
		bool frameGeneration = false;
		uint frameGenMethod = (uint)FrameGenMethod::kFSR;
		bool fgShowOnlyGenerated = false;
		bool fgDebugView = false;
		bool fgDebugTearLines = false;
		bool fgDebugPacingLines = false;
		bool hardwareDefaultsApplied = false;
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	ConstantBuffer* jitterCB = nullptr;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	bool IsUpscalingActive() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;

	UpscaleMethod GetUpscaleMethod() const;
	FrameGenMethod GetFrameGenMethod() const;

	void ApplyHardwareDefaults();

	void CheckResources(UpscaleMethod a_upscalemethod);

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	// Motion-vector dilation (restores dev's EncodeTexturesCS behaviour): fills zero-MV pixels (foliage,
	// sky, character — passes that don't write motion) from the longest 5x5 neighbour so the upscalers
	// don't ghost them. Returns the dilated MV resource (display-size N/A — operates at render size), or
	// the original resource if the pass can't run.
	winrt::com_ptr<ID3D11ComputeShader> motionVectorDilateCS;
	ID3D11ComputeShader* GetMotionVectorDilateCS();
	Texture2D* dilatedMotionVectorTexture = nullptr;
	ConstantBuffer* mvDilateCB = nullptr;
	ID3D11Resource* DilateMotionVectors(ID3D11Resource* a_mvTexture, ID3D11ShaderResourceView* a_mvSRV,
		uint32_t a_renderWidth, uint32_t a_renderHeight);

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	bool IsFrameGenerationActive() const;

	// Effective NVIDIA Reflex state under the frame-generation policy: DLSS-G frame-gen forces Reflex ON
	// (DLSS-G stalls without it), FSR frame-gen forces it OFF, and with no frame generation it follows the
	// user's reflexEnabled toggle. Never mutates the saved preference.
	[[nodiscard]] bool GetEffectiveReflex() const;

	HRESULT PresentWithFrameGeneration(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_present);

	// D3D11 textures
	Texture2D* upscaledTexture = nullptr;
	Texture2D* hudlessTexture = nullptr;

	virtual void ClearShaderCache() override;

	static inline RCAS rcas;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousUpscalingWasActive = false;
	// FSR frame generation under the sl.fsr plugin: the last FG-enabled state actually
	// delivered to slFSRFrameGenerationSetOptions (-1 = none yet). Re-synced every frame
	// until it matches the desired state, because featureFSR + the FG entry points come up
	// a few frames after the first CheckResources.
	int fsrFgAppliedState = -1;
	// Last FSR-FG debug-flag signature delivered to the plugin (bit0 view, bit1 tear, bit2 pacing, bit3
	// show-only-generated). Re-pushed when it changes so runtime debug toggles reach the per-present config.
	uint32_t fsrFgDebugApplied = 0;

	// Latched true while DLSS-G frame-gen is active (its present proxy stickily owns the swapchain and
	// bypasses the Vulkan present hooks). A later switch into FSR-FG forces a DXVK swapchain recreate to
	// evict the proxy; cleared once that recreate has been requested. See CheckResources.
	bool dlssgProxyMayOwnPresent = false;

	bool depthUpscaleUseWideKernel = false;

	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	static void TimerSleepQPC(int64_t targetQPC);

	static double GetRefreshRate(HWND a_window);

private:
	void CreateUpscaledTexture();
	void DestroyUpscaledTexture();
	void CreateHudlessTexture();
	void DestroyHudlessTexture();

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

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		static bool Register();
	};
};
