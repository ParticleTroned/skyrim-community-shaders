#include "Upscaling.h"

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "DxvkLoader.h"
#include "HDRDisplay.h"
#include "Hooks.h"
#include "State.h"
#include "Upscaling/DxvkInterop.h"
#include "Upscaling/Streamline.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

#define I18N_KEY_PREFIX "feature.upscaling."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	sharpnessFSR,
	reflexEnabled,
	reflexBoost,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	frameGeneration,
	frameGenMethod,
	fgShowOnlyGenerated,
	fgDebugView,
	fgDebugTearLines,
	fgDebugPacingLines,
	hardwareDefaultsApplied);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

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
	globals::state->SetAdapterDescription(adapterDesc.Description, adapterDesc.VendorId);

	auto& upscaling = globals::features::upscaling;

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

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;
	upscaling.lowRefreshRate = refreshRate < 120;
	upscaling.isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	return ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
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
}

void Upscaling::DrawSettings()
{
	auto* streamline = Streamline::GetSingleton();
	const bool dlssAvailable = streamline->IsDLSSSupported();
	const bool xessAvailable = streamline->IsXeSSSupported();

	// Build the upscale method list. Enum order: kNONE=0, kTAA=1, kFSR=2, kDLSS=3, kXeSS=4.
	// We always show None/TAA/FSR; DLSS and XeSS are appended only when available. The combo
	// index maps directly to the UpscaleMethod enum value, so we track the mapping.
	std::vector<std::pair<UpscaleMethod, std::string>> upscaleModes = {
		{ UpscaleMethod::kNONE, T(TKEY("method_none"), "None") },
		{ UpscaleMethod::kTAA, T(TKEY("method_taa"), "TAA") },
		{ UpscaleMethod::kFSR, std::string("AMD FSR 3.1") },
	};
	if (dlssAvailable)
		upscaleModes.push_back({ UpscaleMethod::kDLSS, "NVIDIA DLSS" });
	if (xessAvailable)
		upscaleModes.push_back({ UpscaleMethod::kXeSS, "Intel XeSS" });

	// Find which combo index the current setting maps to.
	int comboIndex = 0;
	for (int i = 0; i < (int)upscaleModes.size(); ++i) {
		if ((uint)upscaleModes[i].first == settings.upscaleMethod)
			comboIndex = i;
	}

	std::vector<const char*> modeLabels;
	for (auto& [_, label] : upscaleModes)
		modeLabels.push_back(label.c_str());
	ImGui::Combo(T(TKEY("method"), "Method"), &comboIndex, modeLabels.data(), (int)modeLabels.size());

	comboIndex = std::clamp(comboIndex, 0, (int)upscaleModes.size() - 1);
	settings.upscaleMethod = (uint)upscaleModes[comboIndex].first;

	auto upscaleMethod = GetUpscaleMethod();

	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kXeSS) {
		const char* upscalePresets[] = {
			T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			T(TKEY("preset_performance"), "Performance"),
			T(TKEY("preset_balanced"), "Balanced"),
			T(TKEY("preset_quality"), "Quality"),
			T(TKEY("preset_native_aa"), "Native AA")
		};

		int presetIndex = 0;
		if (settings.qualityMode <= 4)
			presetIndex = 4 - static_cast<int>(settings.qualityMode);
		presetIndex = std::clamp(presetIndex, 0, 4);

		std::string labelWithScale = std::format("{} ( {:.2f}x )", upscalePresets[presetIndex], (resolutionScale.x + resolutionScale.y) * 0.5f);

		ImGui::SliderInt(T(TKEY("upscale_preset"), "Upscale Preset"), (int*)&settings.qualityMode, 0, 4, labelWithScale.c_str());

		if (upscaleMethod == UpscaleMethod::kFSR)
			ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
	} else if (upscaleMethod == UpscaleMethod::kDLSS) {
		// DLSS quality modes are ordered forward (0=DLAA ... 4=Ultra Performance) to match the
		// sl::DLSSMode mapping in Streamline::EvaluateDLSS — unlike the FSR/XeSS branch above which
		// is reverse-indexed. Without an explicit format string ImGui::SliderInt renders the raw
		// integer ("0".."4"), so build a named label the same way the FSR/XeSS branch does.
		const char* dlssPresets[] = {
			T(TKEY("preset_dlaa"), "DLAA"),                            // qualityMode 0 -> eDLAA
			T(TKEY("preset_quality"), "Quality"),                     // 1 -> eMaxQuality
			T(TKEY("preset_balanced"), "Balanced"),                   // 2 -> eBalanced
			T(TKEY("preset_performance"), "Performance"),             // 3 -> eMaxPerformance
			T(TKEY("preset_ultra_performance"), "Ultra Performance")  // 4 -> eUltraPerformance
		};
		int dlssIndex = std::clamp((int)settings.qualityMode, 0, 4);
		std::string dlssLabel = std::format("{} ( {:.2f}x )", dlssPresets[dlssIndex], (resolutionScale.x + resolutionScale.y) * 0.5f);
		ImGui::SliderInt(T(TKEY("upscale_preset"), "Upscale Preset"), (int*)&settings.qualityMode, 0, 4, dlssLabel.c_str());
	}

	ImGui::Checkbox(T(TKEY("frame_generation"), "Frame Generation"), &settings.frameGeneration);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("frame_generation_tooltip"),
							  "Generates interpolated frames to increase perceived frame rate. "
							  "Works with any upscale method."));
	}

	if (settings.frameGeneration) {
		ImGui::Indent();

		const bool dlssgAvailable = streamline->IsDLSSGSupported();

		std::vector<std::pair<FrameGenMethod, std::string>> fgModes = {
			{ FrameGenMethod::kFSR, "AMD FSR 3" },
		};
		if (dlssgAvailable)
			fgModes.push_back({ FrameGenMethod::kDLSSG, "NVIDIA DLSS-G" });

		if (fgModes.size() > 1) {
			int fgIndex = 0;
			for (int i = 0; i < (int)fgModes.size(); ++i) {
				if ((uint)fgModes[i].first == settings.frameGenMethod)
					fgIndex = i;
			}
			std::vector<const char*> fgLabels;
			for (auto& [_, label] : fgModes)
				fgLabels.push_back(label.c_str());
			ImGui::Combo(T(TKEY("fg_method"), "FG Method"), &fgIndex, fgLabels.data(), (int)fgLabels.size());
			fgIndex = std::clamp(fgIndex, 0, (int)fgModes.size() - 1);
			settings.frameGenMethod = (uint)fgModes[fgIndex].first;
		}

		ImGui::TextDisabled("%s", T(TKEY("frame_generation_debug"), "Frame Generation Debug"));

		ImGui::Checkbox(T(TKEY("fg_show_only_generated"), "Show Only Generated Frames"), &settings.fgShowOnlyGenerated);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("fg_show_only_generated_tooltip"),
								  "Present only the interpolated frames (hides the real frames) so the "
								  "generated output can be inspected in isolation."));

		ImGui::Checkbox(T(TKEY("fg_debug_view"), "Debug View"), &settings.fgDebugView);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("fg_debug_view_tooltip"),
								  "FFX draws debug visualizations (motion vectors, disocclusion, etc.) "
								  "into the generated frames."));

		ImGui::Checkbox(T(TKEY("fg_debug_tear_lines"), "Debug Tear Lines"), &settings.fgDebugTearLines);
		ImGui::Checkbox(T(TKEY("fg_debug_pacing_lines"), "Debug Pacing Lines"), &settings.fgDebugPacingLines);
		ImGui::Unindent();
	}

	// Low-latency section: Reflex only (when available).
	const bool reflexAvailable = streamline->IsReflexSupported();
	if (reflexAvailable) {
		ImGui::SeparatorText(T(TKEY("low_latency"), "Low Latency"));

		// Frame generation dictates Reflex: DLSS-G forces it ON, FSR-FG forces it OFF. In those cases show a
		// disabled checkbox reflecting the forced state; only when no frame generation is active is it the
		// user's toggle. (GetEffectiveReflex encodes the same policy used at the apply sites.)
		const bool fgForcesReflex = IsFrameGenerationActive() &&
		                            (GetFrameGenMethod() == FrameGenMethod::kDLSSG ||
		                             GetFrameGenMethod() == FrameGenMethod::kFSR);
		if (fgForcesReflex) {
			bool effectiveReflex = GetEffectiveReflex();
			ImGui::BeginDisabled();
			ImGui::Checkbox("NVIDIA Reflex", &effectiveReflex);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("%s", GetFrameGenMethod() == FrameGenMethod::kDLSSG ?
			                              T(TKEY("reflex_forced_dlssg"), "(forced on by DLSS-G)") :
			                              T(TKEY("reflex_forced_fsrfg"), "(forced off by FSR frame gen)"));
		} else {
			ImGui::Checkbox("NVIDIA Reflex", &settings.reflexEnabled);

			if (settings.reflexEnabled)
				ImGui::Checkbox(T(TKEY("reflex_boost"), "Boost"), &settings.reflexBoost);
		}
	}
}

