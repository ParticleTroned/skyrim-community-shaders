#pragma once

#include "Feature.h"
#include "Upscaling/FidelityFX.h"
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
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 1;
		float sharpnessFSR = 0.0f;
		// FSR3 frame generation. Runs optical flow + interpolation on the DX11
		// (DXVK) device — no DX12, no interop. Off by default. Only meaningful
		// with FSR upscaling active.
		bool frameGeneration = false;
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	struct UpscalingDataCB
	{
		float2 trueSamplingDim;
		float2 pad0;
	};

	ConstantBuffer* jitterCB = nullptr;
	ConstantBuffer* upscalingDataCB = nullptr;

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

	void CheckResources(UpscaleMethod a_upscalemethod);
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);

	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[3];
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	/** @brief True when FSR3 frame generation produced an interpolated frame to present this frame. */
	bool IsFrameGenInterpolatedReady() const;

	/** @brief SRV of the interpolated (display-resolution) frame, or nullptr if none. */
	ID3D11ShaderResourceView* GetFrameGenInterpolatedSRV() const;

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;

	virtual void ClearShaderCache() override;

	static inline RCAS rcas;
	static inline FidelityFX fidelityFX;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousUpscalingWasActive = false;
	bool depthUpscaleUseWideKernel = false;

	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	static void TimerSleepQPC(int64_t targetQPC);

	static double GetRefreshRate(HWND a_window);

private:
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
