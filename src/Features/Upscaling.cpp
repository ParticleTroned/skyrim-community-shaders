#include "Upscaling.h"

#include "Deferred.h"
#include "Features/RenderDoc.h"
#include "HDRDisplay.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <directx/d3dx12.h>
#include <format>
#include <string_view>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	dlssPreset,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	frameGenerationAllowInMenus,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessDLSS,
	fsr4RuntimeEnable,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	reflexUseMarkersToOptimize,
	reflexUseFPSLimit,
	reflexFPSLimit);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

namespace
{
	std::atomic_bool g_renderDocDllDetected{ false };
	std::atomic_bool g_renderDocUpscalingD3DHookBypassLogged{ false };

	bool IsMainMenuContextActive()
	{
		auto* state = globals::state;
		auto* ui = globals::game::ui;
		return (state && state->isMainMenuOpen) ||
		       (ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME));
	}

	bool IsLoadingMenuContextActive()
	{
		auto* state = globals::state;
		auto* ui = globals::game::ui;
		return (state && state->isLoadingMenuOpen) ||
		       (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	}

	bool IsRenderDocDllLoaded(bool a_probeProcess)
	{
		if (g_renderDocDllDetected.load(std::memory_order_acquire))
			return true;
		if (!a_probeProcess)
			return false;
		if (GetModuleHandleW(L"renderdoc.dll") == nullptr)
			return false;

		g_renderDocDllDetected.store(true, std::memory_order_release);
		return true;
	}

	bool IsRenderDocUpscalingBlocked(bool a_probeProcessForDll = false)
	{
		const auto& renderDoc = globals::features::renderDoc;
		return renderDoc.ShouldBlockUpscaling() || IsRenderDocDllLoaded(a_probeProcessForDll);
	}

	const char* GetRenderDocUpscalingBlockReason()
	{
		const auto& renderDoc = globals::features::renderDoc;
		if (renderDoc.IsAvailable())
			return "RenderDoc capture is active";
		if (renderDoc.enableRenderDocCapture)
			return "RenderDoc capture is enabled";
		return "renderdoc.dll is loaded";
	}

	uint ClampToggleUInt(uint value)
	{
		return value ? 1u : 0u;
	}

	uint ClampQualityModeUInt(uint value)
	{
		return std::min<uint>(value, Upscaling::kQualityModeMaxIndex);
	}

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))) || !texture)
			return false;

		texture->GetDesc(&a_desc);
		return true;
	}

	bool TextureDescMatches(const D3D11_TEXTURE2D_DESC& a_lhs, const D3D11_TEXTURE2D_DESC& a_rhs)
	{
		return a_lhs.Width == a_rhs.Width &&
		       a_lhs.Height == a_rhs.Height &&
		       a_lhs.MipLevels == a_rhs.MipLevels &&
		       a_lhs.ArraySize == a_rhs.ArraySize &&
		       a_lhs.Format == a_rhs.Format &&
		       a_lhs.SampleDesc.Count == a_rhs.SampleDesc.Count &&
		       a_lhs.SampleDesc.Quality == a_rhs.SampleDesc.Quality &&
		       a_lhs.Usage == a_rhs.Usage &&
		       a_lhs.BindFlags == a_rhs.BindFlags &&
		       a_lhs.CPUAccessFlags == a_rhs.CPUAccessFlags &&
		       a_lhs.MiscFlags == a_rhs.MiscFlags;
	}

	bool TextureMatchesRequirements(const std::unique_ptr<Texture2D>& a_texture, const D3D11_TEXTURE2D_DESC& a_expectedDesc, bool a_requireSRV, bool a_requireUAV)
	{
		if (!a_texture || !TextureDescMatches(a_texture->desc, a_expectedDesc))
			return false;
		if (a_requireSRV && !a_texture->srv)
			return false;
		if (a_requireUAV && !a_texture->uav)
			return false;
		return true;
	}

	struct ScopedFullscreenPipelineState
	{
		explicit ScopedFullscreenPipelineState(ID3D11DeviceContext* a_context) :
			context(a_context)
		{
			if (!context)
				return;

			viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			context->RSGetViewports(&viewportCount, viewports);

			context->IAGetInputLayout(inputLayout.put());
			context->IAGetPrimitiveTopology(&primitiveTopology);

			ID3D11Buffer* rawVertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
			context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, rawVertexBuffers, vertexStrides, vertexOffsets);
			for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
				vertexBuffers[i].attach(rawVertexBuffers[i]);

			ID3D11Buffer* rawIndexBuffer = nullptr;
			context->IAGetIndexBuffer(&rawIndexBuffer, &indexFormat, &indexOffset);
			indexBuffer.attach(rawIndexBuffer);

			context->VSGetShader(vertexShader.put(), nullptr, nullptr);
			context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			context->GSGetShader(geometryShader.put(), nullptr, nullptr);
			context->HSGetShader(hullShader.put(), nullptr, nullptr);
			context->DSGetShader(domainShader.put(), nullptr, nullptr);
			context->RSGetState(rasterizerState.put());
			context->OMGetBlendState(blendState.put(), blendFactor, &sampleMask);
			context->OMGetDepthStencilState(depthStencilState.put(), &stencilRef);

			ID3D11RenderTargetView* rawRenderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			ID3D11DepthStencilView* rawDepthStencilView = nullptr;
			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRenderTargets, &rawDepthStencilView);
			for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				renderTargets[i].attach(rawRenderTargets[i]);
			depthStencilView.attach(rawDepthStencilView);

			ID3D11ShaderResourceView* rawPixelShaderResources[1]{};
			context->PSGetShaderResources(0, 1, rawPixelShaderResources);
			pixelShaderResources[0].attach(rawPixelShaderResources[0]);

			ID3D11Buffer* rawPixelShaderConstantBuffer = nullptr;
			context->PSGetConstantBuffers(1, 1, &rawPixelShaderConstantBuffer);
			pixelShaderConstantBuffer1.attach(rawPixelShaderConstantBuffer);
		}

		~ScopedFullscreenPipelineState()
		{
			if (!context)
				return;

			context->RSSetViewports(viewportCount, viewportCount ? viewports : nullptr);
			context->IASetInputLayout(inputLayout.get());
			context->IASetPrimitiveTopology(primitiveTopology);

			ID3D11Buffer* rawVertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
			for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
				rawVertexBuffers[i] = vertexBuffers[i].get();
			context->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, rawVertexBuffers, vertexStrides, vertexOffsets);
			context->IASetIndexBuffer(indexBuffer.get(), indexFormat, indexOffset);

			context->VSSetShader(vertexShader.get(), nullptr, 0);
			context->PSSetShader(pixelShader.get(), nullptr, 0);
			context->GSSetShader(geometryShader.get(), nullptr, 0);
			context->HSSetShader(hullShader.get(), nullptr, 0);
			context->DSSetShader(domainShader.get(), nullptr, 0);
			context->RSSetState(rasterizerState.get());
			context->OMSetBlendState(blendState.get(), blendFactor, sampleMask);
			context->OMSetDepthStencilState(depthStencilState.get(), stencilRef);

			ID3D11RenderTargetView* rawRenderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				rawRenderTargets[i] = renderTargets[i].get();
			context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRenderTargets, depthStencilView.get());

			ID3D11ShaderResourceView* rawPixelShaderResources[1] = { pixelShaderResources[0].get() };
			context->PSSetShaderResources(0, 1, rawPixelShaderResources);
			ID3D11Buffer* rawPixelShaderConstantBuffer = pixelShaderConstantBuffer1.get();
			context->PSSetConstantBuffers(1, 1, &rawPixelShaderConstantBuffer);
		}

		ID3D11DeviceContext* context = nullptr;
		UINT viewportCount = 0;
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		winrt::com_ptr<ID3D11InputLayout> inputLayout;
		D3D11_PRIMITIVE_TOPOLOGY primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		winrt::com_ptr<ID3D11Buffer> vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT vertexStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
		UINT vertexOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
		winrt::com_ptr<ID3D11Buffer> indexBuffer;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
		UINT indexOffset = 0;
		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		winrt::com_ptr<ID3D11GeometryShader> geometryShader;
		winrt::com_ptr<ID3D11HullShader> hullShader;
		winrt::com_ptr<ID3D11DomainShader> domainShader;
		winrt::com_ptr<ID3D11RasterizerState> rasterizerState;
		winrt::com_ptr<ID3D11BlendState> blendState;
		FLOAT blendFactor[4]{};
		UINT sampleMask = 0xffffffff;
		winrt::com_ptr<ID3D11DepthStencilState> depthStencilState;
		UINT stencilRef = 0;
		winrt::com_ptr<ID3D11RenderTargetView> renderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		winrt::com_ptr<ID3D11DepthStencilView> depthStencilView;
		winrt::com_ptr<ID3D11ShaderResourceView> pixelShaderResources[1];
		winrt::com_ptr<ID3D11Buffer> pixelShaderConstantBuffer1;
	};
	uint MigrateLegacyQualityModeUInt(uint value)
	{
		switch (value) {
		case 0:
			return 0u;
		case 1:
			return 3u;
		case 2:
			return 4u;
		case 3:
			return 5u;
		case 4:
			return 6u;
		case 5:
		case 6:
			return value;
		default:
			return 6u;
		}
	}

	uint MigrateLegacyDLSSPresetUInt(uint value)
	{
		// Legacy values were 0=Default, 1=J, 2=K, 3=L, 4=M.
		switch (value) {
		case 1:
			return Upscaling::kDLSSPresetJ;
		case 2:
			return Upscaling::kDLSSPresetK;
		case 3:
			return Upscaling::kDLSSPresetL;
		case 4:
			return Upscaling::kDLSSPresetM;
		default:
			return Upscaling::kDLSSPresetK;
		}
	}

	constexpr std::array kDLSSProfileDisplayOrder = {
		Upscaling::kDLSSPresetE,
		Upscaling::kDLSSPresetF,
		Upscaling::kDLSSPresetJ,
		Upscaling::kDLSSPresetK,
		Upscaling::kDLSSPresetL,
		Upscaling::kDLSSPresetM
	};

	const char* GetDLSSPresetLabel(uint32_t preset)
	{
		switch (preset) {
		case Upscaling::kDLSSPresetE:
			return "E";
		case Upscaling::kDLSSPresetF:
			return "F";
		case Upscaling::kDLSSPresetJ:
			return "J";
		case Upscaling::kDLSSPresetK:
			return "K";
		case Upscaling::kDLSSPresetL:
			return "L";
		case Upscaling::kDLSSPresetM:
			return "M";
		default:
			return "K";
		}
	}

	void DrawDLSSPresetTooltip(uint32_t preset)
	{
		switch (preset) {
		case Upscaling::kDLSSPresetJ:
			ImGui::TextUnformatted("DLAA/Quality/Balanced preset. Slightly less ghosting than K, but more flicker. Speed: about K. Use only if K ghosts.");
			break;
		case Upscaling::kDLSSPresetK:
			ImGui::TextUnformatted("Default for DLAA/Quality/Balanced. Best all-round stability and image quality. Speed: fast. Recommended for most users.");
			break;
		case Upscaling::kDLSSPresetL:
			ImGui::TextUnformatted("Default for Ultra Performance on newer RTX cards. Sharper and more stable, but higher cost than J/K/F.");
			ImGui::TextUnformatted("For RTX 3000-series cards, F is usually the better Performance/Ultra Performance choice.");
			break;
		case Upscaling::kDLSSPresetM:
			ImGui::TextUnformatted("Default for Performance on newer RTX cards. Similar image-quality improvements to L, closer in speed to J/K.");
			ImGui::TextUnformatted("For RTX 3000-series cards, F is usually the better Performance/Ultra Performance choice.");
			break;
		case Upscaling::kDLSSPresetF:
			ImGui::TextUnformatted("Legacy/deprecated preset. Best Performance/Ultra Performance starting point for RTX 3000-series cards.");
			ImGui::TextUnformatted("If you want the adjacent legacy comparison profile, try E.");
			break;
		case Upscaling::kDLSSPresetE:
			ImGui::TextUnformatted("Legacy/deprecated preset. Secondary comparison option next to F for older DLSS behavior.");
			ImGui::TextUnformatted("On RTX 3000-series cards, start with F first, then compare E if you want another legacy profile.");
			break;
		default:
			ImGui::TextUnformatted("Default for DLAA/Quality/Balanced. Best all-round stability and image quality. Speed: fast. Recommended for most users.");
			break;
		}
	}

	float ClampFiniteUnitRange(float value, float fallback)
	{
		if (!std::isfinite(value))
			return fallback;
		return std::clamp(value, 0.0f, 1.0f);
	}

	const char* GetQualityModeName(uint value, bool isDLSS)
	{
		switch (ClampQualityModeUInt(value)) {
		case 1:
			return "Hoshipa";
		case 2:
			return "Ultra Quality";
		case 3:
			return "Quality";
		case 4:
			return "Balanced";
		case 5:
			return "Performance";
		case 6:
			return "Ultra Performance";
		default:
			return isDLSS ? "DLAA" : "Native AA";
		}
	}

	void SanitizeUpscalingSettings(Upscaling::Settings& settings)
	{
		settings.upscaleMethod = std::min<uint>(settings.upscaleMethod, static_cast<uint>(Upscaling::UpscaleMethod::kDLSS));
		settings.upscaleMethodNoDLSS = std::min<uint>(settings.upscaleMethodNoDLSS, static_cast<uint>(Upscaling::UpscaleMethod::kFSR));
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		settings.dlssPreset = std::min<uint>(settings.dlssPreset, Upscaling::kDLSSPresetMaxIndex);
		settings.frameLimitMode = ClampToggleUInt(settings.frameLimitMode);
		settings.frameGenerationMode = ClampToggleUInt(settings.frameGenerationMode);
		settings.frameGenerationForceEnable = ClampToggleUInt(settings.frameGenerationForceEnable);
		settings.streamlineLogLevel = std::min<uint>(settings.streamlineLogLevel, 2u);
		settings.sharpnessFSR = ClampFiniteUnitRange(settings.sharpnessFSR, 0.0f);
		settings.sharpnessDLSS = ClampFiniteUnitRange(settings.sharpnessDLSS, Upscaling::kDefaultDLSSSharpness);
		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
	}

	void DrawFrameGenerationEnabledToggle(Upscaling::Settings& a_settings)
	{
		bool enabled = a_settings.frameGenerationMode != 0;
		if (ImGui::Checkbox("Frame Generation", &enabled))
			a_settings.frameGenerationMode = enabled ? 1u : 0u;
	}

	void DrawFrameGenerationForceEnableToggle(Upscaling& a_upscaling)
	{
		ImGui::TextWrapped("Allows frame generation to function on low refresh rate monitors. Detected: %.2f Hz", a_upscaling.refreshRate);
		bool forceEnabled = a_upscaling.settings.frameGenerationForceEnable != 0;
		if (ImGui::Checkbox("Force Enable Frame Generation", &forceEnabled))
			a_upscaling.settings.frameGenerationForceEnable = forceEnabled ? 1u : 0u;
	}

}

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced upscaling and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChainUpscaling(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description);

	auto& upscaling = globals::features::upscaling;
	if (IsRenderDocUpscalingBlocked(true)) {
		if (!g_renderDocUpscalingD3DHookBypassLogged.exchange(true, std::memory_order_acq_rel)) {
			logger::warn(
				"[Upscaling] Bypassing D3D11 upscaling device hook because {}.",
				GetRenderDocUpscalingBlockReason());
		}
		return ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
			DriverType,
			Software,
			Flags,
			pFeatureLevels,
			FeatureLevels,
			SDKVersion,
			pSwapChainDesc,
			ppSwapChain,
			ppDevice,
			pFeatureLevel,
			ppImmediateContext);
	}

	upscaling.LoadUpscalingSDKs();

	if (upscaling.IsBackendInitialized())
		upscaling.CheckBackendFeatures(pAdapter);

	// FLIP_DISCARD requires BufferCount >= 2 and a flip-model-compatible (non-sRGB) format.
	pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	if (pSwapChainDesc->BufferCount < 2)
		pSwapChainDesc->BufferCount = 2;

	if (globals::features::hdrDisplay.loaded) {
		logger::info("[Upscaling] Upgrading swap chain format from {} to R10G10B10A2_UNORM for HDR", static_cast<int>(pSwapChainDesc->BufferDesc.Format));
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	bool shouldProxy = true;
	if (shouldProxy)
		if (!pSwapChainDesc->Windowed)
			shouldProxy = false;

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;

	if (shouldProxy) {
		if (upscaling.settings.frameGenerationMode)
			if (refreshRate >= 120)
				shouldProxy = true;
			else if (upscaling.settings.frameGenerationForceEnable)
				shouldProxy = true;
			else
				shouldProxy = false;
		else
			shouldProxy = false;
	}

	upscaling.lowRefreshRate = refreshRate < 120;
	upscaling.isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info("[Frame Generation] Frame Generation enabled, using D3D12 proxy");

		if (upscaling.HasFrameGenModule()) {
			DX::ThrowIfFailed(D3D11CreateDevice(
				pAdapter,
				DriverType,
				Software,
				Flags,
				&featureLevel,
				1,
				SDKVersion,
				ppDevice,
				pFeatureLevel,
				ppImmediateContext));

			upscaling.SetProxyD3D11Device(*ppDevice);
			upscaling.SetProxyD3D11DeviceContext(*ppImmediateContext);
			upscaling.CreateProxySwapChain(pAdapter, *pSwapChainDesc);
			upscaling.CreateProxyInterop();

			*ppSwapChain = upscaling.GetProxySwapChain();

			upscaling.d3d12SwapChainActive = true;

			if (upscaling.IsBackendInitialized()) {
				upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
				// Don't wrap the swap chain with Streamline when using the D3D12
				// proxy.  The proxy's GetDevice() returns the D3D11 device for
				// IID_ID3D11Device, which other SKSE plugins (e.g. SkyrimPlatform)
				// rely on.  Streamline's wrapper would bypass this override and
				// forward to the underlying D3D12 swap chain, causing
				// E_NOINTERFACE.  The proxy must remain the outermost layer.
				upscaling.SetBackendD3DDevice(*ppDevice);
				// Some features (notably Reflex/PCL) may report availability only after device bind.
				upscaling.CheckBackendFeatures(pAdapter);
				upscaling.PostBackendDevice();
			}

			return S_OK;
		} else {
			logger::warn("[Frame Generation] FidelityFX DLLs are not loaded, skipping proxy");
			upscaling.fidelityFXMissing = true;
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (upscaling.IsBackendInitialized()) {
		upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
		upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
		upscaling.SetBackendD3DDevice(*ppDevice);
		// Re-check after device bind to ensure feature availability is accurate.
		upscaling.CheckBackendFeatures(pAdapter);
		upscaling.PostBackendDevice();
	}

	return ret;
}

void Upscaling::DrawSettings()
{
	struct UpscaleUiChoice
	{
		UpscaleMethod method;
		bool useRuntimeFsr4;
		const char* label;
	};

	const bool isNvidiaAdapter = fidelityFX.IsNvidiaAdapterDetected();
	const bool runtimeUpscalerPresent = fidelityFX.IsRuntimeUpscalerPresent();
	const bool runtimeFsr4AutoEligible = fidelityFX.IsRuntimeFsr4AutoEligible();
	const bool featureDLSS = streamline.featureDLSS;
	const bool renderDocBlocksUpscaling = IsRenderDocUpscalingBlocked();
	uint32_t* currentUpscaleMode = &settings.upscaleMethod;
	if (!featureDLSS)
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;

	if (!renderDocBlocksUpscaling &&
		*currentUpscaleMode == static_cast<uint32_t>(UpscaleMethod::kFSR) &&
		!runtimeFsr4AutoEligible)
		settings.fsr4RuntimeEnable = false;

	std::vector<UpscaleUiChoice> upscaleChoices = {
		{ UpscaleMethod::kNONE, false, "None" }
	};
	if (!renderDocBlocksUpscaling) {
		upscaleChoices.push_back({ UpscaleMethod::kTAA, false, "TAA" });
		upscaleChoices.push_back({ UpscaleMethod::kFSR, false, "AMD FSR3" });
		if (runtimeFsr4AutoEligible)
			upscaleChoices.push_back({ UpscaleMethod::kFSR, true, "AMD FSR4" });
		if (featureDLSS)
			upscaleChoices.push_back({ UpscaleMethod::kDLSS, false, "NVIDIA DLSS" });
	}

	auto matchesCurrentChoice = [&](const UpscaleUiChoice& choice) {
		if (static_cast<uint32_t>(choice.method) != *currentUpscaleMode)
			return false;
		if (choice.method == UpscaleMethod::kFSR)
			return settings.fsr4RuntimeEnable == choice.useRuntimeFsr4;
		return true;
	};

	int methodUiIndex = 0;
	for (int i = 0; i < static_cast<int>(upscaleChoices.size()); ++i) {
		if (matchesCurrentChoice(upscaleChoices[i])) {
			methodUiIndex = i;
			break;
		}
	}

	if (methodUiIndex == 0 && !matchesCurrentChoice(upscaleChoices[0])) {
		for (int i = 0; i < static_cast<int>(upscaleChoices.size()); ++i) {
			if (static_cast<uint32_t>(upscaleChoices[i].method) == *currentUpscaleMode) {
				methodUiIndex = i;
				break;
			}
		}
	}

	const char* currentMethodLabel = upscaleChoices[methodUiIndex].label;
	if (renderDocBlocksUpscaling)
		ImGui::BeginDisabled();
	const bool methodChanged = ImGui::SliderInt("Method", &methodUiIndex, 0, static_cast<int>(upscaleChoices.size() - 1), currentMethodLabel);
	if (renderDocBlocksUpscaling)
		ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (renderDocBlocksUpscaling) {
			ImGui::Text("Runtime is forced to None while %s.", GetRenderDocUpscalingBlockReason());
		} else {
			ImGui::TextUnformatted("Selects the upscaling backend.");
			if (runtimeFsr4AutoEligible)
				ImGui::TextUnformatted("Range: choose between TAA, DLSS, FSR3, FSR4, or None.");
			else
				ImGui::TextUnformatted("Range: choose between TAA, DLSS, FSR3, or None.");
		}
	}
	methodUiIndex = std::clamp(methodUiIndex, 0, static_cast<int>(upscaleChoices.size() - 1));
	const auto& selectedUpscaleChoice = upscaleChoices[methodUiIndex];
	const bool shouldApplyMethodSelection = !renderDocBlocksUpscaling && (methodChanged || !matchesCurrentChoice(selectedUpscaleChoice));
	if (shouldApplyMethodSelection) {
		*currentUpscaleMode = static_cast<uint32_t>(selectedUpscaleChoice.method);
		if (selectedUpscaleChoice.method == UpscaleMethod::kFSR)
			settings.fsr4RuntimeEnable = selectedUpscaleChoice.useRuntimeFsr4;
	}
	if (renderDocBlocksUpscaling) {
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
		ImGui::TextWrapped(
			"Community Shaders Upscaling runs as None while %s to avoid DLSS/FSR backend startup crashes.",
			GetRenderDocUpscalingBlockReason());
		ImGui::PopStyleColor();
	}

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();
	const bool runtimeFsr4Requested =
		upscaleMethod == UpscaleMethod::kFSR &&
		settings.fsr4RuntimeEnable;
	if (upscaleMethod == UpscaleMethod::kFSR) {
		const auto& hostFsrSdkLabel = FidelityFX::GetHostFsrSdkLabel();
		const auto& runtimeFsr3Label = FidelityFX::GetRuntimeUpscalerLabel(FidelityFX::Fsr3Version);
		ImGui::TextDisabled("FSR path: %s", fidelityFX.GetDisplayedFsrPathLabel().c_str());
		if (fidelityFX.IsRuntimeUpscalerFailureLatched()) {
			ImGui::TextDisabled("Runtime FSR path is latched off after a runtime failure; using %s fallback.", hostFsrSdkLabel.c_str());
		} else if (fidelityFX.IsRuntimeFsr4FailureLatched()) {
			ImGui::TextDisabled("FSR4 is latched off after a runtime failure; using %s.", runtimeFsr3Label.c_str());
		} else if (fidelityFX.HasRuntimeUpscalerSupportCheckResult() &&
				   !fidelityFX.IsRuntimeUpscalerSupportConfirmed()) {
			ImGui::TextDisabled("Runtime FSR context creation failed; using %s fallback.", hostFsrSdkLabel.c_str());
		}
		if (!runtimeUpscalerPresent && runtimeFsr4Requested)
			ImGui::TextDisabled("FSR4 unavailable: missing FidelityFX upscaler runtime.");
	}

	// Display warning for DLSS resolution limits.
	if (upscaleMethod == UpscaleMethod::kDLSS) {
		auto viewport = globals::game::graphicsState;
		const float screenWidth = static_cast<float>(viewport ? viewport->screenWidth : 0);
		const float screenHeight = static_cast<float>(viewport ? viewport->screenHeight : 0);

		if (screenWidth > streamline.MAX_RESOLUTION || screenHeight > streamline.MAX_RESOLUTION) {
			Util::Text::Warning("Warning: Requested resolution %.0f x %.0f exceeds maximum supported resolution %d x %d for DLSS.",
				screenWidth, screenHeight, streamline.MAX_RESOLUTION, streamline.MAX_RESOLUTION);
			Util::Text::Warning("DLSS will not function. Lower your resolution or select a different upscaling method.");
		}
	}

	// Display upscaling settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		const char* baseLabel = GetQualityModeName(settings.qualityMode, upscaleMethod == UpscaleMethod::kDLSS);
		std::string labelWithScale = std::format("{} ( {:.2f}x )", baseLabel, GetQualityModeResolutionScale(settings.qualityMode));
		int qualityMode = static_cast<int>(settings.qualityMode);
		if (ImGui::SliderInt("Upscale Preset", &qualityMode, 0, static_cast<int>(kQualityModeMaxIndex), labelWithScale.c_str()))
			settings.qualityMode = static_cast<uint>(std::clamp(qualityMode, 0, static_cast<int>(kQualityModeMaxIndex)));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls the shared DLSS/FSR3/FSR4 internal render scale / quality level.");
			ImGui::TextUnformatted("Range: low 0 (highest quality, lowest performance gain) to high 6 (highest performance gain, lowest quality).");
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Adjusts post-upscale sharpness for FSR.");
				ImGui::TextUnformatted("Range: low 0.0 (softest) to high 1.0 (sharpest).");
			}
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			settings.dlssPreset = std::min(settings.dlssPreset, kDLSSPresetMaxIndex);

			int dlssProfileUiIndex = 0;
			for (int i = 0; i < static_cast<int>(kDLSSProfileDisplayOrder.size()); ++i) {
				if (kDLSSProfileDisplayOrder[i] == settings.dlssPreset) {
					dlssProfileUiIndex = i;
					break;
				}
			}

			const int dlssProfileUiMaxIndex = static_cast<int>(kDLSSProfileDisplayOrder.size()) - 1;
			ImGui::SliderInt("DLSS Profile", &dlssProfileUiIndex, 0, dlssProfileUiMaxIndex, GetDLSSPresetLabel(kDLSSProfileDisplayOrder[dlssProfileUiIndex]));
			dlssProfileUiIndex = std::clamp(dlssProfileUiIndex, 0, dlssProfileUiMaxIndex);
			settings.dlssPreset = kDLSSProfileDisplayOrder[dlssProfileUiIndex];
			if (auto _tt = Util::HoverTooltipWrapper()) {
				DrawDLSSPresetTooltip(settings.dlssPreset);
			}

			ImGui::SliderFloat("Sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Adjusts post-upscale sharpness for DLSS.");
				ImGui::TextUnformatted("Range: 0.0 off/softest to 1.0 sharpest.");
			}

			if (isNvidiaAdapter) {
				ImGui::TextWrapped("Note: Use K for DLAA/Quality/Balanced. For Performance and Ultra Performance, use L/M on newer RTX cards. On RTX 3000-series cards, start with F and compare E if you want the other legacy profile.");
			}
		}
	}

	const bool frameGenerationDx12PathActive = IsFrameGenerationDx12PathActive();

	if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
		ImGui::Text("Uses AMD FSR Frame Generation technology");
		if (HasFrameGenModule())
			ImGui::Text("AMD FSR Frame Generation is available.");
		ImGui::Text("Requires a D3D11 to D3D12 proxy which can create compatibility issues");
		ImGui::Text("Toggling this setting requires a restart to work correctly");

		bool onlyRequiresRestart = true;

		if (!isWindowed) {
			Util::Text::Warning("Warning: Requires windowed mode");

			onlyRequiresRestart = false;
		}

		if (lowRefreshRate && !settings.frameGenerationForceEnable) {
			Util::Text::Warning("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");

			onlyRequiresRestart = false;
		}

		if (fidelityFXMissing) {
			Util::Text::Warning("Warning: FidelityFX DLLs are not loaded");

			onlyRequiresRestart = false;
		}

		if (onlyRequiresRestart && settings.frameGenerationMode && !frameGenerationDx12PathActive)
			Util::Text::Warning("Warning: Requires restart");

		if (!settings.frameGenerationMode && frameGenerationDx12PathActive)
			Util::Text::Warning("Warning: Requires restart");

		DrawFrameGenerationEnabledToggle(settings);

		if (!frameGenerationDx12PathActive)
			ImGui::BeginDisabled();

		bool flEnabled = settings.frameLimitMode != 0;
		if (ImGui::Checkbox("Frame Limit (Variable Refresh Rate)", &flEnabled))
			settings.frameLimitMode = flEnabled ? 1 : 0;

		if (!frameGenerationDx12PathActive)
			ImGui::EndDisabled();

		DrawFrameGenerationForceEnableToggle(*this);

		ImGui::Checkbox("Frame Generation in Menus", &settings.frameGenerationAllowInMenus);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps frame generation active while game menus are open.");
			ImGui::TextUnformatted("May feel smoother, but increases menu input latency.");
		}

		ImGui::TreePop();
	}

	if (streamline.reflexSupportedOnCurrentAdapter && ImGui::TreeNodeEx("NVIDIA Reflex", ImGuiTreeNodeFlags_DefaultOpen)) {
		const bool reflexBlockedByFrameGeneration = frameGenerationDx12PathActive;
		const bool reflexAvailable = streamline.initialized && streamline.featureReflex;
		const bool reflexControlsAvailable = reflexAvailable && !reflexBlockedByFrameGeneration;
		const bool markerOptimizationAvailable = reflexControlsAvailable && streamline.featurePCL;
		if (reflexBlockedByFrameGeneration) {
			ImGui::TextDisabled("Reflex is unavailable while the DX12 frame-generation swapchain is active.");
		}

		if (!reflexAvailable) {
			ImGui::TextDisabled("Reflex is not available. Ensure sl.reflex.dll is present and restart.");
		}

		if (!reflexControlsAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox("Low Latency Mode", &settings.reflexLowLatencyMode);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Cuts input delay by syncing CPU work closer to the GPU.");
			ImGui::TextUnformatted("Can reduce max FPS a little, but usually feels more responsive.");
		}

		if (!settings.reflexLowLatencyMode)
			ImGui::BeginDisabled();

		ImGui::Checkbox("Low Latency Boost", &settings.reflexLowLatencyBoost);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps GPU clocks higher to avoid latency spikes at low GPU load.");
			ImGui::TextUnformatted("Useful if frametime jumps; costs extra power and heat.");
		}

		if (!markerOptimizationAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox("Use Markers To Optimize", &settings.reflexUseMarkersToOptimize);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses frame markers for tighter Reflex timing.");
			ImGui::TextUnformatted("Try On first; turn Off if it causes stutter on your setup.");
		}

		if (!markerOptimizationAvailable)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable) {
			ImGui::TextDisabled("Marker optimization unavailable (PCL not loaded).");
		}

		ImGui::Checkbox("Use FPS Limit", &settings.reflexUseFPSLimit);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses Reflex's internal FPS cap for steadier frametimes.");
			ImGui::TextUnformatted("Can lower latency versus uncapped rendering.");
		}

		if (!settings.reflexLowLatencyMode)
			ImGui::EndDisabled();

		if (!settings.reflexUseFPSLimit)
			ImGui::BeginDisabled();

		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
		ImGui::SliderFloat("FPS Limit", &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Set your frame cap target.");
			ImGui::TextUnformatted("Start about 2-3 FPS below refresh rate (e.g. 117 for 120 Hz).");
		}

		if (!settings.reflexUseFPSLimit)
			ImGui::EndDisabled();

		if (!reflexControlsAvailable)
			ImGui::EndDisabled();

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Backend Diagnostics")) {
		// Streamline log level selection
		const char* logLevels[] = { "Off", "Default", "Verbose" };
		int logLevelIdx = static_cast<int>(settings.streamlineLogLevel);
		if (ImGui::Combo("Streamline Logging", &logLevelIdx, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<uint>(logLevelIdx);
		}
		ImGui::TextUnformatted("Changing this requires a restart to take effect.");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Streamline logging controls the verbosity of NVIDIA Streamline backend logs. Useful for debugging issues with DLSS/DLSS-G.");
		}

		ImGui::Separator();
		Util::DrawDllVersionTable("AMD FidelityFX DLLs (click to open folder)", FidelityFX::PluginDir, FidelityFX::dllVersions, "ffx_dll_versions");
		Util::DrawDllVersionTable("NVIDIA Streamline DLLs (click to open folder)", Streamline::PluginDir, Streamline::dllVersions, "sl_dll_versions");
		ImGui::TreePop();
	}
}