void Upscaling::SaveSettings(json& o_json)
{
	o_json = settings;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->WriteSetting(setting);
		}
	}
}

void Upscaling::LoadSettings(json& o_json)
{
	settings = o_json;

	constexpr auto enumCount = 5;  // kNONE, kTAA, kFSR, kDLSS, kXeSS
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount - 1);
		settings.upscaleMethod = enumCount - 1;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(UpscaleMethod::kDLSS))
		settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kFSR);

	constexpr auto fgMethodCount = 2;  // kFSR, kDLSSG
	if (settings.frameGenMethod >= static_cast<uint>(fgMethodCount))
		settings.frameGenMethod = static_cast<uint>(FrameGenMethod::kFSR);

	// Migrate legacy Reflex settings to new reflexEnabled bool.
	if (settings.reflexLowLatencyMode && !settings.reflexEnabled) {
		settings.reflexEnabled = true;
		settings.reflexBoost = settings.reflexLowLatencyBoost;
	}

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
}

void Upscaling::DataLoaded()
{
	Util::DisableVanillaTAA();

	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;
}

void Upscaling::Load()
{
	if (DxvkLoader::IsLoaded()) {
		// Map sl.interposer.dll NOW so DXVK's Vulkan loader (which tries sl.interposer.dll first) aliases
		// it at its imminent first-DXGI VkInstance creation — routing DXVK's whole Vulkan surface through
		// Streamline (full interposition). Must precede DXVK's instance creation; this is that window.
		Streamline::GetSingleton()->PreloadInterposer();
	}

	// Route the game's device creation to DXVK's subfolder-loaded export (set up by
	// DxvkLoader during InstallEarlyHooks), not the inert System32 d3d11 the IAT now
	// resolves to. Fall back to the IAT original only if DXVK failed to load.
	const auto iatOriginal = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = DxvkLoader::IsLoaded() ?
	                                                              reinterpret_cast<uintptr_t>(DxvkLoader::GetD3D11CreateDeviceAndSwapChain()) :
	                                                              iatOriginal;
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::PostPostLoad()
{
	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2));

	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D), REL::NOP5, sizeof(REL::NOP5));

	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));

	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1));

	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

#undef I18N_KEY_PREFIX

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	auto method = (UpscaleMethod)settings.upscaleMethod;
	if (method == UpscaleMethod::kDLSS &&
		!Streamline::GetSingleton()->IsDLSSSupported()) {
		method = (UpscaleMethod)settings.upscaleMethodNoDLSS;
		if (method == UpscaleMethod::kDLSS)
			method = UpscaleMethod::kFSR;
	}
	if (method == UpscaleMethod::kXeSS &&
		!Streamline::GetSingleton()->IsXeSSSupported()) {
		method = UpscaleMethod::kFSR;
	}
	return method;
}

void Upscaling::ApplyHardwareDefaults()
{
	static bool applied = false;
	if (applied)
		return;
	applied = true;

	// Auto-select methods only ONCE per install (persisted flag). After that the user's saved config is
	// authoritative — otherwise an explicit choice (e.g. switching FG to FSR because DLSS-G is unstable
	// on this GPU) would be forced back to the hardware default on every launch.
	if (settings.hardwareDefaultsApplied)
		return;
	settings.hardwareDefaultsApplied = true;

	auto* sl = Streamline::GetSingleton();

	// Upscaling priority: DLSS -> XeSS -> FSR 3
	if (settings.upscaleMethod == (uint)UpscaleMethod::kFSR) {
		if (sl->IsDLSSSupported()) {
			settings.upscaleMethod = (uint)UpscaleMethod::kDLSS;
			settings.upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
			logger::info("[Upscaling] Hardware default: DLSS selected (NVIDIA GPU detected)");
		} else if (sl->IsXeSSSupported()) {
			settings.upscaleMethod = (uint)UpscaleMethod::kXeSS;
			logger::info("[Upscaling] Hardware default: XeSS selected (Intel GPU detected via Streamline)");
		}
	}

	// FG priority: DLSS-G (NVIDIA) -> FSR FG (everything else)
	if (settings.frameGenMethod == (uint)FrameGenMethod::kFSR) {
		if (sl->IsDLSSGSupported()) {
			settings.frameGenMethod = (uint)FrameGenMethod::kDLSSG;
			logger::info("[Upscaling] Hardware default: DLSS-G selected for frame generation");
		}
	}

	// Low-latency: Reflex if available
	if (!settings.reflexEnabled) {
		if (sl->IsReflexSupported()) {
			settings.reflexEnabled = true;
			logger::info("[Upscaling] Hardware default: Reflex low-latency enabled");
		}
	}
}

Upscaling::FrameGenMethod Upscaling::GetFrameGenMethod() const
{
	auto method = (FrameGenMethod)settings.frameGenMethod;
	if (method == FrameGenMethod::kDLSSG &&
		!Streamline::GetSingleton()->IsDLSSGSupported())
		method = FrameGenMethod::kFSR;
	return method;
}

bool Upscaling::IsFrameGenerationActive() const
{
	if (!loaded || !settings.frameGeneration)
		return false;
	auto fgMethod = GetFrameGenMethod();
	if (fgMethod == FrameGenMethod::kDLSSG)
		return Streamline::GetSingleton()->IsDLSSGSupported();
	// kFSR: the sl.fsr plugin owns frame generation via its FFX FrameInterpolationSwapChain.
	// "Active" means FSR is supported and we've delivered the enable (settings.frameGeneration
	// already checked above).
	return Streamline::GetSingleton()->IsFSRSupported();
}

bool Upscaling::GetEffectiveReflex() const
{
	// DLSS-G frame generation REQUIRES Reflex on (it stalls otherwise); FSR frame generation requires it
	// OFF (FFX paces the swapchain itself, Reflex fights it); with no frame generation the user's saved
	// reflexEnabled toggle wins. The saved preference is never mutated — only the effective value changes.
	if (IsFrameGenerationActive()) {
		switch (GetFrameGenMethod()) {
		case FrameGenMethod::kDLSSG:
			return true;
		case FrameGenMethod::kFSR:
			return false;
		default:
			break;
		}
	}
	return settings.reflexEnabled;
}