void Upscaling::SaveSettings(json& o_json)
{
	SanitizeUpscalingSettings(settings);
	o_json = settings;
	o_json["qualityModeSchemaVersion"] = 2;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->WriteSetting(setting);
		}
	}
}

void Upscaling::DrawEssentialSettings()
{
	struct UpscaleUiChoice
	{
		UpscaleMethod method;
		bool useRuntimeFsr4;
		const char* label;
	};

	const bool renderDocBlocksUpscaling = IsRenderDocUpscalingBlocked();
	const bool featureDLSS = streamline.featureDLSS;
	const bool runtimeFsr4AutoEligible = fidelityFX.IsRuntimeFsr4AutoEligible();
	uint32_t* currentUpscaleMode = featureDLSS ? &settings.upscaleMethod : &settings.upscaleMethodNoDLSS;
	if (!renderDocBlocksUpscaling &&
		*currentUpscaleMode == static_cast<uint32_t>(UpscaleMethod::kFSR) &&
		!runtimeFsr4AutoEligible) {
		settings.fsr4RuntimeEnable = false;
	}

	std::vector<UpscaleUiChoice> choices = { { UpscaleMethod::kNONE, false, "None" } };
	if (!renderDocBlocksUpscaling) {
		choices.push_back({ UpscaleMethod::kTAA, false, "TAA" });
		choices.push_back({ UpscaleMethod::kFSR, false, "AMD FSR3" });
		if (runtimeFsr4AutoEligible)
			choices.push_back({ UpscaleMethod::kFSR, true, "AMD FSR4" });
		if (featureDLSS)
			choices.push_back({ UpscaleMethod::kDLSS, false, "NVIDIA DLSS" });
	}

	auto matchesCurrentChoice = [&](const UpscaleUiChoice& choice) {
		if (static_cast<uint32_t>(choice.method) != *currentUpscaleMode)
			return false;
		return choice.method != UpscaleMethod::kFSR || settings.fsr4RuntimeEnable == choice.useRuntimeFsr4;
	};

	int methodIndex = 0;
	for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
		if (matchesCurrentChoice(choices[i])) {
			methodIndex = i;
			break;
		}
	}

	ImGui::BeginDisabled(renderDocBlocksUpscaling);
	const bool methodChanged = ImGui::SliderInt("Method", &methodIndex, 0, static_cast<int>(choices.size() - 1), choices[methodIndex].label);
	ImGui::EndDisabled();
	methodIndex = std::clamp(methodIndex, 0, static_cast<int>(choices.size() - 1));
	const auto& selected = choices[methodIndex];
	if (!renderDocBlocksUpscaling && (methodChanged || !matchesCurrentChoice(selected))) {
		*currentUpscaleMode = static_cast<uint32_t>(selected.method);
		if (selected.method == UpscaleMethod::kFSR)
			settings.fsr4RuntimeEnable = selected.useRuntimeFsr4;
	}
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(renderDocBlocksUpscaling ? "Upscaling is disabled while RenderDoc capture is active." : "Selects the upscaling backend.");

	const auto method = GetUpscaleMethod();
	if (method != UpscaleMethod::kNONE && method != UpscaleMethod::kTAA) {
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		const char* baseLabel = GetQualityModeName(settings.qualityMode, method == UpscaleMethod::kDLSS);
		const std::string qualityLabel = std::format("{} ( {:.2f}x )", baseLabel, GetQualityModeResolutionScale(settings.qualityMode));
		int qualityMode = static_cast<int>(settings.qualityMode);
		if (ImGui::SliderInt("Upscale Preset", &qualityMode, 0, static_cast<int>(kQualityModeMaxIndex), qualityLabel.c_str())) {
			settings.qualityMode = static_cast<uint>(std::clamp(qualityMode, 0, static_cast<int>(kQualityModeMaxIndex)));
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Controls the internal render scale and quality level.");

		if (method == UpscaleMethod::kFSR) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
		} else if (method == UpscaleMethod::kDLSS) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");
		}
	}

	ImGui::SeparatorText("Frame Generation");
	DrawFrameGenerationEnabledToggle(settings);
	DrawFrameGenerationForceEnableToggle(*this);
}