HRESULT Upscaling::PresentWithFrameGeneration(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags,
	const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_present)
{
	if (!IsFrameGenerationActive())
		return a_present(a_swapChain, a_syncInterval, a_flags);

	auto fgMethod = GetFrameGenMethod();

	// DLSS-G: Streamline interpolates + paces inside its interposed vkQueuePresentKHR hook.
	// SL's present hook requires a resource tag EVERY present or it waits forever on absent
	// inputs — so guarantee one here. Then present normally; DXVK's present reaches SL's
	// interposed present hook, which does the frame generation.
	if (fgMethod == FrameGenMethod::kDLSSG) {
		Streamline::GetSingleton()->EnsureDLSSGPresentTag();
		return a_present(a_swapChain, a_syncInterval, a_flags);
	}

	// FSR FG: the sl.fsr plugin's FFX FrameInterpolationSwapChain (installed via its
	// CreateSwapchainKHR hook) interpolates + double-presents inside the plugin's vkQueuePresentKHR
	// hook. DXVK's present reaches that hook through the interposer — present once and let the
	// plugin run.
	return a_present(a_swapChain, a_syncInterval, a_flags);
}

void Upscaling::CreateUpscaledTexture()
{
	if (upscaledTexture)
		return;

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);

	// Display-resolution texture with SRV+UAV for upscale output.
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	upscaledTexture = new Texture2D(texDesc);
	Util::SetResourceName(upscaledTexture->resource.get(), "Upscaling::UpscaledTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	upscaledTexture->CreateSRV(srvDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	upscaledTexture->CreateUAV(uavDesc);

	logger::info("[Upscaling] Created upscaled texture ({}x{}, format={})",
		texDesc.Width, texDesc.Height, static_cast<int>(texDesc.Format));
}

void Upscaling::DestroyUpscaledTexture()
{
	if (upscaledTexture) {
		delete upscaledTexture;
		upscaledTexture = nullptr;
		logger::debug("[Upscaling] Destroyed upscaled texture");
	}
}

void Upscaling::CreateHudlessTexture()
{
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	hudlessTexture = new Texture2D(texDesc);
	Util::SetResourceName(hudlessTexture->resource.get(), "Upscaling::HudlessTexture");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	hudlessTexture->CreateSRV(srvDesc);

	logger::info("[Upscaling] Created hudless texture ({}x{}, format={})",
		texDesc.Width, texDesc.Height, static_cast<int>(texDesc.Format));
}

void Upscaling::DestroyHudlessTexture()
{
	if (hudlessTexture) {
		delete hudlessTexture;
		hudlessTexture = nullptr;
		logger::debug("[Upscaling] Destroyed hudless texture");
	}
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGeneration = false;
	static auto previousFGMethod = FrameGenMethod::kFSR;

	auto fgMethod = GetFrameGenMethod();
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);
	bool frameGenChanged = (previousFrameGeneration != settings.frameGeneration);
	bool fgMethodChanged = (previousFGMethod != fgMethod);

	if (upscaleModeChanged || frameGenChanged || fgMethodChanged) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), FrameGen: {} -> {}, FG method: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode),
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousFrameGeneration, settings.frameGeneration,
			magic_enum::enum_name(previousFGMethod), magic_enum::enum_name(fgMethod));

		// Upscaled texture lifecycle — shared by all upscale methods that need a separate output texture.
		bool hadUpscale = (previousUpscaleMode == UpscaleMethod::kFSR ||
		                   previousUpscaleMode == UpscaleMethod::kDLSS ||
		                   previousUpscaleMode == UpscaleMethod::kXeSS) &&
		                  previousUpscalingWasActive;
		if (hadUpscale) {
			DestroyUpscaledTexture();
			DestroyHudlessTexture();
		}
		if (a_upscalemethod == UpscaleMethod::kFSR ||
		    a_upscalemethod == UpscaleMethod::kDLSS ||
		    a_upscalemethod == UpscaleMethod::kXeSS) {
			CreateUpscaledTexture();
			CreateHudlessTexture();
		}

		// FG context lifecycle (DLSS-G and FFX FG are mutually exclusive; independent of upscale
		// method). ORDER MATTERS: per the Streamline DLSS-G guide, DLSS-G must be turned OFF
		// and the device drained BEFORE any swapchain manipulation.
		bool needDLSSG = settings.frameGeneration && fgMethod == FrameGenMethod::kDLSSG;
		bool hadDLSSG = previousFrameGeneration && previousFGMethod == FrameGenMethod::kDLSSG;

		// Once DLSS-G frame-gen is active its present proxy wraps the swapchain and is STICKY — it bypasses
		// the Vulkan present hooks, so FSR's cooperative VK_SUBOPTIMAL can never reclaim present. Latch that
		// here; any later switch into FSR-FG forces a DXVK swapchain recreate (below) to evict the proxy.
		if (needDLSSG)
			dlssgProxyMayOwnPresent = true;

		const auto fgDisplaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
		const auto fgRenderSize = Util::ConvertToDynamic(fgDisplaySize);

		// 1. DLSS-G OFF first, then drain the device.
		if (hadDLSSG && !needDLSSG) {
			Streamline::GetSingleton()->SetDLSSGMode(false,
				(uint32_t)fgRenderSize.x, (uint32_t)fgRenderSize.y,
				(uint32_t)fgDisplaySize.x, (uint32_t)fgDisplaySize.y);
			if (auto* dxvk = DxvkInterop::GetSingleton())
				dxvk->WaitDeviceIdle();
		}

		// 2. On select, initialize DLSS-G mode OFF (registers the viewport). The actual ON is driven
		//    per-frame by the gameplay gate in Main_PostProcessing.
		if (needDLSSG && !hadDLSSG) {
			Streamline::GetSingleton()->SetDLSSGMode(false,
				(uint32_t)fgRenderSize.x, (uint32_t)fgRenderSize.y,
				(uint32_t)fgDisplaySize.x, (uint32_t)fgDisplaySize.y);
		}

		// NOTE on the goal items "recreate swapchain on FG-mode change" + "unload DLSS-G when off": a blanket
		// forced recreate on every FG-mode edge deadlocks FSR's controlled FFX unwrap (it has live present/
		// interpolation threads that must drain via its own present->SUBOPTIMAL path), and runtime
		// slSetFeatureLoaded(kFeatureDLSS_G,false) mid-session froze FSR-FG -> DLSS-G (toggling the DLSS-G
		// present proxy fights the swapchain lifecycle). Both need a coordinated interposer-level
		// owner-suppression design (see comshaders-fg-goal-5items memory) — NOT the host-only approach.

		previousUpscaleMode = a_upscalemethod;
		previousFrameGeneration = settings.frameGeneration;
		previousFGMethod = fgMethod;
		previousUpscalingWasActive = IsUpscalingActive();
	}

	// Per-frame robust FSR-FG sync. Push the desired on/off state to the sl.fsr
	// plugin until it sticks — featureFSR + the FG entry points come up a few frames after the first
	// CheckResources, so a one-shot transition silently misses the enable. The plugin self-triggers the
	// DXVK swapchain (un)wrap from its present hook once it has the state, so CS requests no recreate.
	{
		const int desiredFG = (settings.frameGeneration && fgMethod == FrameGenMethod::kFSR) ? 1 : 0;
		// Re-push when the enable state OR any FSR-FG debug toggle changes — the debug overlays (tear/pacing
		// lines, debug view, show-only-generated) are applied per-present from these options, so a runtime
		// toggle has to reach the plugin even when FG is already on.
		const uint32_t fgDebugSig = (settings.fgDebugView ? 1u : 0u) | (settings.fgDebugTearLines ? 2u : 0u) |
		                            (settings.fgDebugPacingLines ? 4u : 0u) | (settings.fgShowOnlyGenerated ? 8u : 0u);
		if (desiredFG != fsrFgAppliedState || fgDebugSig != fsrFgDebugApplied) {
			const bool fgHDR = globals::features::hdrDisplay.loaded && globals::features::hdrDisplay.settings.enableHDR;
			const auto dispSz = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto rendSz = Util::ConvertToDynamic(dispSz);
			if (Streamline::GetSingleton()->SetFSRFrameGen(desiredFG != 0,
					(uint32_t)rendSz.x, (uint32_t)rendSz.y, (uint32_t)dispSz.x, (uint32_t)dispSz.y, fgHDR,
					settings.fgDebugView, settings.fgDebugTearLines, settings.fgDebugPacingLines, settings.fgShowOnlyGenerated)) {
				fsrFgAppliedState = desiredFG;  // delivered; the plugin (un)wraps on its next present
				fsrFgDebugApplied = fgDebugSig;
			}
		}

		// If FSR-FG is now actually enabled (fsrFgAppliedState==1 — the enable can take a few CheckResources
		// to land while featureFSR warms up) AND DLSS-G's sticky proxy might still own present, force the DXVK
		// swapchain recreate to evict it. On the recreate the interposer suppresses sl.dlss_g's create hook and
		// the FFX FG layer wraps the fresh swapchain — the only way to hand present from DLSS-G back to FSR.
		// The latch is sticky across frames, so this fires on the frame the enable finally lands (fixing the
		// race where SetFSRFrameGen hadn't delivered on the transition frame) and covers the indirect
		// DLSS-G -> disabled -> FSR-FG path too. Cleared after firing so steady-state FSR-FG never recreates
		// every frame. Pure disabled -> FSR-FG with no prior DLSS-G leaves the latch false (FSR's own
		// cooperative SUBOPTIMAL wrap handles that, no forced recreate needed).
		if (fsrFgAppliedState == 1 && dlssgProxyMayOwnPresent) {
			Streamline::RequestDxvkSwapchainRecreate();
			dlssgProxyMayOwnPresent = false;
		}
	}
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl", {}, "ps_5_0"));
	}
	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetUnderwaterMaskUpscalePS()
{
	if (!underwaterMaskUpscalePS) {
		logger::debug("Compiling UnderwaterMaskUpscalePS.hlsl");
		underwaterMaskUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl", {}, "ps_5_0"));
	}
	return underwaterMaskUpscalePS.get();
}

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", {}, "vs_5_0"));
	}
	return upscaleVS.get();
}