void Upscaling::LoadSettings(json& o_json)
{
	const bool hasQualityModeSchemaVersion = o_json.contains("qualityModeSchemaVersion");
	const bool hasDLSSPreset = o_json.contains("dlssPreset");
	const uint legacyDLSSPreset = o_json.value("presetDLSS", 0u);
	settings = o_json;
	if (!hasQualityModeSchemaVersion)
		settings.qualityMode = MigrateLegacyQualityModeUInt(settings.qualityMode);
	if (!hasDLSSPreset)
		settings.dlssPreset = MigrateLegacyDLSSPresetUInt(legacyDLSSPreset);

	// Sanitize loaded settings to ensure enum indices are valid
	constexpr auto enumCount = 4;  // UpscaleMethod has 4 values: kNONE, kTAA, kFSR, kDLSS
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethod = enumCount ? enumCount - 1 : 0;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethodNoDLSS {} out of range, clamping to {}", settings.upscaleMethodNoDLSS, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethodNoDLSS = enumCount ? enumCount - 1 : 0;
	}
	if (settings.dlssPreset > kDLSSPresetMaxIndex)
		logger::warn("[Upscaling] Loaded dlssPreset {} out of range, clamping to {}", settings.dlssPreset, kDLSSPresetMaxIndex);

	SanitizeUpscalingSettings(settings);

	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	if (!std::isfinite(settings.reflexFPSLimit)) {
		settings.reflexFPSLimit = 60.0f;
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} is not finite, resetting to {}",
			originalReflexFPSLimit,
			settings.reflexFPSLimit);
	}
	const float clampedReflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
	if (clampedReflexFPSLimit != settings.reflexFPSLimit) {
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} out of range, clamping to {}",
			settings.reflexFPSLimit,
			clampedReflexFPSLimit);
	}
	settings.reflexFPSLimit = clampedReflexFPSLimit;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->ReadSetting(setting);
		}
	}
}

void Upscaling::RestoreDefaultSettings()
{
	settings = {};
	SanitizeUpscalingSettings(settings);
}

void Upscaling::DataLoaded()
{
	// Fix screenshots fix from Engine Fixes
	Util::DisableVanillaTAA();

	// The game defaults this to a non-zero value
	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;
}

void Upscaling::Load()
{
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		// Force FXAA off safely
		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
void Upscaling::PostPostLoad()
{
	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates resolution and jitter
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

	// Disables the original dynamic resolution system
	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D, 0x25), REL::NOP5, sizeof(REL::NOP5));

	// Performs upscaling in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));

	// Patches RSSetScissorRect calls to use dynamic resolution
	// This is a PC-specific function hence it was missing
	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Patches facegen texture generation to not use dynamic resolution
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Patches precipitation camera to not use dynamic resolution
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1, 0x2FA));

	// Forces FXAA off
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	if (IsRenderDocUpscalingBlocked())
		return UpscaleMethod::kNONE;

	if (streamline.featureDLSS)
		return (UpscaleMethod)settings.upscaleMethod;
	return (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Creating texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		if (!reactiveMaskTexture) {
			reactiveMaskTexture = std::make_unique<Texture2D>(texDesc);
			reactiveMaskTexture->CreateSRV(srvDesc);
			reactiveMaskTexture->CreateUAV(uavDesc);
		}

		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = std::make_unique<Texture2D>(texDesc);
			transparencyCompositionMaskTexture->CreateSRV(srvDesc);
			transparencyCompositionMaskTexture->CreateUAV(uavDesc);
		}
	}

	// Encoded motion vectors are used by DLSS and by FSR's full-frame path.
	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		if (!motionVectorCopyTexture) {
			auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			D3D11_TEXTURE2D_DESC motionTexDesc{};
			motionVector.texture->GetDesc(&motionTexDesc);

			texDesc.Format = motionTexDesc.Format;
			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			motionVectorCopyTexture = std::make_unique<Texture2D>(motionTexDesc);
			motionVectorCopyTexture->CreateSRV(srvDesc);
			motionVectorCopyTexture->CreateUAV(uavDesc);
		}
	}

	if (a_upscalemethod == UpscaleMethod::kDLSS) {
		// RCAS sharpener texture - matches kMAIN format for HDR sharpening
		if (!sharpenerTexture) {
			main.texture->GetDesc(&texDesc);
			main.SRV->GetDesc(&srvDesc);

			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			sharpenerTexture = std::make_unique<Texture2D>(texDesc);
			sharpenerTexture->CreateSRV(srvDesc);
			sharpenerTexture->CreateUAV(uavDesc);
		}
	}
}