ID3D11ComputeShader* Upscaling::GetMotionVectorDilateCS()
{
	if (!motionVectorDilateCS) {
		logger::debug("Compiling MotionVectorDilateCS.hlsl");
		motionVectorDilateCS.attach((ID3D11ComputeShader*)Util::CompileShader(
			L"Data/Shaders/Upscaling/MotionVectorDilateCS.hlsl", {}, "cs_5_0"));
	}
	return motionVectorDilateCS.get();
}

ID3D11Resource* Upscaling::DilateMotionVectors(ID3D11Resource* a_mvTexture, ID3D11ShaderResourceView* a_mvSRV,
	uint32_t a_renderWidth, uint32_t a_renderHeight)
{
	auto* context = globals::d3d::context;
	auto* cs = GetMotionVectorDilateCS();
	if (!context || !cs || !a_mvTexture || !a_mvSRV || a_renderWidth == 0 || a_renderHeight == 0)
		return a_mvTexture;

	if (!dilatedMotionVectorTexture) {
		D3D11_TEXTURE2D_DESC desc{};
		static_cast<ID3D11Texture2D*>(a_mvTexture)->GetDesc(&desc);
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MipLevels = 1;
		desc.MiscFlags = 0;
		try {
			dilatedMotionVectorTexture = new Texture2D(desc, "Upscaling::DilatedMotionVector");
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			dilatedMotionVectorTexture->CreateUAV(uavDesc);
		} catch (...) {
			delete dilatedMotionVectorTexture;
			dilatedMotionVectorTexture = nullptr;
			return a_mvTexture;
		}
	}
	if (!mvDilateCB) {
		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = 16;
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		try {
			mvDilateCB = new ConstantBuffer(cbDesc, "Upscaling::MVDilateCB");
		} catch (...) {
			return a_mvTexture;
		}
	}
	if (!dilatedMotionVectorTexture->resource || !dilatedMotionVectorTexture->uav)
		return a_mvTexture;

	struct
	{
		float renderDim[2];
		float pad[2];
	} cbData = { { (float)a_renderWidth, (float)a_renderHeight }, { 0.0f, 0.0f } };
	mvDilateCB->Update(cbData);

	ID3D11ShaderResourceView* srv[] = { a_mvSRV };
	ID3D11UnorderedAccessView* uav[] = { dilatedMotionVectorTexture->uav.get() };
	ID3D11Buffer* cb[] = { mvDilateCB->CB() };
	context->CSSetShader(cs, nullptr, 0);
	context->CSSetShaderResources(0, 1, srv);
	context->CSSetUnorderedAccessViews(0, 1, uav, nullptr);
	context->CSSetConstantBuffers(0, 1, cb);
	context->Dispatch((a_renderWidth + 7) / 8, (a_renderHeight + 7) / 8, 1);

	ID3D11ShaderResourceView* nullSRV[] = { nullptr };
	ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	return dilatedMotionVectorTexture->resource.get();
}

eastl::unique_ptr<Texture2D> Upscaling::CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
	bool copyBindFlags, bool createSRV, bool createUAV, const char* name)
{
	D3D11_TEXTURE2D_DESC srcDesc;
	static_cast<ID3D11Texture2D*>(src)->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = copyBindFlags ? srcDesc.BindFlags : (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

	auto tex = eastl::make_unique<Texture2D>(desc);

	if (name) {
		Util::SetResourceName(tex->resource.get(), name);
	}

	if (createSRV) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = srcDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		tex->CreateSRV(srvDesc);
	}
	if (createUAV) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = srcDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		tex->CreateUAV(uavDesc);
	}
	return tex;
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

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

	CheckResources(upscaleMethod);

	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	auto state = globals::state;
	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kXeSS || upscaleMethod == UpscaleMethod::kDLSS) {
		auto getUpscaleRatio = [](uint qualityMode) -> float {
			switch (qualityMode) {
			case 0:
				return 1.0f;  // Native (Quality)
			case 1:
				return 1.5f;  // Quality
			case 2:
				return 1.7f;  // Balanced
			case 3:
				return 2.0f;  // Performance
			case 4:
				return 3.0f;  // Ultra Performance
			default:
				return 1.5f;
			}
		};
		float resolutionScaleBase = 1.0f / getUpscaleRatio(settings.qualityMode);

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

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

	runtimeData.dynamicResolutionLock = 1;

	// Diagnostic: log the active upscaler's render vs display resolution whenever it changes.
	// Proves the dynamic-resolution downscale is engaged (render < display) for the selected
	// method — for DLSS this is how we confirm the kFSR/kXeSS/kDLSS gate fix took effect.
	static float s_loggedScale = -1.0f;
	static int s_loggedMethod = -1;
	if (std::abs(resolutionScale.x - s_loggedScale) > 0.001f || (int)upscaleMethod != s_loggedMethod) {
		s_loggedScale = resolutionScale.x;
		s_loggedMethod = (int)upscaleMethod;
		logger::info("[Upscaling] active method={} scale={:.3f} render={}x{} display={}x{}",
			(int)upscaleMethod, resolutionScale.x,
			(int)(screenSize.x * resolutionScale.x), (int)(screenSize.y * resolutionScale.y),
			(int)screenSize.x, (int)screenSize.y);
	}
}