void Upscaling::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Destroying texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	// Clean up D3D11 textures that are no longer needed
	// Only destroy textures when switching away from methods that use them
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		reactiveMaskTexture.reset();
		transparencyCompositionMaskTexture.reset();
	}

	// Encoded motion vectors are used by DLSS and by FSR's full-frame path.
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		motionVectorCopyTexture.reset();
	}

	if (a_upscalemethod != UpscaleMethod::kDLSS) {
		sharpenerTexture.reset();
	}
}

void Upscaling::DestroyAllUpscalingTextureResources()
{
	DestroyUpscalingTextureResources(UpscaleMethod::kNONE);
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;
	static bool previousFSRRuntimePathActive = false;
	static bool previousFSRRuntimeFsr4Configured = false;
	static bool previousFSRRuntimeFsr4Active = false;
	static uint32_t previousQualityMode = ClampQualityModeUInt(settings.qualityMode);
	static uint32_t previousDLSSPreset = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	static D3D11_TEXTURE2D_DESC previousMainDesc{};
	static D3D11_TEXTURE2D_DESC previousMotionVectorDesc{};
	static bool previousTextureSourceDescsValid = false;

	bool frameGenModeCurrent = (settings.frameGenerationMode && d3d12SwapChainActive);
	bool frameGenModeChanged = frameGenModeCurrent != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);
	const uint32_t qualityModeCurrent = ClampQualityModeUInt(settings.qualityMode);
	const uint32_t dlssPresetCurrent = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	const bool qualityModeChanged = previousQualityMode != qualityModeCurrent;
	const bool dlssPresetChanged = previousDLSSPreset != dlssPresetCurrent;
	const bool dlssResourceSettingsChanged =
		(previousUpscaleMode == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kDLSS) &&
		(qualityModeChanged || dlssPresetChanged);
	const bool fsrQualityModeChanged =
		(previousUpscaleMode == UpscaleMethod::kFSR || a_upscalemethod == UpscaleMethod::kFSR) &&
		qualityModeChanged;
	const bool fsrRuntimePathCurrent = IsFSRRuntimePathActive(a_upscalemethod);
	const bool fsrRuntimeFsr4Configured =
		a_upscalemethod == UpscaleMethod::kFSR &&
		settings.fsr4RuntimeEnable &&
		fidelityFX.IsRuntimeFsr4Available();
	const bool fsrRuntimeFsr4Current = IsFSRRuntimeFsr4PathActive(a_upscalemethod);
	const bool compareFSRRuntimePath = a_upscalemethod == UpscaleMethod::kFSR || previousUpscaleMode == UpscaleMethod::kFSR;
	const bool fsrRuntimePathChanged = compareFSRRuntimePath && previousFSRRuntimePathActive != fsrRuntimePathCurrent;
	const bool fsrRuntimeFsr4ConfiguredChanged =
		compareFSRRuntimePath &&
		(fsrRuntimePathCurrent || previousFSRRuntimePathActive) &&
		previousFSRRuntimeFsr4Configured != fsrRuntimeFsr4Configured;
	const bool fsrRuntimeVersionChanged =
		compareFSRRuntimePath &&
		(fsrRuntimePathCurrent || previousFSRRuntimePathActive) &&
		previousFSRRuntimeFsr4Active != fsrRuntimeFsr4Current;

	D3D11_TEXTURE2D_DESC mainDesc{};
	D3D11_TEXTURE2D_DESC motionVectorDesc{};
	bool currentTextureSourceDescsValid = false;
	auto renderer = globals::game::renderer;
	if (renderer) {
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
		currentTextureSourceDescsValid = TryGetTexture2DDesc(main.texture, mainDesc) && TryGetTexture2DDesc(motionVector.texture, motionVectorDesc);
	}

	const bool vendorUpscalerActive = a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR;
	const bool sourceTextureDescChanged =
		currentTextureSourceDescsValid &&
		previousTextureSourceDescsValid &&
		(!TextureDescMatches(previousMainDesc, mainDesc) || !TextureDescMatches(previousMotionVectorDesc, motionVectorDesc));

	if (upscaleModeChanged || frameGenModeChanged || qualityModeChanged || dlssPresetChanged || fsrRuntimePathChanged || fsrRuntimeFsr4ConfiguredChanged || fsrRuntimeVersionChanged) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), Quality: {} -> {}, DLSSPreset: {} -> {}, FrameGen: {} -> {} (d3d12Active={}), FSRRuntimePath: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousQualityMode, qualityModeCurrent, previousDLSSPreset, dlssPresetCurrent, previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive, previousFSRRuntimePathActive, fsrRuntimePathCurrent);

		bool fsrResourcesRecreated = false;

		if (dlssResourceSettingsChanged) {
			streamline.DestroyDLSSResources();
			RequestHistoryReset();
		}

		if (fsrQualityModeChanged) {
			fidelityFX.DestroyFSRResources();
			if (a_upscalemethod == UpscaleMethod::kFSR) {
				fidelityFX.CreateFSRResources();
				fsrResourcesRecreated = true;
			}
			RequestHistoryReset();
		}

		if (upscaleModeChanged) {
			if (previousVendorUpscalerSelected) {
				if (previousUpscaleMode == UpscaleMethod::kDLSS && !dlssResourceSettingsChanged)
					streamline.DestroyDLSSResources();
				else if (previousUpscaleMode == UpscaleMethod::kFSR && !fsrResourcesRecreated)
					fidelityFX.DestroyFSRResources();
			}
			DestroyUpscalingTextureResources(a_upscalemethod);
			if (a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreated)
				fidelityFX.CreateFSRResources();
			RequestHistoryReset();
		}

		if (upscaleModeChanged)
			CreateUpscalingTextureResources(a_upscalemethod);

		if (!upscaleModeChanged && fsrRuntimePathChanged && a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreated) {
			fidelityFX.DestroyFSRResources();
			fidelityFX.CreateFSRResources();
			RequestHistoryReset();
		} else if (!upscaleModeChanged && (fsrRuntimeFsr4ConfiguredChanged || fsrRuntimeVersionChanged) && a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreated) {
			if (fsrRuntimeFsr4ConfiguredChanged || !fidelityFX.IsRuntimeFsr4FailureLatched())
				fidelityFX.ResetRuntimeUpscalerResources(true);
			RequestHistoryReset();
		}

		previousUpscaleMode = a_upscalemethod;
		previousFrameGenMode = (settings.frameGenerationMode && d3d12SwapChainActive);
		previousFSRRuntimePathActive = fsrRuntimePathCurrent;
		previousFSRRuntimeFsr4Configured = fsrRuntimeFsr4Configured;
		previousFSRRuntimeFsr4Active = fsrRuntimeFsr4Current;
		previousQualityMode = qualityModeCurrent;
		previousDLSSPreset = dlssPresetCurrent;
		previousVendorUpscalerSelected = a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR;
	}

	if (vendorUpscalerActive && currentTextureSourceDescsValid) {
		D3D11_TEXTURE2D_DESC expectedMaskDesc = mainDesc;
		expectedMaskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		expectedMaskDesc.Format = DXGI_FORMAT_R8_UNORM;

		D3D11_TEXTURE2D_DESC expectedMotionVectorDesc = motionVectorDesc;

		D3D11_TEXTURE2D_DESC expectedSharpenerDesc = mainDesc;
		expectedSharpenerDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		const bool vendorTextureStateInvalid =
			!TextureMatchesRequirements(reactiveMaskTexture, expectedMaskDesc, true, true) ||
			!TextureMatchesRequirements(transparencyCompositionMaskTexture, expectedMaskDesc, true, true) ||
			!TextureMatchesRequirements(motionVectorCopyTexture, expectedMotionVectorDesc, true, true) ||
			(a_upscalemethod == UpscaleMethod::kDLSS && !TextureMatchesRequirements(sharpenerTexture, expectedSharpenerDesc, true, true));

		if (sourceTextureDescChanged || vendorTextureStateInvalid) {
			logger::debug(
				"[Upscaling] Recreating vendor upscaler textures (sourceDescChanged={}, textureStateInvalid={})",
				sourceTextureDescChanged,
				vendorTextureStateInvalid);
			if (a_upscalemethod == UpscaleMethod::kDLSS) {
				streamline.DestroyDLSSResources();
			} else if (a_upscalemethod == UpscaleMethod::kFSR) {
				fidelityFX.DestroyFSRResources();
				fidelityFX.CreateFSRResources();
			}
			DestroyAllUpscalingTextureResources();
			CreateUpscalingTextureResources(a_upscalemethod);
			RequestHistoryReset();
		}
	}

	if (currentTextureSourceDescsValid) {
		previousMainDesc = mainDesc;
		previousMotionVectorDesc = motionVectorDesc;
	}
	previousTextureSourceDescsValid = currentTextureSourceDescsValid;
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
{
	auto upscaleMethod = GetUpscaleMethod();
	uint methodIndex = (uint)upscaleMethod;

	if (!encodeTexturesCS[methodIndex]) {
		logger::debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

		std::vector<std::pair<const char*, const char*>> defines;

		// Add upscale method define
		switch (upscaleMethod) {
		case UpscaleMethod::kDLSS:
			defines.push_back({ "DLSS", "" });
			break;
		case UpscaleMethod::kFSR:
			defines.push_back({ "FSR", "" });
			break;
		default:
			// No define for NONE or TAA
			break;
		}

		encodeTexturesCS[methodIndex].attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
	}
	return encodeTexturesCS[methodIndex].get();
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetUnderwaterMaskUpscalePS()
{
	if (!underwaterMaskUpscalePS) {
		logger::debug("Compiling UnderwaterMaskPS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		underwaterMaskUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return underwaterMaskUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetCameraMotionVectorsPS()
{
	if (!cameraMotionVectorsPS) {
		logger::debug("Compiling CameraMotionVectorsPS.hlsl");
		cameraMotionVectorsPS.attach((ID3D11PixelShader*)Util::CompileShader(
			L"Data/Shaders/Upscaling/CameraMotionVectorsPS.hlsl",
			{ { "PSHADER", "" } },
			"ps_5_0"));
	}

	return cameraMotionVectorsPS.get();
}

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0"));
	}

	return upscaleVS.get();
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
static float Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

void Upscaling::ConfigureTAA()
{
	auto upscaleMethod = GetUpscaleMethod();
	Util::SetTemporal(upscaleMethod != UpscaleMethod::kNONE);
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	// Cache original TAA values for UI
	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	// Get full screen size
	auto state = globals::state;
	auto graphicsState = globals::game::graphicsState;
	if (!state || !graphicsState) {
		return;
	}

	const int screenWidth = static_cast<int>(graphicsState->screenWidth);
	const int screenHeight = static_cast<int>(graphicsState->screenHeight);

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		float resolutionScaleBase = GetQualityModeResolutionScale(ClampQualityModeUInt(settings.qualityMode));

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);
		// Loading screens cut vendor history every frame; unintegrated jitter only vibrates the image.
		if (IsLoadingMenuContextActive())
			jitter = { 0.0f, 0.0f };

		a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
	} else {
		resolutionScale = { 1.0f, 1.0f };

		jitter.x = -a_viewport->projectionPosScaleX * screenWidth / 2.0f;

		jitter.y = a_viewport->projectionPosScaleY * screenHeight / 2.0f;
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;

	dynamicResolutionWidthRatio = resolutionScale.x;
	dynamicResolutionHeightRatio = resolutionScale.y;

	// Resource creation uses the runtime dynamic-resolution ratios via ConvertToDynamic.
	CheckResources(upscaleMethod);

	// Disable dynamic resolution unless the game explicitly enables it
	runtimeData.dynamicResolutionLock = 1;
}

void Upscaling::SetupResources()
{
	if (IsRenderDocUpscalingBlocked(true)) {
		logger::warn(
			"[Upscaling] Skipping upscaling resource setup because {}.",
			GetRenderDocUpscalingBlockReason());
		return;
	}

	QueryPerformanceFrequency(&qpf);

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)
	depthStencilDesc.StencilEnable = false;                        // Disable stencil testing

	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	// Create jitter offset constant buffer for depth upscaling
	jitterCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<JitterCB>());

	// Create upscaling data constant buffer for encode textures compute shader
	upscalingDataCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<UpscalingDataCB>());
	cameraMotionVectorsCB = std::make_unique<ConstantBuffer>(
		ConstantBufferDesc<CameraMotionVectorsCB>(),
		"Upscaling::CameraMotionVectorsCB");
	menuCameraMVsValid = false;
	menuCameraMVsPreparedFrame = std::numeric_limits<uint32_t>::max();

	// Create blend state for depth upscaling
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	// Create rasterizer state for fullscreen rendering
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;
	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));

	CheckResources(GetUpscaleMethod());

	rcas.Initialize();

	if (d3d12SwapChainActive)
		dx12SwapChain.CreateSharedResources();

	copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));

	// Setup HDR resources only when the HDR Display feature is loaded
	if (globals::features::hdrDisplay.loaded) {
		globals::features::hdrDisplay.SetupResources();
	}
}