void Upscaling::SetupResources()
{
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
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depthStencilDesc.StencilEnable = false;

	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	jitterCB = new ConstantBuffer(ConstantBufferDesc<JitterCB>());

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

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

	if (globals::features::hdrDisplay.loaded) {
		globals::features::hdrDisplay.SetupResources();
	}

	// Bridge to DXVK's own Vulkan device for Streamline interposition.
	// On native D3D11 this is a no-op (IsAvailable() stays false).
	auto* dxvk = DxvkInterop::GetSingleton();
	if (dxvk->Initialize()) {
		VkImage probeImage = VK_NULL_HANDLE;
		VkImageCreateInfo probeInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		if (dxvk->GetVkImage(main.texture, &probeImage, nullptr, &probeInfo) && probeImage != VK_NULL_HANDLE) {
			logger::info("[Upscaling] DXVK texture->VkImage probe OK: main RT VkImage={:#x} ({}x{}, vkFormat={})",
				reinterpret_cast<uintptr_t>(probeImage), probeInfo.extent.width, probeInfo.extent.height, static_cast<int>(probeInfo.format));
		} else {
			logger::warn("[Upscaling] DXVK texture->VkImage probe FAILED");
		}

		dxvk->CreateCommandResources(3);

		// NVIDIA Streamline (DLSS/Reflex) on DXVK's Vulkan device.
		auto* streamline = Streamline::GetSingleton();
		if (streamline->Initialize()) {
			streamline->SetVulkanDevice();
			// Register the DXVK ownership predicate so DXVK knows when the interposer
			// owns presentation (frame-gen swapchain active).
			Streamline::RegisterDxvkOwnershipPredicate();
		}

		// Hardware-adaptive defaults (one-shot, first run only). Applied after feature
		// probing so we know what's available. User settings override on subsequent loads.
		ApplyHardwareDefaults();
	}
}

void Upscaling::ClearShaderCache()
{
	depthRefractionUpscalePS = nullptr;
	underwaterMaskUpscalePS = nullptr;
	upscaleVS = nullptr;
	motionVectorDilateCS = nullptr;
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

	globals::state->UpdateSharedData(false, false);
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
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
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
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

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	if (method != UpscaleMethod::kFSR && method != UpscaleMethod::kXeSS && method != UpscaleMethod::kDLSS) {
		return false;
	}

	return resolutionScale.x < .99f;
}

void Upscaling::Upscale()
{
	ZoneScoped;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	// Restore dev's motion-vector dilation (removed with EncodeTexturesCS): Skyrim leaves foliage/sky/
	// character pixels with zero MV, which ghosts them in the upscalers. Fill those gaps once here and feed
	// the dilated MV (instead of the raw engine target) to whichever upscaler runs below.
	const auto mvDisplaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	const auto mvRenderSize = Util::ConvertToDynamic(mvDisplaySize);
	ID3D11Resource* dilatedMV = DilateMotionVectors(motionVector.texture, motionVector.SRV,
		(uint32_t)mvRenderSize.x, (uint32_t)mvRenderSize.y);

	{
		globals::profiler->BeginPass("Upscaling::Upscale");
		state->BeginPerfEvent("Upscaling");
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		if (GetUpscaleMethod() == UpscaleMethod::kFSR) {
			// FSR3 upscale runs UNDER STREAMLINE via the sl.fsr plugin (slEvaluateFeature(kFeatureFSR)).
			// Upscale into the separate texture, then copy back.
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize);
			if (upscaledTexture && upscaledTexture->resource) {
				Streamline::GetSingleton()->EvaluateFSR(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, dilatedMV,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR,
					jitter.x, jitter.y);
				globals::d3d::context->CopyResource(main.texture, upscaledTexture->resource.get());
			}
		} else if (GetUpscaleMethod() == UpscaleMethod::kDLSS) {
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize);
			// DLSS cannot upscale in place: reading the low-res render sub-rect while writing the
			// full-res output of the SAME texture corrupts it. Upscale into the separate display-res
			// output texture, then copy it back to main.
			if (upscaledTexture && upscaledTexture->resource) {
				Streamline::GetSingleton()->EvaluateDLSS(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, dilatedMV,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR,
					jitter.x, jitter.y);
				globals::d3d::context->CopyResource(main.texture, upscaledTexture->resource.get());
			}
		} else if (GetUpscaleMethod() == UpscaleMethod::kXeSS) {
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			auto& depthTex = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const auto displaySize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto renderSize = Util::ConvertToDynamic(displaySize);
			// XeSS runs UNDER STREAMLINE via the sl.xess plugin (slEvaluateFeature(kFeatureXeSS)).
			// Upscale into the separate texture, then copy back.
			if (upscaledTexture && upscaledTexture->resource) {
				Streamline::GetSingleton()->EvaluateXeSS(
					main.texture, upscaledTexture->resource.get(), depthTex.texture, dilatedMV,
					(uint32_t)renderSize.x, (uint32_t)renderSize.y,
					(uint32_t)displaySize.x, (uint32_t)displaySize.y,
					settings.qualityMode, settings.sharpnessFSR,
					jitter.x, jitter.y);
				globals::d3d::context->CopyResource(main.texture, upscaledTexture->resource.get());
			}
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

	runtimeData.dynamicResolutionLock = 1;

	UpdateCameraData();
}