void Upscaling::ClearShaderCache()
{
	for (int i = 0; i < 4; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	cameraMotionVectorsPS = nullptr;     // com_ptr automatically releases
	upscaleVS = nullptr;                 // com_ptr automatically releases
	copyDepthToSharedBufferPS = nullptr;
}

void Upscaling::CopySharedD3D12Resources()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Copy Shared D3D12 Resources");
	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	static bool loggedMissingSharedResources = false;
	static bool loggedMissingCopySources = false;

	if (!copyDepthToSharedBufferPS)
		copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));

	const bool hasSharedResources =
		renderer &&
		context &&
		dx12SwapChain.motionVectorBufferShared12 &&
		dx12SwapChain.motionVectorBufferShared12->resource11 &&
		dx12SwapChain.depthBufferShared12 &&
		dx12SwapChain.depthBufferShared12->rtv &&
		copyDepthToSharedBufferPS;
	if (!hasSharedResources) {
		if (!loggedMissingSharedResources) {
			logger::error("[Upscaling] Skipping D3D12 shared-resource copy because frame-generation interop resources are incomplete.");
			loggedMissingSharedResources = true;
		}
		globals::state->EndPerfEvent();
		return;
	}
	loggedMissingSharedResources = false;

	ScopedFullscreenPipelineState restoreState(context);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (!motionVector.texture || !depth.depthSRV) {
		if (!loggedMissingCopySources) {
			logger::error("[Upscaling] Skipping D3D12 shared-resource copy because source depth or motion-vector resources are missing.");
			loggedMissingCopySources = true;
		}
		globals::state->EndPerfEvent();
		return;
	}
	loggedMissingCopySources = false;

	context->CopyResource(dx12SwapChain.motionVectorBufferShared12->resource11.get(), motionVector.texture);

	{
		// Set up viewport for fullscreen rendering
		auto viewportState = globals::game::graphicsState;
		const float screenWidth = static_cast<float>(viewportState ? viewportState->screenWidth : 0);
		const float screenHeight = static_cast<float>(viewportState ? viewportState->screenHeight : 0);

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenWidth;
		viewport.Height = screenHeight;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set up Input Assembler for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

		// Set up rasterizer and blend states
		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		// Set up pixel shader resources
		ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(views), views);

		// Set render target view for pixel shader output
		ID3D11RenderTargetView* rtvs[1] = { dx12SwapChain.depthBufferShared12->rtv.get() };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(copyDepthToSharedBufferPS.get(), nullptr, 0);

		globals::profiler->BeginPass("Upscaling::CopyDepthD3D12");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	// Clean up
	ID3D11ShaderResourceView* views[1] = { nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(views), views);

	globals::state->EndPerfEvent();
}

void UpdateCameraData()
{
	using func_t = decltype(&UpdateCameraData);
	static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
	func();
}

void Upscaling::PostDisplay()
{
	auto viewport = globals::game::graphicsState;

	viewport->projectionPosScaleX = projectionPosScaleX;
	viewport->projectionPosScaleY = projectionPosScaleY;

	auto& runtimeData = viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = 1;
	runtimeData.dynamicResolutionPreviousHeightRatio = 1;
	runtimeData.dynamicResolutionWidthRatio = 1;
	runtimeData.dynamicResolutionHeightRatio = 1;
	runtimeData.dynamicResolutionLock = 1;

	globals::game::renderer->UpdateViewPort(0, 0, 1);
	UpdateCameraData();

	if (d3d12SwapChainActive)
		globals::features::hdrDisplay.SetUIBuffer();

	globals::state->UpdateSharedData(false, false);
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::FrameLimiter()
{
	if (d3d12SwapChainActive) {
		// Use frame latency waitable object if available for better frame pacing
		HANDLE waitableObject = GetFrameLatencyWaitableObject();
		static bool loggedMissingWaitableObject = false;
		static bool loggedWaitFailure = false;
		if (waitableObject && waitableObject != INVALID_HANDLE_VALUE) {
			const DWORD waitResult = WaitForSingleObject(waitableObject, INFINITE);
			if (waitResult != WAIT_OBJECT_0 && !loggedWaitFailure) {
				logger::warn("[Upscaling] Frame-latency wait failed with result {}", waitResult);
				loggedWaitFailure = true;
			}
		} else if (!loggedMissingWaitableObject) {
			logger::warn("[Upscaling] Frame-latency waitable object is unavailable; falling back to timer-based pacing.");
			loggedMissingWaitableObject = true;
		}

		if (settings.frameLimitMode) {
			static constexpr int64_t kNanosecondsPerSecond = 1000000000LL;
			static constexpr double kFrameGenerationRateScale = 0.5;
			const double frameRateScale = ShouldUseFrameGenerationThisFrame() ? kFrameGenerationRateScale : 1.0;
			int64_t targetFrameTimeNS = int64_t(static_cast<double>(kNanosecondsPerSecond) / (refreshRate * frameRateScale));
			int64_t targetFrameTicks = (targetFrameTimeNS * qpf.QuadPart) / kNanosecondsPerSecond;

			static LARGE_INTEGER lastFrame = {};
			LARGE_INTEGER timeNow;
			QueryPerformanceCounter(&timeNow);

			int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
			if (delta < targetFrameTicks) {
				TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
			}
			QueryPerformanceCounter(&lastFrame);
		}
	}
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						// get the refresh rate
						UINT numerator = p.targetInfo.refreshRate.Numerator;
						UINT denominator = p.targetInfo.refreshRate.Denominator;
						return (double)numerator / (double)denominator;
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

bool Upscaling::IsFrameGenerationDx12PathActive() const
{
	return d3d12SwapChainActive;
}

bool Upscaling::IsFrameGenerationActive() const
{
	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && fidelityFX.isFrameGenActive;
}

bool Upscaling::ShouldUseFrameGenerationThisFrame() const
{
	auto* ui = globals::game::ui;
	auto* state = globals::state;
	const bool menuOpen = (ui && ui->GameIsPaused()) || (state && state->IsMainOrLoadingMenuOpen(ui));
	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && (settings.frameGenerationAllowInMenus || !menuOpen);
}

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	// Only consider vendor upscalers (FSR/DLSS) as "active" when the
	// selected method actually produces a downscale. If the renderer is
	// currently running at 1:1 (no downscale), treat upscaling as inactive.
	if (!(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS)) {
		return false;
	}

	return resolutionScale.x < .99f || resolutionScale.y < .99f;
}

bool Upscaling::IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
	       fidelityFX.ShouldUseRuntimeUpscalerForFSR();
}