void Upscaling::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth");

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

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
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

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	context->VSSetShader(fullscreenVS, nullptr, 0);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	JitterCB jitterData;
	jitterData.jitter = jitter;
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

	const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
		if (dst && src && dst != src) {
			context->CopyResource(dst, src);
		}
	};

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth Upscale");

		copyIfNonAliased(depthCopy.texture, depth.texture);

		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		copyIfNonAliased(refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
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

		viewport.Width = screenSize.x * 0.5f;
		viewport.Height = screenSize.y * 0.5f;
		context->RSSetViewports(1, &viewport);

		copyIfNonAliased(underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

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

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	auto& upscaling = globals::features::upscaling;
	upscaling.PostDisplay();

	auto& hdr = globals::features::hdrDisplay;
	if (hdr.loaded)
		hdr.SetUIBuffer();

	func(a1);
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();

	// Reflex/PCL: the game simulation has finished and post-process/upscale render submission
	// happens here — mark the simulation-end / render-submit-start boundary.
	auto* streamline = Streamline::GetSingleton();
	if (upscaling.GetEffectiveReflex()) {
		streamline->SetPCLMarker(Streamline::PclMarker::SimulationEnd);
		streamline->SetPCLMarker(Streamline::PclMarker::RenderSubmitStart);
	}

	if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kXeSS || upscaleMethod == UpscaleMethod::kDLSS)
		upscaling.PerformUpscaling();

	// Capture the hudless scene (post-upscale, pre-UI) for frame generation. Both DLSS-G and
	// FSR FG use this as their HUDLessColor input to avoid re-generating UI artifacts.
	if (upscaling.IsFrameGenerationActive() && upscaling.hudlessTexture && upscaling.hudlessTexture->resource) {
		auto& main = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		globals::d3d::context->CopyResource(upscaling.hudlessTexture->resource.get(), main.texture);
	}

	// Frame-gen resource tagging is independent of which upscaler ran (an upscaler + frame
	// generation can be active together), so this is its own `if`, not an `else if`.
	if (upscaling.IsFrameGenerationActive()) {
		auto fgMethod = upscaling.GetFrameGenMethod();
		auto renderer = globals::game::renderer;
		auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

		auto* ui = globals::game::ui;
		const bool gameplay = ui && !ui->GameIsPaused() && !globals::state->IsMainOrLoadingMenuOpen(ui);
		ID3D11Resource* hudless = (upscaling.hudlessTexture && upscaling.hudlessTexture->resource)
		                              ? upscaling.hudlessTexture->resource.get()
		                              : nullptr;

		if (fgMethod == FrameGenMethod::kDLSSG) {
			// DLSS-G mode is set ON ONCE — the first frame the Streamline swapchain owns presentation —
			// and left ON for the swapchain lifetime. slDLSSGSetOptions is NOT thread-safe vs Streamline's
			// present hook (DLSS-G guide section 6.0); that present hook runs on DXVK's submit thread while this
			// runs on the render thread, so flipping the mode per-frame races the present and triggers
			// VK_ERROR_DEVICE_LOST. SetDLSSGMode is cached, so passing a constant `true` issues the real
			// slDLSSGSetOptions exactly once (the registered-off -> on edge) and no-ops every frame after.
			const auto dispSize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto rendSize = Util::ConvertToDynamic(dispSize);
			Streamline::GetSingleton()->SetDLSSGMode(true,
				(uint32_t)rendSize.x, (uint32_t)rendSize.y, (uint32_t)dispSize.x, (uint32_t)dispSize.y);

			if (gameplay) {
				auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
				Streamline::GetSingleton()->TagDLSSGResources(
					depth.texture, motionVector.texture, hudless,
					(uint32_t)rendSize.x, (uint32_t)rendSize.y,
					(uint32_t)dispSize.x, (uint32_t)dispSize.y);
				Streamline::GetSingleton()->LogDLSSGFrameStats();
			}
		} else if (fgMethod == FrameGenMethod::kFSR && gameplay) {
			// Drive the FSR FG-prepare every gameplay frame from depth + motion vectors, independent of the
			// active upscaler (the sl.fsr plugin runs FrameGenerationPrepare on a color-less FSR evaluate).
			const auto dispSize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
			const auto rendSize = Util::ConvertToDynamic(dispSize);
			auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			Streamline::GetSingleton()->EvaluateFSRFrameGen(
				depth.texture, motionVector.texture, hudless,
				(uint32_t)rendSize.x, (uint32_t)rendSize.y,
				(uint32_t)dispSize.x, (uint32_t)dispSize.y,
				upscaling.jitter.x, upscaling.jitter.y);
		}
	}

	Util::SetTemporal(upscaleMethod == UpscaleMethod::kTAA);

	bool hdrLoaded = globals::features::hdrDisplay.loaded;
	if (hdrLoaded)
		globals::features::hdrDisplay.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

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