bool Upscaling::IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
	       fidelityFX.ShouldRequestRuntimeFsr4();
}

void Upscaling::RequestHistoryReset()
{
	historyResetRequested = true;
}

void Upscaling::FillMenuCameraMotionVectors()
{
	menuCameraMVsValid = false;

	auto* renderer = globals::game::renderer;
	auto* context = globals::d3d::context;
	if (!renderer || !context || !cameraMotionVectorsCB)
		return;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto* pixelShader = GetCameraMotionVectorsPS();
	auto* vertexShader = GetUpscaleVS();
	if (!pixelShader || !vertexShader || !motionVector.RTV || !motionVector.texture || !depth.depthSRV)
		return;

	CameraMotionVectorsCB cbData{};
	cbData.curViewProjUnjitteredInverse = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Invert();
	cbData.prevViewProjUnjittered = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered();
	cameraMotionVectorsCB->Update(cbData);

	ScopedFullscreenPipelineState restoreState(context);

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	context->VSSetShader(vertexShader, nullptr, 0);
	context->PSSetShader(pixelShader, nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);

	context->OMSetRenderTargets(0, nullptr, nullptr);
	ID3D11ShaderResourceView* depthSRV = depth.depthSRV;
	context->PSSetShaderResources(0, 1, &depthSRV);

	ID3D11Buffer* constantBuffer = cameraMotionVectorsCB->CB();
	context->PSSetConstantBuffers(1, 1, &constantBuffer);

	context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	context->OMSetDepthStencilState(nullptr, 0);
	context->RSSetState(nullptr);

	D3D11_TEXTURE2D_DESC motionVectorDesc{};
	static_cast<ID3D11Texture2D*>(motionVector.texture)->GetDesc(&motionVectorDesc);
	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(motionVectorDesc.Width);
	viewport.Height = static_cast<float>(motionVectorDesc.Height);
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	ID3D11RenderTargetView* motionVectorRTV = motionVector.RTV;
	context->OMSetRenderTargets(1, &motionVectorRTV, nullptr);
	globals::profiler->BeginPass("Upscaling::MenuCameraMotionVectors");
	context->Draw(3, 0);
	globals::profiler->EndPass();

	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(0, 1, &nullSRV);
	menuCameraMVsValid = true;
}

void Upscaling::PrepareMenuCameraMotionVectors()
{
	auto* state = globals::state;
	if (!state) {
		menuCameraMVsValid = false;
		menuCameraMVsPreparedFrame = std::numeric_limits<uint32_t>::max();
		return;
	}

	const uint32_t frame = state->frameCount;
	if (menuCameraMVsPreparedFrame == frame)
		return;

	menuCameraMVsPreparedFrame = frame;
	menuCameraMVsValid = false;
	if (!IsMainMenuContextActive() || IsLoadingMenuContextActive())
		return;

	FillMenuCameraMotionVectors();
}

bool Upscaling::ShouldResetHistoryThisFrame() const
{
	return historyResetThisFrame;
}

void Upscaling::LatchHistoryResetForCurrentFrame()
{
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	if (historyResetLatchedFrame == frame)
		return;

	historyResetLatchedFrame = frame;
	historyResetThisFrame = historyResetRequested;
	historyResetRequested = false;
}

void Upscaling::UpdateHistoryResetState(UpscaleMethod a_upscaleMethod)
{
	auto state = globals::state;
	if (!state)
		return;

	PrepareMenuCameraMotionVectors();

	auto* ui = globals::game::ui;
	const bool inWorld = state->inWorld;
	const auto* viewport = globals::game::graphicsState;
	const bool inMapMenu = ui ? ui->IsMenuOpen(RE::MapMenu::MENU_NAME) : false;
	const bool mainMenuOpen = IsMainMenuContextActive();
	const bool loadingMenuOpen = IsLoadingMenuContextActive();
	const float2 screenSize{
		static_cast<float>(viewport ? viewport->screenWidth : 0),
		static_cast<float>(viewport ? viewport->screenHeight : 0)
	};
	const uint32_t qualityMode = ClampQualityModeUInt(settings.qualityMode);
	const bool fsrRuntimePathActive = IsFSRRuntimePathActive(a_upscaleMethod);
	const bool fsrRuntimeFsr4Active = IsFSRRuntimeFsr4PathActive(a_upscaleMethod);

	auto cameraCutDetected = []() {
		constexpr float kCameraCutDistanceThreshold = 2500.0f;
		const float cutDistanceSq = kCameraCutDistanceThreshold * kCameraCutDistanceThreshold;
		const auto& currentPos = globals::game::frameBufferCached.GetCameraPosAdjust();
		const auto& previousPos = globals::game::frameBufferCached.GetCameraPreviousPosAdjust();
		const float dx = currentPos.x - previousPos.x;
		const float dy = currentPos.y - previousPos.y;
		const float dz = currentPos.z - previousPos.z;
		return (dx * dx + dy * dy + dz * dz) > cutDistanceSq;
	};

	bool shouldReset = false;
	if (!historyResetTrackingInitialized) {
		shouldReset = true;
		historyResetTrackingInitialized = true;
	} else {
		const bool screenSizeChanged =
			std::abs(screenSize.x - previousHistoryScreenSize.x) > 0.5f ||
			std::abs(screenSize.y - previousHistoryScreenSize.y) > 0.5f;
		const bool scaleChanged =
			std::abs(resolutionScale.x - previousHistoryResolutionScale.x) > 1e-4f ||
			std::abs(resolutionScale.y - previousHistoryResolutionScale.y) > 1e-4f;
		const bool qualityModeChanged = qualityMode != previousHistoryQualityMode;
		const bool worldStateChanged =
			inWorld != previousHistoryInWorld ||
			inMapMenu != previousHistoryInMapMenu;
		const bool methodChanged = a_upscaleMethod != previousHistoryUpscaleMethod;
		const bool fsrRuntimePathChanged = fsrRuntimePathActive != previousHistoryFSRRuntimePathActive;
		const bool fsrRuntimeVersionChanged =
			(fsrRuntimePathActive || previousHistoryFSRRuntimePathActive) &&
			fsrRuntimeFsr4Active != previousHistoryFSRRuntimeFsr4Active;
		const bool longFrameGap = globals::game::deltaTime &&
		                          std::isfinite(*globals::game::deltaTime) &&
		                          *globals::game::deltaTime > 0.20f;
		const bool cameraCut = inWorld && cameraCutDetected();

		shouldReset = screenSizeChanged || scaleChanged || qualityModeChanged || worldStateChanged ||
		              methodChanged || fsrRuntimePathChanged || fsrRuntimeVersionChanged || longFrameGap || cameraCut;
	}

	// Loading screens animate geometry that camera-derived motion vectors cannot represent.
	if (loadingMenuOpen)
		shouldReset = true;
	// The main menu has camera-only motion, so preserve temporal history only after
	// the synthesized reprojection pass completed successfully.
	if (mainMenuOpen && !menuCameraMVsValid)
		shouldReset = true;

	if (shouldReset)
		RequestHistoryReset();

	previousHistoryScreenSize = screenSize;
	previousHistoryResolutionScale = resolutionScale;
	previousHistoryQualityMode = qualityMode;
	previousHistoryInWorld = inWorld;
	previousHistoryInMapMenu = inMapMenu;
	previousHistoryUpscaleMethod = a_upscaleMethod;
	previousHistoryFSRRuntimePathActive = fsrRuntimePathActive;
	previousHistoryFSRRuntimeFsr4Active = fsrRuntimeFsr4Active;
}

/**
 * @brief Retrieves the current frame time for frame generation.
 *
 * Returns the frame time from the D3D12 swap chain if frame generation is active; otherwise, returns 0.
 *
 * @return float The current frame time in seconds, or 0 if frame generation is inactive.
 */
float Upscaling::GetFrameGenerationFrameTime() const
{
	if (!IsFrameGenerationActive())
		return 0.0f;

	// Get the current frame time from D3D12 swapchain
	if (dx12SwapChain.swapChain) {
		// Get frame time from the D3D12 SwapChain
		return GetFrameTime();
	}

	return 0.0f;
}

// Unified interface methods
void Upscaling::LoadUpscalingSDKs()
{
	if (IsRenderDocUpscalingBlocked(true)) {
		if (!renderDocUpscalingBackendSkipLogged) {
			logger::warn(
				"[Upscaling] Skipping Community Shaders Streamline/FidelityFX backend initialization because {}.",
				GetRenderDocUpscalingBlockReason());
			renderDocUpscalingBackendSkipLogged = true;
		}
		return;
	}

	// Initialize upscaling SDK components during plugin startup
	// This ensures all SDKs are available before any D3D device creation
	streamline.LoadInterposer();
	fidelityFX.LoadFFX();
}

HANDLE Upscaling::GetFrameLatencyWaitableObject() const
{
	return dx12SwapChain.GetFrameLatencyWaitableObject();
}

float Upscaling::GetFrameTime() const
{
	return dx12SwapChain.GetFrameTime();
}

// Backend interface methods
bool Upscaling::IsBackendInitialized() const
{
	if (IsRenderDocUpscalingBlocked())
		return false;

	return streamline.initialized;
}

void Upscaling::CheckBackendFeatures(IDXGIAdapter* adapter)
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.CheckFeatures(adapter);
}

void Upscaling::UpgradeBackendInterface(void** ppInterface)
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.slUpgradeInterface(ppInterface);
}

void Upscaling::SetBackendD3DDevice(ID3D11Device* device)
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.slSetD3DDevice(device);
}

void Upscaling::PostBackendDevice()
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.PostDevice();
}

// Module availability methods
bool Upscaling::HasFrameGenModule() const
{
	if (IsRenderDocUpscalingBlocked())
		return false;

	return fidelityFX.featureFSR3FG;
}

// Proxy interface methods
void Upscaling::SetProxyD3D11Device(ID3D11Device* device)
{
	dx12SwapChain.SetD3D11Device(device);
}

void Upscaling::SetProxyD3D11DeviceContext(ID3D11DeviceContext* context)
{
	dx12SwapChain.SetD3D11DeviceContext(context);
}

void Upscaling::CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc)
{
	dx12SwapChain.CreateSwapChain(adapter, swapChainDesc);
}

void Upscaling::CreateProxyInterop()
{
	dx12SwapChain.CreateInterop();
}

IDXGISwapChain* Upscaling::GetProxySwapChain()
{
	return dx12SwapChain.GetSwapChainProxy();
}

Upscaling::BlurResources Upscaling::GetBlurResources() const
{
	if (d3d12SwapChainActive) {
		return dx12SwapChain.GetBlurResources();
	}
	return {};
}

void Upscaling::Upscale()
{
	ZoneScoped;
	auto upscaleMethod = GetUpscaleMethod();

	UpdateHistoryResetState(upscaleMethod);
	LatchHistoryResetForCurrentFrame();

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	const bool requiresEncodedMotionVectors = upscaleMethod == UpscaleMethod::kDLSS || upscaleMethod == UpscaleMethod::kFSR;
	if (requiresEncodedMotionVectors && (!motionVectorCopyTexture || !motionVectorCopyTexture->uav || !motionVectorCopyTexture->resource)) {
		logger::error("[Upscaling] Missing encoded motion-vector resources for method {}", magic_enum::enum_name(upscaleMethod));
		return;
	}

	{
		globals::profiler->BeginPass("Upscaling::EncodeTextures");
		state->BeginPerfEvent("Encode Upscaling Textures");
		TracyD3D11Zone(globals::state->tracyCtx, "Encode Upscaling Textures");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		const auto* viewport = globals::game::graphicsState;
		const float2 displaySize{
			static_cast<float>(viewport ? viewport->screenWidth : 0),
			static_cast<float>(viewport ? viewport->screenHeight : 0)
		};
		auto renderSize = Util::ConvertToDynamic(displaySize);
		uint32_t renderWidth = static_cast<uint32_t>(renderSize.x);
		uint32_t renderHeight = static_cast<uint32_t>(renderSize.y);

		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);
		context->CSSetShader(GetEncodeTexturesCS(), nullptr, 0);

		UpscalingDataCB upscalingData{};
		upscalingData.trueSamplingDim = renderSize;
		upscalingDataCB->Update(upscalingData);
		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

		ID3D11UnorderedAccessView* uavs[3] = {
			reactiveMaskTexture->uav.get(),
			transparencyCompositionMaskTexture->uav.get(),
			requiresEncodedMotionVectors ? motionVectorCopyTexture->uav.get() : nullptr
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->Dispatch((renderWidth + 7) / 8, (renderHeight + 7) / 8, 1);

		ID3D11ShaderResourceView* nullViews[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	{
		globals::profiler->BeginPass("Upscaling::Upscale");
		state->BeginPerfEvent("Upscaling");
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");
		ID3D11Resource* motionVectorResource = requiresEncodedMotionVectors ?
		                                           motionVectorCopyTexture->resource.get() :
		                                           nullptr;

		if (upscaleMethod == UpscaleMethod::kDLSS) {
			streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorResource);
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			fidelityFX.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorResource, settings.sharpnessFSR);
		}

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}
}

void Upscaling::PerformUpscaling()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling");
	Upscale();
	UpscaleDepth();

	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();

	// Disable dynamic resolution past this point
	runtimeData.dynamicResolutionLock = 1;

	// Updates the PerFrame constant buffer so that dynamic resolution settings are disabled
	UpdateCameraData();
}

void Upscaling::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth");
	// Optimization overview:
	// 1) Early validation exits before issuing GPU work.
	// 2) Wide-kernel depth mode uses hysteresis to avoid frequent toggles.
	// 3) Resource copies are skipped for aliased src/dst to reduce copy churn.

	// (1) Early validation exits
	if (!IsUpscalingActive()) {
		return;
	}

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState || !upscaleDepthStencilState) {
		return;
	}

	auto* viewportState = globals::game::graphicsState;
	const float screenWidth = viewportState ? static_cast<float>(viewportState->screenWidth) : 0.0f;
	const float screenHeight = viewportState ? static_cast<float>(viewportState->screenHeight) : 0.0f;
	if (screenWidth <= 0.0f || screenHeight <= 0.0f) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& refractionNormals = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kREFRACTION_NORMALS];
	auto& saoCameraZ = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSAO_CAMERAZ];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];

	if (!depth.texture || !depth.views[0] || !depthCopy.texture || !depthCopy.depthSRV ||
		!refractionNormals.texture || !refractionNormals.textureCopy || !refractionNormals.SRVCopy || !refractionNormals.RTV || !saoCameraZ.RTV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}

	auto* fullscreenVS = GetUpscaleVS();
	auto* depthUpscalePS = GetDepthRefractionUpscalePS();
	auto* underwaterMaskPS = GetUnderwaterMaskUpscalePS();
	if (!fullscreenVS || !depthUpscalePS || !underwaterMaskPS) {
		return;
	}

	state->BeginPerfEvent("Render Target Upscaling");

	// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up vertex shader that generates fullscreen triangle using SV_VertexID
	context->VSSetShader(fullscreenVS, nullptr, 0);

	// Set up viewport for fullscreen rendering
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenWidth;
	viewport.Height = screenHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set rasterizer and blend state
	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	// Set up jitter/depth-kernel constant buffer for upscaling
	JitterCB jitterData;
	jitterData.jitter = jitter;
	// (2) Wide-kernel hysteresis
	{
		constexpr float kEnterWideKernelRatio = 1.55f;
		constexpr float kExitWideKernelRatio = 1.45f;
		const float minScale = std::max(std::min(resolutionScale.x, resolutionScale.y), FLT_EPSILON);
		const float upscaleRatio = 1.0f / minScale;

		if (depthUpscaleUseWideKernel) {
			if (upscaleRatio < kExitWideKernelRatio) {
				depthUpscaleUseWideKernel = false;
			}
		} else {
			if (upscaleRatio > kEnterWideKernelRatio) {
				depthUpscaleUseWideKernel = true;
			}
		}

		jitterData.useWideKernel = depthUpscaleUseWideKernel ? 1.0f : 0.0f;
		jitterData.pad0 = 0.0f;
	}

	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	// (3) Skip aliased copies
	const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
		if (dst && src && dst != src) {
			context->CopyResource(dst, src);
		}
	};

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth Upscale");

		// Sometimes this is not already copied e.g. map menu.
		// Skip alias copies to reduce unnecessary copy churn.
		copyIfNonAliased(depthCopy.texture, depth.texture);

		// Set depth stencil state to write 0x00
		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		copyIfNonAliased(refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV, saoCameraZ.RTV };
		context->OMSetRenderTargets(2, rtvs, depth.views[0]);

		context->PSSetShader(depthUpscalePS, nullptr, 0);
		globals::profiler->BeginPass("Upscaling::DepthUpscale");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Underwater Mask");

		viewport.Width = screenWidth * 0.5f;
		viewport.Height = screenHeight * 0.5f;
		context->RSSetViewports(1, &viewport);

		copyIfNonAliased(underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

		// t0: vanilla mask copy, t1: original depth.
		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		globals::profiler->BeginPass("Upscaling::UnderwaterMaskUpscale");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

	state->EndPerfEvent();
}

void Upscaling::ApplySharpening()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Sharpening");

	if (settings.sharpnessDLSS <= 0.0f)
		return;

	if (!dlssSharpenerOutputValid || !sharpenerTexture)
		return;

	float currentSharpness = (-2.0f * settings.sharpnessDLSS) + 2.0f;
	currentSharpness = exp2(-currentSharpness);

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (!main.UAV || !sharpenerTexture->srv)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	rcas.ApplySharpen(sharpenerTexture->srv.get(), main.UAV, currentSharpness);

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	globals::features::upscaling.PostDisplay();

	// For non-Frame Gen HDR: redirect kFRAMEBUFFER.RTV to UI texture before vanilla UI renders
	// When FG is active, its SetUIBuffer redirects to uiBufferWrapped instead
	// When HDR Display is not loaded, skip entirely so vanilla UI renders to kFRAMEBUFFER
	auto& upscaling = globals::features::upscaling;
	if (!upscaling.d3d12SwapChainActive && globals::features::hdrDisplay.loaded) {
		globals::features::hdrDisplay.SetUIBuffer();
	}

	func(a1);
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();

	if (upscaling.ShouldUseFrameGenerationThisFrame())
		upscaling.CopySharedD3D12Resources();

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA)
		upscaling.PerformUpscaling();

	if (upscaleMethod == UpscaleMethod::kDLSS)
		upscaling.ApplySharpening();

	if (upscaleMethod == UpscaleMethod::kNONE) {
		// Keep vanilla TAA/water stabilization state untouched when no upscaler is active.
		func(a_this, a3, a_target, a_4, a_5);
		return;
	}

	Util::SetTemporal(upscaleMethod == UpscaleMethod::kTAA);

	// Redirect kFRAMEBUFFER to float texture before ISHDR runs so HDR values >1.0 survive
	// When HDR Display is not loaded, ISHDR writes to vanilla kFRAMEBUFFER (SDR path)
	bool hdrLoaded = globals::features::hdrDisplay.loaded;
	if (hdrLoaded)
		globals::features::hdrDisplay.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

	// Restore kFRAMEBUFFER after ISHDR — hdrTexture now has the HDR scene
	if (hdrLoaded)
		globals::features::hdrDisplay.RestoreFramebuffer();

	Util::SetTemporal(false);
}

void Upscaling::SetScissorRect::thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom)
{
	auto viewport = globals::game::graphicsState;
	auto& runtimeData = viewport->GetRuntimeData();

	if (!runtimeData.dynamicResolutionLock) {
		a_left = static_cast<int>(a_left * runtimeData.dynamicResolutionWidthRatio);
		a_right = static_cast<int>(a_right * runtimeData.dynamicResolutionWidthRatio);

		a_top = static_cast<int>(a_top * runtimeData.dynamicResolutionHeightRatio);
		a_bottom = static_cast<int>(a_bottom * runtimeData.dynamicResolutionHeightRatio);
	}

	func(This, a_left, a_top, a_right, a_bottom);
}

void Upscaling::Main_RenderPrecipitation::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}

void Upscaling::BSFaceGenManager_UpdatePendingCustomizationTextures::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}
