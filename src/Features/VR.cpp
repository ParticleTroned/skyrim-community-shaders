#include "VR.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "RE/B/BSOpenVR.h"
#include "RE/N/NiPoint3.h"
#include "RE/P/PlayerCharacter.h"
#include "DynamicCubemaps.h"
#include "EngineFixes/ShadowmapCascadeRasterizerFix.h"
#include "FoveatedCommon.h"
#include "ScreenSpaceGI.h"
#include "ScreenSpaceShadows.h"
#include "SubsurfaceScattering.h"
#include "Upscaling.h"
#include "WetnessEffects.h"
#include "Wetterness.h"
#include "WaterEffects.h"
#include <openvr.h>

#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/PerfUtils.h"
#include "Utils/UI.h"
#include "Utils/VRUtils.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <array>
#include <cmath>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <unordered_map>
#include <windows.h>

using AttachMode = VR::Settings::OverlayAttachMode;

namespace
{
	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}
}

constexpr int kOverlayWidth = 1920;
constexpr int kOverlayHeight = 1080;
constexpr const char* kMenuOverlayKey = "communityshaders.menu";
constexpr const char* kMenuOverlayName = "Community Shaders Menu";
constexpr const char* kControllerOverlayKey = "communityshaders.menu.controller";
constexpr const char* kControllerOverlayName = "Community Shaders Menu (Controller)";
constexpr float kLegacyDefaultHMDOffsetZ = -0.41f;
constexpr float kDefaultOffsetEpsilon = 0.0001f;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCullingInterior,
	EnableDepthBufferCullingExterior,
	MinOccludeeBoxExtent,
	VRMenuScale,
	VRMenuPositioningMethod,
	attachMode,
	VRMenuAttachController,
	VRMenuOffsetX,
	VRMenuOffsetY,
	VRMenuOffsetZ,
	VRMenuControllerOffsetX,
	VRMenuControllerOffsetY,
	VRMenuControllerOffsetZ,
	mouseDeadzone,
	mouseSpeed,
	dragHighlightColor,
	VRMenuOpenKeys,
	VRMenuCloseKeys,
	VROverlayOpenKeys,
	VROverlayCloseKeys,
	comboTimeout,
	EnableDragToReposition,
	kAutoHideSeconds,
	VRMenuAutoResetDistance,
	EnableWandPointing,
	EnableStereoBlend,
	StereoBlendDepthSigma,
	StereoBlendMaxFactor,
	StereoBlendColorThreshold,
	EnableOuterCascadeCasterBias,
	EnableLightingFoveation,
	EnableLightingFoveationHardCutoff,
	EnableUtilityFoveation,
	EnableUtilityFoveationHardCutoff,
	EnableSSRFoveation,
	EnableSSRFoveationHardCutoff,
	EnableWaterParallaxFoveation,
	EnableWaterParallaxFoveationHardCutoff,
	EnableWetternessFoveation,
	EnableWetternessFoveationHardCutoff,
	EnableDynamicCubemapFoveation,
	EnableDynamicCubemapVisibilityThrottle,
	menuOverlayPath)

//=============================================================================
// FEATURE BASE CLASS OVERRIDES
//=============================================================================

void VR::LoadSettings(json& o_json)
{
	settings = o_json.get<Settings>();
	if (o_json.is_object() &&
	    o_json.contains("VRMenuOffsetZ") &&
	    std::abs(o_json.value("VRMenuOffsetZ", Config::kDefaultHMDOffsetZ) - kLegacyDefaultHMDOffsetZ) < kDefaultOffsetEpsilon) {
		settings.VRMenuOffsetZ = Config::kDefaultHMDOffsetZ;
	}
	// Validate and clamp loaded settings to ensure they're within valid ranges
	settings.ClampToValidRanges();

	if (settings.EnableOuterCascadeCasterBias) {
		ShadowmapRasterizerFix::InstallD3DHooks(globals::d3d::context);
	}
}

void VR::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VR::RestoreDefaultSettings()
{
	settings = {};
}

void VR::SetupResources()
{
	// Detect OpenVR version and compatibility early to avoid CTDs
	DetectOpenVRInfo();

	// Log OpenVR information
	if (openVRInfo.isAvailable) {
		logger::info("OpenVR DLL detected:");
		logger::info("  Path: {}", openVRInfo.dllPath);
		logger::info("  Version: {}", openVRInfo.version);
		logger::info("  Size: {} bytes", openVRInfo.fileSize);
		logger::info("  Modified: {}", openVRInfo.modificationTime);
		logger::info("  Runtime: {}", VRDetection::RuntimeTypeToString(openVRInfo.runtimeType));
		logger::info("  Interfaces: overlay={}, system={}, compositor={}",
			openVRInfo.hasOverlayInterface ? "yes" : "no",
			openVRInfo.hasSystemInterface ? "yes" : "no",
			openVRInfo.hasCompositorInterface ? "yes" : "no");
		logger::info("  Compatible: {}", openVRInfo.isCompatible ? "Yes" : "No");

		if (!openVRInfo.isCompatible) {
			logger::info("Required OpenVR system/compositor interfaces are unavailable.");
			logger::info("Community Shaders VR menus will be disabled for stability");
		}
	} else {
		logger::info("OpenVR DLL not available in current process");
	}
}

void VR::ClearShaderCache()
{
	stereoBlendCS = nullptr;
}

bool VR::AnyScreenSpaceEffectActive()
{
	const auto& ssgi = globals::features::screenSpaceGI;
	const auto& shadows = globals::features::screenSpaceShadows;
	const auto& dynamicCubemaps = globals::features::dynamicCubemaps;
	const auto& sss = globals::features::subsurfaceScattering;

	bool ssgiActive = false;
	if (ssgi.loaded && ssgi.settings.Enabled) {
		const bool isInterior = Util::IsInterior();
		const bool ssgiAOActive = !ssgi.settings.AOInteriorsOnly || isInterior;
		const bool ssgiGIActive = ssgi.IsGIActive() && (!ssgi.settings.ILInteriorsOnly || isInterior);
		ssgiActive = ssgiAOActive || ssgiGIActive;
	}

	const auto* sky = globals::game::sky;
	const bool shadowsActive = shadows.loaded &&
	                           shadows.bendSettings.Enable != 0 &&
	                           sky &&
	                           sky->mode.get() == RE::Sky::Mode::kFull;

	const bool dynamicSSRActive = dynamicCubemaps.IsSSRRuntimeActive();

	return ssgiActive ||
	       shadowsActive ||
	       dynamicSSRActive ||
	       sss.loaded;
}

bool VR::EnsureStereoBlendResources()
{
	if (!globals::d3d::device || !globals::game::renderer)
		return false;

	if (!stereoBlendCS) {
		std::vector<std::pair<const char*, const char*>> defines = { { "VR", "" }, { "FRAMEBUFFER", "" } };
		auto* shader = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VR\\StereoBlendCS.hlsl", defines, "cs_5_0"));
		if (!shader)
			return false;
		stereoBlendCS.attach(shader);
	}

	auto renderer = globals::game::renderer;
	auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!main.texture || !main.UAV)
		return false;

	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);
	if (mainDesc.ArraySize != 1 || mainDesc.SampleDesc.Count != 1)
		return false;

	const bool copyMatches =
		stereoBlendCopyTex &&
		stereoBlendCopyTex->desc.Width == mainDesc.Width &&
		stereoBlendCopyTex->desc.Height == mainDesc.Height &&
		stereoBlendCopyTex->desc.MipLevels == mainDesc.MipLevels &&
		stereoBlendCopyTex->desc.ArraySize == mainDesc.ArraySize &&
		stereoBlendCopyTex->desc.SampleDesc.Count == mainDesc.SampleDesc.Count &&
		stereoBlendCopyTex->desc.SampleDesc.Quality == mainDesc.SampleDesc.Quality &&
		stereoBlendCopyTex->desc.Format == mainDesc.Format;

	if (!copyMatches) {
		D3D11_TEXTURE2D_DESC copyDesc = mainDesc;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;

		stereoBlendCopyTex = eastl::make_unique<Texture2D>(copyDesc, "VR::StereoBlendCopy");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = copyDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		stereoBlendCopyTex->CreateSRV(srvDesc);
	}

	if (!stereoBlendCB)
		stereoBlendCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<StereoBlendCB>(), "VR::StereoBlendCB");

	return stereoBlendCS && stereoBlendCopyTex && stereoBlendCopyTex->srv && stereoBlendCB;
}

void VR::DrawStereoBlend()
{
	if (!loaded)
		return;

	if (!REL::Module::IsVR() || !settings.EnableStereoBlend)
		return;

	if (settings.StereoBlendMaxFactor <= Config::kMinStereoBlendMaxFactor)
		return;

	if (!AnyScreenSpaceEffectActive())
		return;

	if (!EnsureStereoBlendResources())
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	if (!context || !renderer)
		return;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto* depthSRV = Util::GetCurrentSceneDepthSRV();
	if (!main.texture || !main.UAV || !depthSRV)
		return;

	float2 resolution = Util::ConvertToDynamic(globals::state->screenSize);
	if (resolution.x <= 0.0f || resolution.y <= 0.0f)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Stereo Blend");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("VR Stereo Blend");

	// Deferred composite leaves kMAIN bound as a UAV. Unbind before copying it as the source texture.
	ID3D11UnorderedAccessView* nullUavs[3]{ nullptr, nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUavs), nullUavs, nullptr);

	context->CopyResource(stereoBlendCopyTex->resource.get(), main.texture);

	StereoBlendCB cbData{};
	cbData.FrameDim[0] = resolution.x;
	cbData.FrameDim[1] = resolution.y;
	cbData.RcpFrameDim[0] = 1.0f / resolution.x;
	cbData.RcpFrameDim[1] = 1.0f / resolution.y;
	cbData.DepthSigma = settings.StereoBlendDepthSigma;
	cbData.MaxBlendFactor = settings.StereoBlendMaxFactor;
	cbData.ColorDiffThreshold = settings.StereoBlendColorThreshold;
	stereoBlendCB->Update(cbData);

	Util::BindGlobalConstantBuffersForCS(context);

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	auto* cbPtr = stereoBlendCB->CB();
	ID3D11ShaderResourceView* srvs[2]{ stereoBlendCopyTex->srv.get(), depthSRV };
	ID3D11UnorderedAccessView* uavs[1]{ main.UAV };

	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetShader(stereoBlendCS.get(), nullptr, 0);

	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	ID3D11ShaderResourceView* nullSrvs[2]{ nullptr, nullptr };
	uavs[0] = nullptr;
	cbPtr = nullptr;
	context->CSSetShaderResources(0, ARRAYSIZE(nullSrvs), nullSrvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShader(nullptr, nullptr, 0);

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void VR::PostPostLoad()
{
	gDepthBufferCulling = reinterpret_cast<bool*>(REL::Offset(0x1EC6B88).address());
	if (!gDepthBufferCulling) {
		static bool s_defaultDepthBufferCulling = false;  // safe fallback
		gDepthBufferCulling = &s_defaultDepthBufferCulling;
		logger::warn("VR: gDepthBufferCulling address not found - using fallback default (false)");
	}

	gMinOccludeeBoxExtent = reinterpret_cast<float*>(REL::Offset(0x1ED64E8).address());
	if (!gMinOccludeeBoxExtent) {
		static float s_defaultMinOccludeeBoxExtent = 10.0f;
		gMinOccludeeBoxExtent = &s_defaultMinOccludeeBoxExtent;
		logger::warn("VR: gMinOccludeeBoxExtent address not found - using fallback default (10.0)");
	}

	// Patches BSGeometry::CopyTransformAndBounds to copy the model-bound translation across correctly instead of overwriting it with the bounding sphere centre
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xD9) + 0x2, 0x148);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xE5) + 0x2, 0x14C);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xF1) + 0x2, 0x150);
}

void VR::DataLoaded()
{
	// Initialize occlusion culling based on user settings and current interior/exterior state.
	UpdateDepthBufferCulling();

	if (gMinOccludeeBoxExtent) {
		*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
	} else {
		logger::warn("VR::DataLoaded: gMinOccludeeBoxExtent is null, skipping assignment");
	}
}

void VR::EarlyPrepass()
{
	// Apply culling setting each prepass based on current interior/exterior state.
	UpdateDepthBufferCulling();
}

//=============================================================================
// OVERLAY FEATURE OVERRIDES
//=============================================================================

bool VR::ShouldShowAutoHideOverlay() const
{
	return settings.kAutoHideSeconds > 0 &&
	       globals::state &&
	       globals::state->isMainMenuOpen &&
	       globals::menu &&
	       !globals::menu->IsEnabled;
}

bool VR::ShouldUseInSceneOverlay() const
{
	if (!openVRInfo.isCompatible) {
		return false;
	}

	switch (settings.menuOverlayPath) {
	case Settings::MenuOverlayPath::IVROverlay:
		return false;
	case Settings::MenuOverlayPath::InScene:
		return true;
	case Settings::MenuOverlayPath::Auto:
	default:
		return true;
	}
}

bool VR::IsOverlayVisible() const
{
	return openVRInfo.isCompatible && ShouldShowAutoHideOverlay();
}

void VR::DrawOverlay()
{
	if (!openVRInfo.isCompatible)
		return;
	static LARGE_INTEGER overlayShowStart = { 0 };
	static LARGE_INTEGER freq = { 0 };

	bool shouldShow = ShouldShowAutoHideOverlay();

	if (!shouldShow) {
		overlayShowStart.QuadPart = 0;  // Reset timer when overlay is not shown
		return;
	}

	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	if (overlayShowStart.QuadPart == 0) {
		overlayShowStart = now;
	}

	double elapsed = double(now.QuadPart - overlayShowStart.QuadPart) / double(freq.QuadPart);
	const double autoHideSeconds = static_cast<double>(settings.kAutoHideSeconds);
	if (elapsed >= autoHideSeconds) {
		return;
	}
	int secondsLeft = int(std::ceil(autoHideSeconds - elapsed));

	ImGuiIO& io = ImGui::GetIO();
	const float scale = Util::GetUIScale();
	ImVec2 overlaySize(480.0f * scale, 0);  // width, height auto
	ImVec2 overlayPos = ImVec2((io.DisplaySize.x - overlaySize.x) * 0.5f, 80.0f * scale);
	ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f);

	ImGui::Begin("HowToUseOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
	ImGui::Text("How to Use VR Community Shaders Menu:");
	ImGui::Separator();
	ImGui::Text("You must be in the Main Menu or Tween Menu for these key binds to work.");
	ImGui::Spacing();
	ImGui::Text("Open Menu: ");
	Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);
	ImGui::Text("\nClose Menu: ");
	Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);
	ImGui::Spacing();
	ImGui::TextDisabled("(This message will auto-disable in %d seconds)", secondsLeft);
	ImGui::TextDisabled("(You can disable this message in VR settings > Controller Input Instructions)");
	ImGui::End();
}

namespace
{
	void DrawControllerInputInstructions();
	void DrawGeneralVRSettings();
	void DrawMenuSettings();
	void DrawMouseSettings();
	void DrawDragSettings();
	void DrawStereoSettings();
	void DrawStereoSyncSettings();
	void DrawStereoBlendSettings();
	void DrawFoveationSettings();
	void DrawShadowmapRasterizerSettings();
	void DrawKeyBindings();
	void DrawDebugSection();
}

void VR::DrawSettings()
{
	auto menu = globals::menu;
	if (!menu)
		return;
	if (ImGui::BeginTabBar("##VRTabs", ImGuiTabBarFlags_None)) {
		// General Settings Tab
		if (BeginTabItemWithFont("General", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRGeneralFrame", { 0, 0 }, true)) {
				DrawGeneralVRSettings();
				DrawControllerInputInstructions();
				DrawMenuSettings();
				DrawMouseSettings();
				DrawDragSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (BeginTabItemWithFont("Foveation", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRFoveationFrame", { 0, 0 }, true)) {
				DrawFoveationSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Key Bindings Tab
		if (openVRInfo.isCompatible) {
			if (BeginTabItemWithFont("Bindings", Menu::FontRole::Subheading)) {
				if (ImGui::BeginChild("##VRBindingsFrame", { 0, 0 }, true)) {
					DrawKeyBindings();
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
		}

		if (BeginTabItemWithFont("Stereo", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRStereoFrame", { 0, 0 }, true)) {
				DrawStereoSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (BeginTabItemWithFont("Shadowmap Rasterizer", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRShadowmapRasterizerFrame", { 0, 0 }, true)) {
				DrawShadowmapRasterizerSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Debug Tab (existing debug functionality)
		if (BeginTabItemWithFont("Debug", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRDebugFrame", { 0, 0 }, true)) {
				DrawDebugSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	// Combo recording popup
	if (this->isCapturingCombo) {
		ImGui::OpenPopup("Record Combo");
		if (auto popup = Util::CenteredPopupModal("Record Combo")) {
			// Helper function to get button name
			auto GetButtonName = [](uint32_t key) -> const char* {
				switch (key) {
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kTrigger):
					return "Trigger";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip):
					return "Grip";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kTouchpadClick):
					return "Touchpad";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger):
					return "Stick Click";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA):
					return "A/X";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY):
					return "B/Y";
				default:
					return "Unknown";
				}
			};

			ImGui::Text("Recording combo for: %s", this->currentComboName ? this->currentComboName : "Unknown");
			ImGui::Spacing();

			ImGui::TextDisabled("(During recording, any controller's buttons can be used. Requirement is only enforced during use.)");

			ImGui::Spacing();

			// Show countdown timer with color
			double remainingTime = this->comboTimeout - (Util::GetNowSecs() - this->comboStartTime);
			ImVec4 timerColor = remainingTime > 2.0 ? Util::Colors::GetTimerGood() :
			                    remainingTime > 1.0 ? Util::Colors::GetTimerWarning() :
			                                          Util::Colors::GetTimerCritical();
			ImGui::TextColored(timerColor, "Time remaining: %.1f seconds", remainingTime);

			ImGui::Spacing();

			// Show recorded buttons
			if (this->recordedCombo.empty()) {
				ImGui::Text("Press buttons to record combo...");
			} else {
				ImGui::Text("Recorded buttons:");
				// Create a sorted list of decoded buttons for consistent display
				std::vector<ButtonCombo> sortedRecordedCombos;
				for (size_t i = 0; i < this->recordedCombo.size(); ++i) {
					sortedRecordedCombos.push_back(this->recordedCombo[i]);
				}
				std::sort(sortedRecordedCombos.begin(), sortedRecordedCombos.end(),
					[](const ButtonCombo& a, const ButtonCombo& b) {
						return a.GetKey() < b.GetKey();
					});

				Util::DrawButtonCombo(sortedRecordedCombos, false);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Instructions
			ImGui::Text("Press ENTER to accept, ESC to cancel");

			// Handle button recording
			// Check for VR controller button presses - record them (any controller allowed during recording)
			bool buttonPressed = false;
			uint32_t pressedKey = 0;
			ControllerDevice pressedDevice = ControllerDevice::Both;  // Default to Both, will set below

			// Check primary controller buttons
			for (const auto& [keyCode, buttonState] : primaryControllerState.GetActiveButtons()) {
				if (buttonState->isPressed) {
					pressedKey = keyCode;
					buttonPressed = true;
					pressedDevice = ControllerDevice::Primary;
					break;
				}
			}

			// Check secondary controller buttons if primary didn't have any
			if (!buttonPressed) {
				for (const auto& [keyCode, buttonState] : secondaryControllerState.GetActiveButtons()) {
					if (buttonState->isPressed) {
						pressedKey = keyCode;
						buttonPressed = true;
						pressedDevice = ControllerDevice::Secondary;
						break;
					}
				}
			}

			// Record button press
			if (buttonPressed) {
				// Check if this button is already in the combo (avoid duplicates)
				auto it = recordingButtonControllers.find(pressedKey);
				if (it == recordingButtonControllers.end()) {
					// Not yet recorded, add with the current device
					recordingButtonControllers[pressedKey] = pressedDevice;
				} else {
					// Already recorded, if the other controller is now pressed, set to BOTH
					if (it->second != pressedDevice && it->second != ControllerDevice::Both) {
						it->second = ControllerDevice::Both;
					}
				}
				// Update the recordedCombo vector to match the map
				this->recordedCombo.clear();
				for (const auto& [key, device] : recordingButtonControllers) {
					this->recordedCombo.push_back(ButtonCombo(device, key));
				}
			}

			// Handle ENTER key to accept combo
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
				if (!this->recordedCombo.empty()) {
					// Apply the recorded combo to the correct settings vector
					switch (this->currentComboType) {
					case VR::ComboType::MenuOpen:
						settings.VRMenuOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::MenuClose:
						settings.VRMenuCloseKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayOpen:
						settings.VROverlayOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayClose:
						settings.VROverlayCloseKeys = this->recordedCombo;
						break;
					default:
						break;
					}
				}

				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			// Handle ESC key to cancel
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			// Handle timeout - auto-accept if buttons were pressed, auto-cancel if not
			if (remainingTime <= 0.0) {
				if (!this->recordedCombo.empty()) {
					// Auto-accept if buttons were pressed - apply to correct settings vector
					switch (this->currentComboType) {
					case VR::ComboType::MenuOpen:
						settings.VRMenuOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::MenuClose:
						settings.VRMenuCloseKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayOpen:
						settings.VROverlayOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayClose:
						settings.VROverlayCloseKeys = this->recordedCombo;
						break;
					default:
						break;
					}
				}
				// Auto-cancel if no buttons were pressed (do nothing, just close)

				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}
		}
	}
}

namespace
{
	void DrawControllerInputInstructions()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		if (!vr.openVRInfo.isCompatible)
			return;
		if (ImGui::CollapsingHeader("Controller Input Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderInt("Auto-hide Welcome overlay timeout", &settings.kAutoHideSeconds, 0, VR::Config::kMaxAutoHideSeconds,
				settings.kAutoHideSeconds <= 0 ? "Hidden" : "%d seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Set to 0 to hide the overlay, or a positive value to show it for that many seconds");
			}
			ImGui::TextWrapped("Menu (while in the main menu or tween menu):");
			if (ImGui::BeginTable("MenuInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Open Community Shaders Menu:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Close Community Shaders Menu:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);
				ImGui::EndTable();
			}
			ImGui::TextWrapped("Overlay (while in the main menu or tween menu):");
			if (ImGui::BeginTable("OverlayInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Open Overlay:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VROverlayOpenKeys, true);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Close Overlay:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VROverlayCloseKeys, true);
				ImGui::EndTable();
			}
			ImGui::TextWrapped("Menu Controller Input:");
			if (ImGui::BeginTable("ControllerInputTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Trigger (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Left mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Grip (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Right mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Touchpad Click (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Middle mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Stick Click (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Middle mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "A/X (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Enter");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerPrimaryColor(), "B/Y (Primary Controller)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Tab");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerSecondaryColor(), "B/Y (Secondary Controller)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Shift+Tab");
				ImGui::EndTable();
			}
			// Thumbstick instructions
			bool useAttachedControllerForCursor = (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly || settings.attachMode == VR::Settings::OverlayAttachMode::Both);
			if (ImGui::BeginTable("ThumbstickInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				if (useAttachedControllerForCursor) {
					if (settings.VRMenuAttachController == ControllerDevice::Primary) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Mouse movement (attached controller)");
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Scroll");
					} else {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Scroll");
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Mouse movement (attached controller)");
					}
				} else {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Mouse movement (HMD mode)");
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Scroll");
				}
				ImGui::EndTable();
			}
		}
	}

	void DrawGeneralVRSettings()
	{
		auto& vr = globals::features::vr;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("General Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			// Exteriors
			bool exteriorChanged = ImGui::Checkbox("Enable Depth Buffer Culling in Exteriors", &settings.EnableDepthBufferCullingExterior);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Improves performance in exteriors, recommended ON.");
			}

			// Interiors
			bool interiorChanged = ImGui::Checkbox("Enable Depth Buffer Culling in Interiors", &settings.EnableDepthBufferCullingInterior);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Improves performance in interiors, recommended ON.");
			}

		if (exteriorChanged || interiorChanged) {
			vr.UpdateDepthBufferCulling();
		}

			if (ImGui::SliderFloat("Min Occludee Box Extent", &settings.MinOccludeeBoxExtent, 0.0f, 1000.0f, "%.1f")) {
				if (vr.gMinOccludeeBoxExtent) {
					*vr.gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Minimum bounding box dimensions for object occlusion culling. Lower values improve performance but may result in visual artifacts.");
			}

		}
	}

	void DrawMenuSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		if (!vr.openVRInfo.isCompatible)
			return;
		if (ImGui::CollapsingHeader("Menu Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Menu Scale", &settings.VRMenuScale, VR::Config::kMinMenuScale, VR::Config::kMaxMenuScale, "%.2f");
			const char* positioningMethods[] = { "HMD Relative", "Fixed World Position" };
			ImGui::Combo("Menu Positioning Method", &settings.VRMenuPositioningMethod, positioningMethods, IM_ARRAYSIZE(positioningMethods));
			const char* attachModes[] = { "HMD Only", "Controller Only", "Both", "None (Desktop Only)" };
			int attachModeInt = static_cast<int>(settings.attachMode);
			if (ImGui::Combo("Attach Mode", &attachModeInt, attachModes, IM_ARRAYSIZE(attachModes))) {
				settings.attachMode = static_cast<VR::Settings::OverlayAttachMode>(attachModeInt);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Use 'None (Desktop Only)' to hide the VR menu and keep the menu only on desktop.");
			}
			const char* menuOverlayPaths[] = { "Auto", "IVROverlay", "In-scene" };
			int menuOverlayPath = static_cast<int>(settings.menuOverlayPath);
			if (ImGui::Combo("Menu Overlay Path", &menuOverlayPath, menuOverlayPaths, IM_ARRAYSIZE(menuOverlayPaths))) {
				settings.menuOverlayPath = static_cast<VR::Settings::MenuOverlayPath>(menuOverlayPath);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Auto uses the in-scene submit-hook path.");
				ImGui::Text("Use IVROverlay only to force the compositor overlay path for troubleshooting.");
				ImGui::Text("In-scene is rendered into submitted eye textures and may appear in desktop VR mirror views.");
			}

			// Controller-specific settings (only show when controller mode is active)
			if (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
				settings.attachMode == VR::Settings::OverlayAttachMode::Both) {
				const char* attachControllers[] = { "Primary Controller", "Secondary Controller" };
				int attachControllerInt = static_cast<int>(settings.VRMenuAttachController);
				if (ImGui::Combo("Attach to Controller", &attachControllerInt, attachControllers, IM_ARRAYSIZE(attachControllers))) {
					settings.VRMenuAttachController = static_cast<ControllerDevice>(attachControllerInt);
				}

				ImGui::Separator();
				ImGui::Text("Controller Offset Settings");
				ImGui::SliderFloat("Controller Offset X", &settings.VRMenuControllerOffsetX, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Controller Offset Y", &settings.VRMenuControllerOffsetY, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Controller Offset Z", &settings.VRMenuControllerOffsetZ, -2.0f, 2.0f, "%.2f");
			}

			// HMD-specific settings (only show when HMD mode is active)
			if (settings.attachMode == VR::Settings::OverlayAttachMode::HMDOnly ||
				settings.attachMode == VR::Settings::OverlayAttachMode::Both) {
				ImGui::Separator();
				ImGui::Text("HMD Offset Settings");
				ImGui::SliderFloat("HMD Offset X", &settings.VRMenuOffsetX, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("HMD Offset Y", &settings.VRMenuOffsetY, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("HMD Offset Z", &settings.VRMenuOffsetZ, -2.0f, 2.0f, "%.2f");
			}

			// Fixed World Position: show auto reset distance and manual reset button
			if (settings.VRMenuPositioningMethod == 1) {  // 1 = Fixed World Position
				ImGui::Separator();
				ImGui::Text("Fixed World Position Settings");
				ImGui::SliderFloat("Auto Reset Distance (game units)", &settings.VRMenuAutoResetDistance, 100.0f, 5000.0f, "%.0f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("If you move farther than this distance from the menu, it will automatically reset to your HMD position. %s", Util::Units::FormatDistance(settings.VRMenuAutoResetDistance).c_str());
				}
				if (ImGui::Button("Reset Menu to HMD Position")) {
					vr.SetFixedOverlayToCurrentHMD();
				}
			}
		}
	}

	void DrawMouseSettings()
	{
		auto& vr = globals::features::vr;
		if (!vr.openVRInfo.isCompatible)
			return;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("Input Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			// Wand pointing settings
			if (ImGui::Checkbox("Enable Wand Pointing", &settings.EnableWandPointing)) {
				// Reset wand state when toggling
				vr.wandState.isIntersecting = false;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Use controller ray-casting to point at UI elements");
			}
			ImGui::Separator();
			ImGui::Text("Joystick Settings");
			ImGui::SliderFloat("Mouse Deadzone", &settings.mouseDeadzone, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Thumbstick deadzone for joystick cursor movement");
			}
			ImGui::SliderFloat("Mouse Speed", &settings.mouseSpeed, 0.1f, 50.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Speed multiplier for joystick cursor movement");
			}
		}
	}

	void DrawDragSettings()
	{
		auto& vr = globals::features::vr;
		if (!vr.openVRInfo.isCompatible)
			return;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("Drag Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::CollapsingHeader("Drag Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextWrapped("Overlay Positioning (Grip + Drag):");
				ImGui::BulletText("Fixed World Position: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
				ImGui::BulletText("HMD Relative: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
				ImGui::BulletText("Controller Attached: Only the opposite hand can drag the controller overlay");
			}
			ImGui::Checkbox("Enable drag to reposition overlays", &settings.EnableDragToReposition);
			ImGui::BeginDisabled(!settings.EnableDragToReposition);
			ImGui::ColorEdit4("Drag Highlight Color", settings.dragHighlightColor.data());
			ImGui::EndDisabled();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Color used to highlight draggable overlays in VR.");
			}
		}
	}

	void DrawStereoSyncSettings()
	{
		const bool isVR = REL::Module::IsVR();
		auto& screenSpaceShadows = globals::features::screenSpaceShadows;
		auto& screenSpaceGI = globals::features::screenSpaceGI;
		const bool screenSpaceShadowsEnabled = isVR && screenSpaceShadows.loaded && screenSpaceShadows.bendSettings.Enable != 0;
		const bool screenSpaceGIEnabled = isVR && screenSpaceGI.loaded && screenSpaceGI.settings.Enabled;

		if (ImGui::CollapsingHeader("Screen Space Sync", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto drawSyncToggle =
				[](const char* a_label,
					bool& a_enabled,
					bool a_available,
					const char* a_summary,
					const char* a_benefit,
					const char* a_cost,
					const char* a_requirement) {
				auto guard = Util::DisableGuard(!a_available);
				ImGui::Checkbox(a_label, &a_enabled);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted(a_summary);
					ImGui::TextUnformatted(a_benefit);
					ImGui::TextUnformatted(a_cost);
					if (!a_available)
						ImGui::TextUnformatted(a_requirement);
				}
			};

			drawSyncToggle(
				"Sync Screen Space Shadows",
				screenSpaceShadows.enableStereoSync,
				screenSpaceShadowsEnabled,
				"Matches screen-space shadow results between VR eyes.",
				"Reduces left/right shadow mismatch and per-eye noise.",
				"Costs one extra VR compute pass when Screen Space Shadows is active.",
				"Requires VR and active Screen Space Shadows.");
			drawSyncToggle(
				"Sync SSGI",
				screenSpaceGI.settings.EnableStereoSync,
				screenSpaceGIEnabled,
				"Matches SSGI AO/GI results between VR eyes.",
				"Reduces left/right AO and indirect-light mismatch.",
				"Costs one extra compute pass, plus a center pass when FOV SSGI is active.",
				"Requires VR and active SSGI.");

			if (!isVR)
				ImGui::TextDisabled("VR-only.");
		}
	}

	void DrawStereoSettings()
	{
		DrawStereoSyncSettings();
		ImGui::Spacing();
		DrawStereoBlendSettings();
	}

	void DrawStereoBlendSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		const bool screenSpaceEffectActive = VR::AnyScreenSpaceEffectActive();
		const bool blendCanRun = settings.EnableStereoBlend && settings.StereoBlendMaxFactor > VR::Config::kMinStereoBlendMaxFactor && screenSpaceEffectActive;

		if (ImGui::CollapsingHeader("Stereo Blending", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::TextWrapped("Advanced fallback for VR screen-space mismatches. It is default-off and only runs when a supported screen-space effect is active.");
			ImGui::Spacing();

			ImGui::Checkbox("Blend Between Eyes", &settings.EnableStereoBlend);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Depth-aware blend between eyes after deferred composite.");
				ImGui::TextUnformatted("Can hide residual screen-space mismatches.");
				ImGui::TextUnformatted("Costs one full-screen compute pass when active.");
			}

			ImGui::Text("Runtime gate: %s", screenSpaceEffectActive ? "screen-space effect active" : "inactive - no supported screen-space effect");
			ImGui::Text("Current state: %s", blendCanRun ? "will run" : "off");
			ImGui::Spacing();

			ImGui::BeginDisabled(!settings.EnableStereoBlend);

			ImGui::SliderFloat("Max Blend Strength", &settings.StereoBlendMaxFactor, VR::Config::kMinStereoBlendMaxFactor, VR::Config::kMaxStereoBlendMaxFactor, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Limits cross-eye color contribution. Lower is safer; default is %.3f.", VR::Config::kDefaultStereoBlendMaxFactor);
			}

			ImGui::SliderFloat("Depth Match Tolerance", &settings.StereoBlendDepthSigma, VR::Config::kMinStereoBlendDepthSigma, VR::Config::kMaxStereoBlendDepthSigma, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Depth tolerance for cross-eye matches.");
				ImGui::TextUnformatted("Lower values are stricter and reduce halo risk.");
			}

			ImGui::SliderFloat("Color Mismatch Threshold", &settings.StereoBlendColorThreshold, VR::Config::kMinStereoBlendColorThreshold, VR::Config::kMaxStereoBlendColorThreshold, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Minimum luminance mismatch before blending.");
				ImGui::TextUnformatted("Higher values skip already-matching pixels.");
			}

			if (ImGui::Button("Reset Blending Defaults")) {
				settings.StereoBlendDepthSigma = VR::Config::kDefaultStereoBlendDepthSigma;
				settings.StereoBlendMaxFactor = VR::Config::kDefaultStereoBlendMaxFactor;
				settings.StereoBlendColorThreshold = VR::Config::kDefaultStereoBlendColorThreshold;
			}

			ImGui::EndDisabled();

			ImGui::TextDisabled("Performance: runs one full-screen compute pass while enabled.");
			ImGui::Spacing();
			ImGui::TextWrapped("This pass operates on final composite color and cannot reliably attribute pixels to individual screen-space producers.");
		}
	}

	void DrawFoveationSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		auto& upscaling = globals::features::upscaling;
		auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		auto& screenSpaceGI = globals::features::screenSpaceGI;
		auto& screenSpaceShadows = globals::features::screenSpaceShadows;
		auto& waterEffects = globals::features::waterEffects;
		auto& wetnessEffects = globals::features::wetnessEffects;
		auto& wetterness = globals::features::wetterness;
		const bool isVR = REL::Module::IsVR();
		if (!isVR) {
			ImGui::TextDisabled("VR foveation controls are available only in VR.");
			return;
		}

		auto drawSection = [](const char* a_label) {
			ImGui::Spacing();
			MenuFonts::FontRoleGuard headingFont(Menu::FontRole::Subheading);
			ImGui::SeparatorText(a_label);
		};

		auto drawDetailBudget = [](const char* a_label, bool& a_enabled, const char* a_hardCutoffLabel, bool& a_hardCutoff,
								 const char* a_line0, const char* a_line1, const char* a_line2,
								 const char* a_hardLine0, const char* a_hardLine1, const char* a_hardLine2) {
			ImGui::Checkbox(a_label, &a_enabled);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(a_line0);
				ImGui::TextUnformatted(a_line1);
				ImGui::TextUnformatted(a_line2);
			}

			ImGui::BeginDisabled(!a_enabled);
			ImGui::Checkbox(a_hardCutoffLabel, &a_hardCutoff);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(a_hardLine0);
				ImGui::TextUnformatted(a_hardLine1);
				ImGui::TextUnformatted(a_hardLine2);
			}
			ImGui::EndDisabled();
		};

		drawSection("Shared FOV Mask");
		upscaling.DrawFoveatedSettings();

		const auto profile = upscaling.loaded ? upscaling.GetActiveUpscalingFoveatedProfile() : Upscaling::ActiveUpscalingFoveatedProfile{};
		const bool foveatedProfileActive = profile.available && FoveatedCommon::IsActiveCoverage(profile.coverageArea);
		const bool ssrAvailable = dynamicCubemaps.IsSSRRuntimeActive();
		const bool waterParallaxAvailable = waterEffects.loaded;
		const bool wetnessEffectsLoaded = wetnessEffects.loaded;
		const bool wetternessFeatureAvailable = wetterness.loaded && !wetnessEffectsLoaded;
		const bool wetternessRuntimeActive = wetterness.IsRuntimeActive() && !wetnessEffectsLoaded;
		const bool screenSpaceShadowsRuntimeActive = screenSpaceShadows.loaded && screenSpaceShadows.bendSettings.Enable != 0;
		const bool screenSpaceGIFeatureAvailable = screenSpaceGI.loaded;
		const bool screenSpaceGIRuntimeActive = screenSpaceGIFeatureAvailable && screenSpaceGI.settings.Enabled;
		const bool dynamicCubemapsRuntimeActive = dynamicCubemaps.loaded;
		const bool lightingFoveationAvailable = foveatedProfileActive;
		const bool utilityFoveationAvailable = foveatedProfileActive;
		const bool ssrFoveationAvailable = foveatedProfileActive && ssrAvailable;
		const bool waterParallaxFoveationAvailable = foveatedProfileActive && waterParallaxAvailable;
		const bool wetternessFoveationAvailable = foveatedProfileActive && wetternessRuntimeActive;
		const bool dynamicCubemapFoveationAvailable = foveatedProfileActive && dynamicCubemapsRuntimeActive;
		const bool anySharedMaskConsumerEnabled =
			settings.EnableLightingFoveation ||
			settings.EnableUtilityFoveation ||
			(settings.EnableSSRFoveation && ssrAvailable) ||
			(settings.EnableWaterParallaxFoveation && waterParallaxAvailable) ||
			(settings.EnableWetternessFoveation && wetternessRuntimeActive) ||
			(settings.EnableDynamicCubemapFoveation && dynamicCubemapsRuntimeActive) ||
			(settings.EnableDynamicCubemapVisibilityThrottle && dynamicCubemapsRuntimeActive) ||
			(screenSpaceShadowsRuntimeActive && screenSpaceShadows.bendSettings.EnableFoveated != 0) ||
			(screenSpaceGIRuntimeActive && screenSpaceGI.settings.EnableFoveated);

		if (profile.available) {
			ImGui::Text("Mask source: %s", profile.usesPeripheryTAAOuterMask ? "Peripheral TAA outer edge" : "Upscaling FOV center");
			ImGui::Text("Coverage scale: %.2f", profile.coverageArea);
			ImGui::Text("Horizontal scale: %.2f", profile.centerHorizontalScale);
			if (anySharedMaskConsumerEnabled && !foveatedProfileActive)
				ImGui::TextDisabled("Shared-mask consumers require FOV area below 1.00.");
		} else if (anySharedMaskConsumerEnabled) {
			ImGui::TextDisabled("Shared-mask consumers require active foveated upscaling.");
		}

		const bool ssrFoveationEnabled = settings.EnableSSRFoveation && ssrAvailable;
		const bool waterParallaxFoveationEnabled = settings.EnableWaterParallaxFoveation && waterParallaxAvailable;
		const bool wetternessFoveationEnabled = settings.EnableWetternessFoveation && wetternessRuntimeActive;
		const bool dynamicCubemapCadenceEnabled = settings.EnableDynamicCubemapFoveation && dynamicCubemapsRuntimeActive;
		const bool dynamicCubemapVisibilityEnabled = settings.EnableDynamicCubemapVisibilityThrottle && dynamicCubemapsRuntimeActive;
		const bool screenSpaceShadowsEnabled = screenSpaceShadowsRuntimeActive && screenSpaceShadows.bendSettings.EnableFoveated != 0;
		const bool screenSpaceGIEnabled = screenSpaceGIRuntimeActive && screenSpaceGI.settings.EnableFoveated;

		drawSection("Screen-Space Effects");
		ImGui::BeginDisabled(!foveatedProfileActive || !screenSpaceShadowsRuntimeActive);
		screenSpaceShadows.DrawFoveationSettings();
		ImGui::EndDisabled();
		if (!screenSpaceShadows.loaded)
			ImGui::TextDisabled("Screen Space Shadows foveation requires Screen Space Shadows.");
		else if (screenSpaceShadows.bendSettings.Enable == 0)
			ImGui::TextDisabled("Screen Space Shadows foveation requires Screen Space Shadows to be enabled.");
		ImGui::Separator();
		ImGui::BeginDisabled(!foveatedProfileActive || !screenSpaceGIRuntimeActive);
		screenSpaceGI.DrawFoveationSettings();
		ImGui::EndDisabled();
		if (!foveatedProfileActive)
			ImGui::TextDisabled("Screen-space foveation requires active foveated upscaling with FOV area below 1.00.");
		if (!screenSpaceGIFeatureAvailable)
			ImGui::TextDisabled("FOV SSGI requires Screen Space GI.");
		else if (!screenSpaceGI.settings.Enabled)
			ImGui::TextDisabled("FOV SSGI requires Screen Space GI to be enabled.");

		drawSection("Shader Detail Budgets");
		{
			struct FoveationToggleRef
			{
				bool available = false;
				bool* enabled = nullptr;
			};

			struct FoveationFeatureCounts
			{
				int available = 0;
				int enabled = 0;
			};

			const std::array<FoveationToggleRef, 7> boolFoveationToggles{
				FoveationToggleRef{ lightingFoveationAvailable, &settings.EnableLightingFoveation },
				FoveationToggleRef{ utilityFoveationAvailable, &settings.EnableUtilityFoveation },
				FoveationToggleRef{ ssrFoveationAvailable, &settings.EnableSSRFoveation },
				FoveationToggleRef{ waterParallaxFoveationAvailable, &settings.EnableWaterParallaxFoveation },
				FoveationToggleRef{ wetternessFoveationAvailable, &settings.EnableWetternessFoveation },
				FoveationToggleRef{ dynamicCubemapFoveationAvailable, &settings.EnableDynamicCubemapFoveation },
				FoveationToggleRef{ dynamicCubemapFoveationAvailable, &settings.EnableDynamicCubemapVisibilityThrottle },
			};

			auto getFoveationFeatureCounts = [&]() {
				FoveationFeatureCounts counts{};
				auto countFoveationFeature = [&](bool a_available, bool a_enabled) {
					if (!a_available)
						return;
					++counts.available;
					if (a_enabled)
						++counts.enabled;
				};

				for (const auto& toggle : boolFoveationToggles) {
					countFoveationFeature(toggle.available, toggle.enabled && *toggle.enabled);
				}
				return counts;
			};

			FoveationFeatureCounts foveationFeatureCounts = getFoveationFeatureCounts();

			const bool anyFoveationFeatureAvailable = foveationFeatureCounts.available > 0;
			bool allAvailableFoveationFeaturesEnabled =
				anyFoveationFeatureAvailable &&
				foveationFeatureCounts.enabled == foveationFeatureCounts.available;

			{
				auto masterGuard = Util::DisableGuard(!foveatedProfileActive || !anyFoveationFeatureAvailable);
				Util::BlueFrameStyleWrapper blueFrameStyle(true);
				if (ImGui::Checkbox("Shader FOV", &allAvailableFoveationFeaturesEnabled)) {
					const bool enableFoveationFeatures = allAvailableFoveationFeaturesEnabled;
					auto applyMasterToggle = [&](const FoveationToggleRef& a_toggle) {
						if (!a_toggle.enabled)
							return;
						if (enableFoveationFeatures) {
							if (a_toggle.available)
								*a_toggle.enabled = true;
						} else {
							*a_toggle.enabled = false;
						}
					};

					for (const auto& toggle : boolFoveationToggles) {
						applyMasterToggle(toggle);
					}

					foveationFeatureCounts = getFoveationFeatureCounts();
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Master switch for eligible shader/detail FOV features and Dynamic Cubemap FOV throttles.");
				ImGui::TextUnformatted("Screen Space Shadows and FOV SSGI stay controlled separately.");
				ImGui::TextUnformatted("Does not change Upscaling FOV, mask visualization, mask geometry, Peripheral TAA, or hard-cutoff sub-modes.");
				ImGui::TextUnformatted("Turning on enables only available features; turning off clears those toggles.");
			}
			ImGui::SameLine();
			if (anyFoveationFeatureAvailable)
				ImGui::TextDisabled("%d/%d available enabled", foveationFeatureCounts.enabled, foveationFeatureCounts.available);
			else
				ImGui::TextDisabled("No Shader FOV features available");
		}
		ImGui::Separator();
		if (!foveatedProfileActive)
			ImGui::TextDisabled("Lighting, Utility, SSR, Water, and Wetterness shader budgets require active foveated upscaling with FOV area below 1.00.");

		ImGui::BeginDisabled(!foveatedProfileActive);
		drawDetailBudget(
			"Lighting Auxiliary Detail",
			settings.EnableLightingFoveation,
			"Hard Cutoff Outside FOV##Lighting",
			settings.EnableLightingFoveationHardCutoff,
			"Uses the active shared FOV mask to reduce expensive auxiliary detail in the Lighting shader.",
			"The full visible FOV uses the normal Upscaling FOV mask, or the outside edge of Peripheral TAA when FOV + Peripheral TAA is enabled.",
			"Base diffuse lighting, albedo, normal, and shadowmask sampling remain unchanged.",
			"Uses a binary mask for Lighting shader auxiliary detail.",
			"Inside the FOV mask gets full auxiliary detail; outside the mask skips those optional paths instead of feathering quality.",
			"This can save more work, but can make detail transitions more visible near the mask edge.");
		ImGui::Separator();

		drawDetailBudget(
			"Utility Shadowmask Filtering",
			settings.EnableUtilityFoveation,
			"Hard Cutoff Outside FOV##Utility",
			settings.EnableUtilityFoveationHardCutoff,
			"Uses the active shared FOV mask to reduce expensive Utility shader shadowmask filtering.",
			"The full visible FOV uses the normal Upscaling FOV mask, or the outside edge of Peripheral TAA when FOV + Peripheral TAA is enabled.",
			"Outside the mask, high-cost PCF filtering fades toward a single shadow comparison while keeping valid shadowmask output.",
			"Uses a binary mask for Utility shader shadowmask filtering.",
			"Inside the FOV mask keeps full shadowmask filtering; outside the mask skips PCF filtering and uses one shadow comparison.",
			"This can save more work, but can make shadow filter transitions more visible near the mask edge.");
		ImGui::Separator();

		ImGui::BeginDisabled(!ssrAvailable);
		drawDetailBudget(
			"SSR Raymarch",
			settings.EnableSSRFoveation,
			"Hard Cutoff Outside FOV##SSR",
			settings.EnableSSRFoveationHardCutoff,
			"Uses the active shared FOV mask to reduce expensive screen-space reflection raymarching in VR.",
			"Inside the FOV mask keeps full SSR. The feather band uses fewer raymarch iterations and fades SSR alpha so existing reflection fallback shows through.",
			"Outside the mask, SSR raymarching is skipped and downstream water/reflection fallback remains responsible for the result.",
			"Uses a binary mask for SSR raymarching.",
			"Inside the FOV mask keeps full SSR; outside the mask skips SSR raymarching completely and relies on fallback reflections.",
			"This assumes only the foveated area is visibly important and can make reflective water transitions more visible near the mask edge.");
		ImGui::EndDisabled();
		if (!ssrAvailable) {
			ImGui::TextDisabled("SSR foveation requires Dynamic Cubemaps SSR.");
			if (dynamicCubemaps.loaded && dynamicCubemaps.settings.EnabledSSR != 0 && !dynamicCubemaps.enabledAtBoot)
				ImGui::TextDisabled("VR SSR must be enabled before startup.");
		}
		ImGui::Separator();

		ImGui::BeginDisabled(!waterParallaxAvailable);
		drawDetailBudget(
			"Water Parallax Detail",
			settings.EnableWaterParallaxFoveation,
			"Hard Cutoff Outside FOV##WaterParallax",
			settings.EnableWaterParallaxFoveationHardCutoff,
			"Uses the active shared FOV mask to reduce expensive Water Effects parallax loops in VR.",
			"Inside the FOV mask keeps full water parallax. The feather band uses fewer parallax steps and fades offsets toward base water normals.",
			"Base water color, normal sampling, reflection, refraction, and Wetterness ripple paths remain active.",
			"Uses a binary mask for Water Effects parallax detail.",
			"Inside the FOV mask keeps full parallax; outside the mask skips parallax offsets and uses base water normal UVs.",
			"This assumes only the foveated area is visibly important and can make water microdetail transitions more visible near the mask edge.");
		ImGui::EndDisabled();
		if (!waterParallaxAvailable)
			ImGui::TextDisabled("Water parallax foveation requires Water Effects.");
		ImGui::Separator();

		ImGui::BeginDisabled(!wetternessRuntimeActive);
		drawDetailBudget(
			"Wetterness Dynamic Detail",
			settings.EnableWetternessFoveation,
			"Hard Cutoff Outside FOV##Wetterness",
			settings.EnableWetternessFoveationHardCutoff,
			"Uses the active shared FOV mask to reduce Wetterness raindrop/ripple microdetail and direct wet specular in VR.",
			"Base wetness, puddles, darkening, shore wetness, broad reflection, and cubemap wet reflectance remain active everywhere.",
			"This applies only to Wetterness and does not affect legacy Wetness Effects.",
			"Uses a binary mask for Wetterness dynamic detail.",
			"Inside the FOV mask keeps full dynamic wet detail; outside the mask skips raindrop/ripple work and direct wet specular contribution.",
			"This assumes only the foveated area is visibly important and can make wet microdetail transitions more visible near the mask edge.");
		ImGui::EndDisabled();
		if (wetnessEffectsLoaded)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation is only available with Wetterness. Wetness Effects is not supported.");
		else if (!wetternessFeatureAvailable)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness.");
		else if (!wetternessRuntimeActive)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness to be enabled.");
		ImGui::EndDisabled();

		drawSection("Dynamic Cubemaps");
		ImGui::BeginDisabled(!foveatedProfileActive || !dynamicCubemapsRuntimeActive);
		ImGui::Checkbox("Dynamic Cubemap Cadence", &settings.EnableDynamicCubemapFoveation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses the active shared FOV mask as the VR-only foveation gate for Dynamic Cubemap update cadence.");
			ImGui::TextUnformatted("When active, cubemap capture, inference, irradiance, and BC6H compression are spread across more frames when reflections are low priority.");
			ImGui::TextUnformatted("Additive with Low-Visibility Cubemap Throttle; this is not a Lighting/Utility-style feathered vs hard-cutoff pair.");
		}

		ImGui::Checkbox("Low-Visibility Cubemap Throttle", &settings.EnableDynamicCubemapVisibilityThrottle);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Adds visibility-based reduction for Dynamic Cubemaps when the secondary reflection path is not currently useful.");
			ImGui::TextUnformatted("When no real reflection pass is active, exterior fake-reflection cubemap work is not forced and any pending secondary reflection task is skipped.");
			ImGui::TextUnformatted("It can be enabled independently, but enabling both cubemap toggles applies both cadence throttling and eligible low-value reflection-task skipping.");
			ImGui::TextUnformatted("Additive with Dynamic Cubemap Cadence; it is not a replacement mode or a hard-cutoff option.");
		}
		ImGui::EndDisabled();
		if (!foveatedProfileActive)
			ImGui::TextDisabled("Dynamic Cubemap foveation requires active foveated upscaling with FOV area below 1.00.");
		if (!dynamicCubemapsRuntimeActive)
			ImGui::TextDisabled("Dynamic Cubemap foveation requires Dynamic Cubemaps.");

		ImGui::Spacing();
		ImGui::SetNextItemOpen(false, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Status##VRFoveationStatus")) {
			const bool statusLightingActive = settings.EnableLightingFoveation && foveatedProfileActive;
			const bool statusUtilityActive = settings.EnableUtilityFoveation && foveatedProfileActive;
			const bool statusSSRActive = ssrFoveationEnabled && foveatedProfileActive;
			const bool statusWaterParallaxActive = waterParallaxFoveationEnabled && foveatedProfileActive;
			const bool statusWetternessActive = wetternessFoveationEnabled && foveatedProfileActive;
			const bool statusCubemapCadenceActive = dynamicCubemapCadenceEnabled && foveatedProfileActive;
			const bool statusCubemapVisibilityActive = dynamicCubemapVisibilityEnabled && foveatedProfileActive;
			const bool statusScreenSpaceShadowsEnabled = screenSpaceShadowsEnabled;
			const bool statusSsgiFoveatedEnabled = screenSpaceGIEnabled;
			const auto statusLightingMode = FoveatedCommon::GetDetailMode(settings.EnableLightingFoveation, settings.EnableLightingFoveationHardCutoff);
			const auto statusUtilityMode = FoveatedCommon::GetDetailMode(settings.EnableUtilityFoveation, settings.EnableUtilityFoveationHardCutoff);
			const auto statusSSRMode = FoveatedCommon::GetDetailMode(ssrFoveationEnabled, settings.EnableSSRFoveationHardCutoff);
			const auto statusWaterParallaxMode = FoveatedCommon::GetDetailMode(waterParallaxFoveationEnabled, settings.EnableWaterParallaxFoveationHardCutoff);
			const auto statusWetternessMode = FoveatedCommon::GetDetailMode(wetternessFoveationEnabled, settings.EnableWetternessFoveationHardCutoff);
			const bool statusAnyCubemapFoveationEnabled =
				settings.EnableDynamicCubemapFoveation ||
				settings.EnableDynamicCubemapVisibilityThrottle;
			ImGui::Text("Shared FOV mask: %s", foveatedProfileActive ? "active" : profile.available ? "full coverage" : "unavailable");
			ImGui::Text("Lighting auxiliary detail: %s (%s)", statusLightingActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusLightingMode));
			ImGui::Text("Utility shadowmask filtering: %s (%s)", statusUtilityActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusUtilityMode));
			ImGui::Text("SSR raymarch: %s (%s)", statusSSRActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusSSRMode));
			ImGui::Text("Water parallax detail: %s (%s)", statusWaterParallaxActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusWaterParallaxMode));
			ImGui::Text("Wetterness dynamic detail: %s (%s)", statusWetternessActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusWetternessMode));
			ImGui::Text("Screen Space Shadows: %s", statusScreenSpaceShadowsEnabled && foveatedProfileActive ? "active" : "inactive");
			ImGui::Text("Screen Space GI: %s", statusSsgiFoveatedEnabled && foveatedProfileActive ? "active" : "inactive");
			ImGui::Text("Dynamic cubemap cadence: %s", statusCubemapCadenceActive ? "active" : "inactive");
			ImGui::Text("Dynamic cubemap visibility throttle: %s", statusCubemapVisibilityActive ? "active" : "inactive");
			if (statusAnyCubemapFoveationEnabled && !dynamicCubemaps.loaded)
				ImGui::TextDisabled("Dynamic Cubemap foveation requires Dynamic Cubemaps.");
			if (settings.EnableSSRFoveation && !ssrAvailable) {
				ImGui::TextDisabled("SSR foveation requires Dynamic Cubemaps SSR.");
				if (dynamicCubemaps.loaded && dynamicCubemaps.settings.EnabledSSR != 0 && !dynamicCubemaps.enabledAtBoot)
					ImGui::TextDisabled("VR SSR must be enabled before startup.");
			}
			if (settings.EnableWaterParallaxFoveation && !waterParallaxAvailable)
				ImGui::TextDisabled("Water parallax foveation requires Water Effects.");
			if (screenSpaceShadows.bendSettings.EnableFoveated != 0 && !screenSpaceShadowsRuntimeActive)
				ImGui::TextDisabled("Screen Space Shadows foveation requires active Screen Space Shadows.");
			if (screenSpaceGI.settings.EnableFoveated && !screenSpaceGIRuntimeActive)
				ImGui::TextDisabled("FOV SSGI requires active Screen Space GI.");
			if (settings.EnableWetternessFoveation && wetnessEffectsLoaded)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation is only available with Wetterness. Wetness Effects is not supported.");
			else if (settings.EnableWetternessFoveation && !wetternessFeatureAvailable)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness.");
			else if (settings.EnableWetternessFoveation && !wetternessRuntimeActive)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness to be enabled.");
		}
	}

	void DrawShadowmapRasterizerSettings()
	{
		auto& settings = globals::features::vr.settings;

		if (ImGui::Checkbox("Apply Outer Cascade Caster Bias", &settings.EnableOuterCascadeCasterBias) &&
			settings.EnableOuterCascadeCasterBias) {
			ShadowmapRasterizerFix::InstallD3DHooks(globals::d3d::context);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Default off. Turn this on only if distant or outer-cascade surfaces show shadow acne.\n"
				"It applies a small caster-side rasterizer bias to the outer cascade only.\n"
				"Leave it off if shadows look stable, or if enabling it causes detached shadows, pulsing, or flicker.");
			ImGui::Text(
				"Values: DepthBias %d, Clamp %.4f, SlopeScaledDepthBias %.2f.",
				ShadowmapRasterizerFix::vrOuterCascadeDepthBias,
				ShadowmapRasterizerFix::vrOuterCascadeDepthBiasClamp,
				ShadowmapRasterizerFix::vrOuterCascadeSlopeScaleBias);
		}
	}

	void DrawKeyBindings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;

		// Combo Settings
		if (ImGui::CollapsingHeader("Combo Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Combo Timeout", &settings.comboTimeout, 1.0f, 10.0f, "%.1f seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Time limit for recording button combinations.");
			}
		}
		ImGui::Separator();
		// Combo box for selecting which combo to record
		const char* comboTypes[] = {
			"Open Community Shaders Menu",
			"Close Community Shaders Menu",
			"Open VR Overlay",
			"Close VR Overlay"
		};
		static int selectedComboIndex = 0;
		ImGui::Text("Select Combo to Record:");
		ImGui::SameLine();
		if (ImGui::Combo("##ComboSelector", &selectedComboIndex, comboTypes, IM_ARRAYSIZE(comboTypes))) {
			// Reset recording state when changing selection
			vr.isCapturingCombo = false;
			vr.currentComboType = VR::ComboType::None;
			vr.recordedCombo.clear();
		}
		if (ImGui::Button("Record Selected Combo")) {
			// Start recording the selected combo
			vr.isCapturingCombo = true;
			vr.currentComboType = static_cast<VR::ComboType>(selectedComboIndex + 1);
			vr.currentComboName = comboTypes[selectedComboIndex];
			vr.recordedCombo.clear();
			vr.comboStartTime = Util::GetNowSecs();
			vr.recordingButtonControllers.clear();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear")) {
			// Clear the selected combo
			switch (selectedComboIndex) {
			case 0:
				settings.VRMenuOpenKeys.clear();
				break;
			case 1:
				settings.VRMenuCloseKeys.clear();
				break;
			case 2:
				settings.VROverlayOpenKeys.clear();
				break;
			case 3:
				settings.VROverlayCloseKeys.clear();
				break;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Click to start recording a new button combination for the selected action.");
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		// Table for displaying current key bindings
		if (ImGui::BeginTable("##VRBindingsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Current Binding");
			ImGui::TableSetupColumn("Description");
			ImGui::TableHeadersRow();
			// Define VR key binding configurations
			struct VRKeyBindingConfig
			{
				const char* label;
				std::vector<InputCombo>& combos;
				const char* description;
				const char* controllerRequirement;
			};
			std::vector<VRKeyBindingConfig> keyBindingConfigs = {
				{ "Open Community Shaders Menu", settings.VRMenuOpenKeys, "Button combination to open the Community Shaders menu", "Primary" },
				{ "Close Community Shaders Menu", settings.VRMenuCloseKeys, "Button combination to close the Community Shaders menu", "Both" },
				{ "Open VR Overlay", settings.VROverlayOpenKeys, "Button combination to open the VR overlay", "Primary" },
				{ "Close VR Overlay", settings.VROverlayCloseKeys, "Button combination to close the VR overlay", "Secondary" }
			};
			for (size_t row = 0; row < keyBindingConfigs.size(); ++row) {
				const auto& config = keyBindingConfigs[row];
				ImGui::TableNextRow();
				// Highlight the selected row
				if (row == static_cast<size_t>(selectedComboIndex)) {
					ImU32 highlight = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 0.15f));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight);
				}
				// Make row selectable
				ImGui::TableSetColumnIndex(0);
				char selectableId[64];
				snprintf(selectableId, sizeof(selectableId), "##combo_row_%zu", row);
				bool rowSelected = (row == static_cast<size_t>(selectedComboIndex));
				if (ImGui::Selectable(selectableId, rowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 0))) {
					selectedComboIndex = static_cast<int>(row);
				}
				ImGui::SameLine(0, 0);
				ImGui::Text("%s", config.label);
				// Current Binding column
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(config.combos, false);
				// Description column
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", config.description);
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		// Reset to defaults button
		if (ImGui::Button("Reset to Defaults")) {
			// Use InputCombo structure for cleaner defaults
			settings.VRMenuOpenKeys = {
				InputCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA)),
				InputCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY))
			};
			settings.VRMenuCloseKeys = {
				InputCombo::Both(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip))
			};
			settings.VROverlayOpenKeys = {
				InputCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
			};
			settings.VROverlayCloseKeys = {
				InputCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
			};
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reset all VR key bindings to their default values.");
		}
	}
	void DrawDebugSection()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		auto menu = globals::menu;

		// OpenVR Version Information
		if (ImGui::CollapsingHeader("OpenVR Information", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& info = vr.openVRInfo;
			if (info.isAvailable) {
				ImGui::Text("OpenVR System: %s", info.isCompatible ? "Active & Compatible" : "Active but INCOMPATIBLE");
				if (!info.isCompatible) {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Reason: Required OpenVR system/compositor interfaces are unavailable. VR menus disabled.");
				}
				ImGui::Text("DLL Path: %s", info.dllPath.c_str());
				ImGui::Text("DLL Version: %s", info.version.c_str());
				ImGui::Text("DLL Size: %llu bytes", info.fileSize);
				ImGui::Text("Modified: %s", info.modificationTime.c_str());
				ImGui::Text("Runtime: %s", VRDetection::RuntimeTypeToString(info.runtimeType));
				ImGui::Text("Interfaces: overlay=%s system=%s compositor=%s",
					info.hasOverlayInterface ? "yes" : "no",
					info.hasSystemInterface ? "yes" : "no",
					info.hasCompositorInterface ? "yes" : "no");
				ImGui::Text("Menu Path: %s", vr.ShouldUseInSceneOverlay() ? "In-scene" : "IVROverlay");
			} else {
				ImGui::Text("OpenVR system not available");
			}
		}

		// Controller Diagnostics Section
		if (ImGui::CollapsingHeader("Controller Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Checkbox("Test Mode: Disable controller menu input (except scroll controller and triggers)", &settings.VRMenuControllerDiagnosticsTestMode)) {
				ImGui::SetScrollHereY(0.0f);  // Scroll to top of the window when toggled
			}
			ImGui::SeparatorText("Button State");
			double nowSecs = Util::GetNowSecs();
			// Get highlight color from theme
			ImVec4 highlightColor = menu->GetTheme().StatusPalette.InfoColor;
			ImU32 highlightColorU32 = ImGui::ColorConvertFloat4ToU32(highlightColor);

			// Determine display order based on handedness
			bool isLeftHanded = vr.lastKnownLeftHandedMode;  // Use cached handedness

			if (ImGui::BeginTable("vr_input_state_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Button");
				if (isLeftHanded) {
					// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
				} else {
					// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
				}
				ImGui::TableHeadersRow();
				// Helper for button type text
				auto DrawButtonType = [](const RE::ButtonState& state) {
					if (!state.isPressed) {
						if (state.IsClick())
							ImGui::TextUnformatted("Click");
						else if (state.IsHold())
							ImGui::TextUnformatted("Hold");
						else
							ImGui::TextUnformatted("-");
					} else {
						ImGui::TextUnformatted("Held");
					}
				};
				// Helper for printing a row with left/right cell highlight
				auto printRow = [&](const char* label, const RE::ButtonState& left, const RE::ButtonState& right) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::TextUnformatted(left.isPressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(2);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::Text("%.2f", left.GetCurrentHeldTime(nowSecs));
					ImGui::TableSetColumnIndex(3);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					DrawButtonType(left);
					ImGui::TableSetColumnIndex(4);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::TextUnformatted(right.isPressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(5);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::Text("%.2f", right.GetCurrentHeldTime(nowSecs));
					ImGui::TableSetColumnIndex(6);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					DrawButtonType(right);
				};

				// Helper to determine the correct order for display based on handedness
				auto printRowWithHandedness = [&](const char* label, auto key) {
					auto& primary = vr.primaryControllerState[key];
					auto& secondary = vr.secondaryControllerState[key];
					if (isLeftHanded) {
						// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
						printRow(label, primary, secondary);
					} else {
						// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
						printRow(label, secondary, primary);
					}
				};

				printRowWithHandedness("Trigger", RE::BSOpenVRControllerDevice::Keys::kTrigger);
				printRowWithHandedness("Grip", RE::BSOpenVRControllerDevice::Keys::kGrip);
				printRowWithHandedness("GripAlt", RE::BSOpenVRControllerDevice::Keys::kGripAlt);
				printRowWithHandedness("Stick Click", RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger);
				printRowWithHandedness("Touchpad Click", RE::BSOpenVRControllerDevice::Keys::kTouchpadClick);
				printRowWithHandedness("Touchpad Alt", RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt);
				printRowWithHandedness("B/Y", RE::BSOpenVRControllerDevice::Keys::kBY);
				printRowWithHandedness("A/X", RE::BSOpenVRControllerDevice::Keys::kXA);
				ImGui::EndTable();
			}
			ImGui::SeparatorText("VR Thumbstick State");
			// Helper to draw a thumbstick quadrant visualization (returns ImVec2 for label alignment)
			auto DrawThumbstickPad = [&](float x, float y, ImU32 highlightCol) -> ImVec2 {
				ImVec2 padSize = ImVec2(80, 80);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 center = ImVec2(cursor.x + padSize.x / 2, cursor.y + padSize.y / 2);
				float radius = padSize.x / 2 - 4;
				ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
				ImU32 axisCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
				ImU32 dotCol = ImGui::GetColorU32(ImGuiCol_Text);
				// Draw background
				drawList->AddRectFilled(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
				// Draw border
				drawList->AddRect(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), borderCol, 4.0f, 0, 2.0f);
				// Draw axes
				drawList->AddLine(ImVec2(center.x, cursor.y + 4), ImVec2(center.x, cursor.y + padSize.y - 4), axisCol, 1.0f);
				drawList->AddLine(ImVec2(cursor.x + 4, center.y), ImVec2(cursor.x + padSize.x - 4, center.y), axisCol, 1.0f);
				// Determine quadrant
				int quad = 0;
				if (x > 0 && y > 0)
					quad = 1;  // top-right
				else if (x < 0 && y > 0)
					quad = 2;  // top-left
				else if (x < 0 && y < 0)
					quad = 3;  // bottom-left
				else if (x > 0 && y < 0)
					quad = 4;  // bottom-right
				// Highlight quadrant
				if (quad != 0) {
					ImVec2 q0 = center;
					ImVec2 q1 = center;
					ImVec2 q2 = center;
					ImVec2 q3 = center;
					if (quad == 1) {  // top-right
						q1.x += radius;
						q1.y -= radius;
						q2.x += radius;
						q2.y += 0;
						q3.x += 0;
						q3.y -= radius;
					} else if (quad == 2) {  // top-left
						q1.x -= radius;
						q1.y -= radius;
						q2.x -= radius;
						q2.y += 0;
						q3.x += 0;
						q3.y -= radius;
					} else if (quad == 3) {  // bottom-left
						q1.x -= radius;
						q1.y += radius;
						q2.x -= radius;
						q2.y += 0;
						q3.x += 0;
						q3.y += radius;
					} else if (quad == 4) {  // bottom-right
						q1.x += radius;
						q1.y += radius;
						q2.x += radius;
						q2.y += 0;
						q3.x += 0;
						q3.y += radius;
					}
					ImVec2 poly[4] = { center, q1, q2, q3 };
					drawList->AddConvexPolyFilled(poly, 4, highlightCol);
				}
				// Draw stick position dot
				ImVec2 dot = ImVec2(center.x + x * radius, center.y - y * radius);
				drawList->AddCircleFilled(dot, 5.0f, dotCol);
				// Return size for label alignment
				return padSize;
			};
			ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(menu->GetTheme().StatusPalette.InfoColor);
			if (ImGui::BeginTable("##VRThumbstickTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				if (isLeftHanded) {
					// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				} else {
					// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				}
				ImGui::TableHeadersRow();

				// Left column content
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginGroup();
				if (isLeftHanded) {
					// Left-handed: Show primary controller in left column
					ImVec2 padSizeL = DrawThumbstickPad(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
					ImGui::Dummy(padSizeL);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				} else {
					// Right-handed: Show secondary controller in left column
					ImVec2 padSizeL = DrawThumbstickPad(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, highlightCol);
					ImGui::Dummy(padSizeL);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, RE::GetQuadrantName(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y));
				}
				ImGui::EndGroup();

				// Right column content
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginGroup();
				if (isLeftHanded) {
					// Left-handed: Show secondary controller in right column
					ImVec2 padSizeR = DrawThumbstickPad(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, highlightCol);
					ImGui::Dummy(padSizeR);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, RE::GetQuadrantName(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y));
				} else {
					// Right-handed: Show primary controller in right column
					ImVec2 padSizeR = DrawThumbstickPad(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
					ImGui::Dummy(padSizeR);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				}
				ImGui::EndGroup();
				ImGui::EndTable();
			}
			ImGui::SeparatorText("Recent VR Controller Events");
			ImGui::TextDisabled("Note: For thumbstick events, KeyCode/Value columns show X/Y floats.");
			if (ImGui::BeginTable("eventlog", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("KeyCode/X", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Value/Y", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Pressed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Known Mapping", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("Event Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableHeadersRow();
				for (const auto& e : vr.vrControllerEventLog) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%d", e.device);
					ImGui::TableSetColumnIndex(1);
					if (e.heldSource == "thumbstick") {
						ImGui::Text("%.3f", e.thumbstickX);
					} else {
						ImGui::Text("%d", e.keyCode);
					}
					ImGui::TableSetColumnIndex(2);
					if (e.heldSource == "thumbstick") {
						ImGui::Text("%.3f", e.thumbstickY);
					} else {
						ImGui::Text("%d", e.value);
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%s", e.pressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(4);
					if (e.heldSource == "thumbstick") {
						ImGui::TextUnformatted(e.controllerRole.c_str());
					} else {
						ImGui::TextUnformatted(RE::GetOpenVRButtonName(e.keyCode));
					}
					ImGui::TableSetColumnIndex(5);
					if (e.heldSource == "thumbstick") {
						ImGui::TextUnformatted("-");
					} else {
						// Show click/hold for release events if available
						if (!e.pressed) {
							if (e.heldTime > 0.0) {
								if (e.heldTime < 0.5) {
									ImGui::Text("Click (%.2fs)", e.heldTime);
								} else {
									ImGui::Text("Hold (%.2fs)", e.heldTime);
								}
							} else {
								ImGui::Text("Release");
							}
						} else if (e.pressed) {
							if (e.heldTime > 0.0) {
								ImGui::Text("Held for %.2fs", e.heldTime);
							} else {
								ImGui::Text("Press");
							}
						}
					}
				}
				ImGui::EndTable();
			}

			// Wand Pointing Diagnostics
			ImGui::SeparatorText("Wand Pointing State");
			if (ImGui::BeginTable("##WandPointingState", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Wand Pointing Enabled");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", settings.EnableWandPointing ? "Yes" : "No");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Intersecting Overlay");
				ImGui::TableSetColumnIndex(1);
				if (vr.wandState.isIntersecting) {
					ImGui::TextColored(menu->GetTheme().StatusPalette.InfoColor, "YES");
				} else {
					ImGui::Text("No");
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("UV Coordinates");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.3f, %.3f)", vr.wandState.uvCoordinates.x, vr.wandState.uvCoordinates.y);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Controller Index");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", vr.wandState.controllerIndex);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Ray Origin");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.2f, %.2f, %.2f)", vr.wandState.rayOrigin.x, vr.wandState.rayOrigin.y, vr.wandState.rayOrigin.z);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Ray Direction");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.2f, %.2f, %.2f)", vr.wandState.rayDirection.x, vr.wandState.rayDirection.y, vr.wandState.rayDirection.z);

				ImGui::EndTable();
			}
		}

		// Debugging addresses for copy/paste
		if (ImGui::CollapsingHeader("OpenVR Addresses")) {
			auto openvr = RE::BSOpenVR::GetSingleton();
			auto overlay = openvr && vr.openVRInfo.hasOverlayInterface ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
			auto vrSystem = openvr ? openvr->vrSystem : nullptr;
			ADDRESS_NODE(openvr)
			ADDRESS_NODE(overlay)
			ADDRESS_NODE(vrSystem)
		}
	}
}  // namespace

//=============================================================================
// VR-SPECIFIC PUBLIC API
//=============================================================================

void VR::UpdateVROverlayPosition()
{
	Util::OpenVRContext ctx(true);
	if (!ctx.HasOverlay())
		return;

	if (menuOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	// Determine positioning strategy based on settings
	bool showOnController = (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both);
	bool showOnHMD = (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both);

	// Texture size
	float aspect = static_cast<float>(kOverlayHeight) / kOverlayWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;
	float offsetX = settings.VRMenuOffsetX;
	float offsetY = settings.VRMenuOffsetY;
	float offsetZ = settings.VRMenuOffsetZ;

	static int lastPositioningMethod = -1;
	bool justSwitchedToFixed = (lastPositioningMethod != 1 && settings.VRMenuPositioningMethod == 1);
	lastPositioningMethod = settings.VRMenuPositioningMethod;

	// Handle HMD positioning
	if (showOnHMD) {
		if (settings.VRMenuPositioningMethod == 0) {
			// HMD Relative positioning
			vr::TrackedDevicePose_t hmdPose;
			if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, &hmdPose, 1))
				return;

			if (hmdPose.bPoseIsValid) {
				// Calculate position in front of HMD using offsets directly
				float height = 0.0f;

				// Create transform matrix - start with identity
				vr::HmdMatrix34_t hmdTransform;
				hmdTransform.m[0][0] = 1.0f;
				hmdTransform.m[0][1] = 0.0f;
				hmdTransform.m[0][2] = 0.0f;
				hmdTransform.m[0][3] = 0.0f;
				hmdTransform.m[1][0] = 0.0f;
				hmdTransform.m[1][1] = 1.0f;
				hmdTransform.m[1][2] = 0.0f;
				hmdTransform.m[1][3] = 0.0f;
				hmdTransform.m[2][0] = 0.0f;
				hmdTransform.m[2][1] = 0.0f;
				hmdTransform.m[2][2] = 1.0f;
				hmdTransform.m[2][3] = 0.0f;

				// Copy HMD position
				hmdTransform.m[0][3] = hmdPose.mDeviceToAbsoluteTracking.m[0][3];
				hmdTransform.m[1][3] = hmdPose.mDeviceToAbsoluteTracking.m[1][3];
				hmdTransform.m[2][3] = hmdPose.mDeviceToAbsoluteTracking.m[2][3];

				// Copy HMD orientation
				hmdTransform.m[0][0] = hmdPose.mDeviceToAbsoluteTracking.m[0][0];
				hmdTransform.m[0][1] = hmdPose.mDeviceToAbsoluteTracking.m[0][1];
				hmdTransform.m[0][2] = hmdPose.mDeviceToAbsoluteTracking.m[0][2];
				hmdTransform.m[1][0] = hmdPose.mDeviceToAbsoluteTracking.m[1][0];
				hmdTransform.m[1][1] = hmdPose.mDeviceToAbsoluteTracking.m[1][1];
				hmdTransform.m[1][2] = hmdPose.mDeviceToAbsoluteTracking.m[1][2];
				hmdTransform.m[2][0] = hmdPose.mDeviceToAbsoluteTracking.m[2][0];
				hmdTransform.m[2][1] = hmdPose.mDeviceToAbsoluteTracking.m[2][1];
				hmdTransform.m[2][2] = hmdPose.mDeviceToAbsoluteTracking.m[2][2];

				// Apply HMD offset positions directly (in HMD local space)
				hmdTransform.m[0][3] += hmdTransform.m[0][0] * offsetX + hmdTransform.m[0][1] * offsetY + hmdTransform.m[0][2] * offsetZ;
				hmdTransform.m[1][3] += hmdTransform.m[1][0] * offsetX + hmdTransform.m[1][1] * offsetY + hmdTransform.m[1][2] * offsetZ;
				hmdTransform.m[2][3] += hmdTransform.m[2][0] * offsetX + hmdTransform.m[2][1] * offsetY + hmdTransform.m[2][2] * offsetZ;

				// Move up by height (Y axis in HMD space)
				hmdTransform.m[0][3] += hmdTransform.m[0][1] * height;
				hmdTransform.m[1][3] += hmdTransform.m[1][1] * height;
				hmdTransform.m[2][3] += hmdTransform.m[2][1] * height;

				// Scale the overlay based on width/height
				hmdTransform.m[0][0] *= overlayWidth;
				hmdTransform.m[1][1] *= overlayHeight;

				Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
				ctx.overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &hmdTransform);
				ctx.overlay->SetOverlayWidthInMeters(menuOverlayHandle, baseWidth * settings.VRMenuScale);

			} else {
				logger::debug("HMD pose invalid, falling back to fixed positioning");
				settings.VRMenuPositioningMethod = 1;  // Fall back to fixed positioning
			}
		}

		if (settings.VRMenuPositioningMethod == 1) {
			// Fixed World Position
			// Cache player position once per frame
			RE::NiPoint3 playerPos = savedPlayerWorldPos;
			auto player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				playerPos = player->GetPosition();
			}

			if (justSwitchedToFixed) {
				SetFixedOverlayToCurrentHMD();
				// Save player position when switching to Fixed World Position
				savedPlayerWorldPos = playerPos;
			}

			// --- Auto reset logic using player world position ---
			float sqDist = playerPos.GetSquaredDistance(savedPlayerWorldPos);
			float thresholdSq = settings.VRMenuAutoResetDistance * settings.VRMenuAutoResetDistance;
			if (sqDist > thresholdSq) {
				SetFixedOverlayToCurrentHMD();
				// Update saved position after reset
				savedPlayerWorldPos = playerPos;
			}

			// Scale the overlay based on width/height (same as relative HMD mode)
			vr::HmdMatrix34_t fixedTransform = Util::MatrixToHmdMatrix34(fixedWorldOverlayPosition.m);
			fixedTransform.m[0][0] *= overlayWidth;
			fixedTransform.m[1][1] *= overlayHeight;

			Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
			ctx.overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &fixedTransform);
			ctx.overlay->SetOverlayWidthInMeters(menuOverlayHandle, baseWidth * settings.VRMenuScale);
		}
	}

	// Handle controller positioning separately (can be shown alongside HMD)
	if (showOnController) {
		// Get the VR controller overlay handle from Menu.cpp
		if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
			return;
		}

		// Attach to controller
		vr::TrackedDeviceIndex_t controllerIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);

		if (controllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
			// Position relative to controller using offset settings
			vr::HmdMatrix34_t transform = Util::CreateControllerOverlayTransform(
				settings.VRMenuControllerOffsetX,
				settings.VRMenuControllerOffsetY,
				settings.VRMenuControllerOffsetZ,
				overlayWidth,
				overlayHeight);

			Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
			ctx.overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

			// Update the overlay width to match the calculated size
			ctx.overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, overlayWidth);

			// Update controller overlay flags for input interaction
			Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
		}
	}

	// Update overlay flags for input interaction
	Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
}

void VR::UpdateVROverlayControllerPosition()
{
	Util::OpenVRContext ctx(true);
	if (!ctx.HasOverlay())
		return;

	// Get the VR controller overlay handle from Menu.cpp
	if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	// Texture size based on preset
	float aspect = static_cast<float>(kOverlayHeight) / kOverlayWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;

	// Find the appropriate controller for the controller overlay
	vr::TrackedDeviceIndex_t controllerIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
	if (controllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
		ctx.overlay->HideOverlay(menuControllerOverlayHandle);
		return;
	}

	// Position relative to controller using offset settings
	vr::HmdMatrix34_t transform = Util::CreateControllerOverlayTransform(
		settings.VRMenuControllerOffsetX,
		settings.VRMenuControllerOffsetY,
		settings.VRMenuControllerOffsetZ,
		overlayWidth,
		overlayHeight);

	Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
	ctx.overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

	// Update the overlay width to match the calculated size
	ctx.overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, overlayWidth);

	// Update controller overlay flags for input interaction
	Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
}

// Add overlay management methods for VR menu overlays
void VR::EnsureOverlayInitialized()
{
	// Check OpenVR compatibility first
	if (!openVRInfo.isCompatible) {
		logger::warn("Required OpenVR system/compositor interfaces are unavailable.");
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	static bool loggedOpenVRContext = false;
	if (!loggedOpenVRContext) {
		logger::debug("BSOpenVR: 0x{:X}", reinterpret_cast<uintptr_t>(openvr));
	}
	if (!openvr) {
		logger::error("BSOpenVR::GetSingleton() returned nullptr");
		return;
	}
	auto* vrSystem = openvr->vrSystem;
	const bool wantsControllerOverlay =
		settings.attachMode == Settings::OverlayAttachMode::ControllerOnly ||
		settings.attachMode == Settings::OverlayAttachMode::Both;
	RecreateOverlayTexturesIfNeeded(wantsControllerOverlay);

	auto* overlay = openVRInfo.hasOverlayInterface ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!loggedOpenVRContext) {
		logger::debug("openVR->vrSystem: 0x{:X}", reinterpret_cast<uintptr_t>(vrSystem));
		logger::debug("openVR->vrContext: 0x{:X}", reinterpret_cast<uintptr_t>(&openvr->vrContext));
		logger::debug("openVR->vrContext.vrOverlay: 0x{:X}", reinterpret_cast<uintptr_t>(openvr->vrContext.vrOverlay));
		logger::debug("openVR->hmdDeviceType: {} ({})", static_cast<int>(openvr->hmdDeviceType), magic_enum::enum_name(openvr->hmdDeviceType));
		for (int i = 0; i < RE::BSVRInterface::Hand::kTotal; ++i) {
			logger::debug("openVR->controllerNodes[{}]: 0x{:X}", i, reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()));
			if (openvr->controllerNodes[i] && reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()) < 0x1000) {
				logger::warn("controllerNodes[{}] is suspiciously low (0x{:X})", i, reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()));
			}
		}
		loggedOpenVRContext = true;
	}
	logger::debug("menuOverlayHandle: 0x{:X}", menuOverlayHandle);
	logger::debug("menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
	if (!overlay) {
		if (settings.menuOverlayPath == Settings::MenuOverlayPath::IVROverlay) {
			logger::error("IVROverlay is unavailable for forced IVROverlay menu path");
		} else {
			logger::debug("IVROverlay is unavailable; using in-scene menu path");
		}
		return;
	}

	auto ensureOverlayHandle = [&](const char* key, const char* name, vr::VROverlayHandle_t& handle) {
		if (handle != vr::k_ulOverlayHandleInvalid) {
			return;
		}

		vr::EVROverlayError err = overlay->FindOverlay(key, &handle);
		if (err == vr::VROverlayError_None) {
			logger::debug("FindOverlay succeeded for {}: 0x{:X}", key, handle);
			Util::SetOverlayInputFlags(overlay, handle);
			overlay->SetOverlayWidthInMeters(handle, 1.0f);
			return;
		}

		err = overlay->CreateOverlay(key, name, &handle);
		if (err == vr::VROverlayError_None) {
			logger::debug("CreateOverlay succeeded for {}: 0x{:X}", key, handle);
			Util::SetOverlayInputFlags(overlay, handle);
			overlay->SetOverlayWidthInMeters(handle, 1.0f);
			return;
		}

		handle = vr::k_ulOverlayHandleInvalid;
		logger::error("CreateOverlay failed for {}: {} ({})", key, static_cast<int>(err), magic_enum::enum_name(err));
	};

	ensureOverlayHandle(kMenuOverlayKey, kMenuOverlayName, menuOverlayHandle);
	ensureOverlayHandle(kControllerOverlayKey, kControllerOverlayName, menuControllerOverlayHandle);
}

//=============================================================================
// PRIVATE IMPLEMENTATION
//=============================================================================

void VR::DestroyOverlay()
{
	if (!openVRInfo.hasOverlayInterface) {
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!overlay) {
		logger::debug("DestroyOverlay: IVROverlay is unavailable");
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
		return;
	}
	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuOverlayHandle);
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuControllerOverlayHandle);
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
}

void VR::RecreateOverlayTexturesIfNeeded(bool needsControllerTexture)
{
	if (!globals::d3d::device) {
		static bool warnedMissingDevice = false;
		if (!warnedMissingDevice) {
			logger::warn("RecreateOverlayTexturesIfNeeded: D3D11 device is unavailable");
			warnedMissingDevice = true;
		}
		return;
	}

	auto isTextureValid = [](ID3D11Texture2D* texture, ID3D11RenderTargetView* rtv) {
		if (!texture || !rtv) {
			return false;
		}

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		return desc.Width == kOverlayWidth &&
		       desc.Height == kOverlayHeight &&
		       desc.ArraySize == 1 &&
		       desc.MipLevels == 1;
	};

	if (!isTextureValid(menuTexture.get(), menuRTV.get())) {
		Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuTexture.put(), menuRTV.put());
	}

	if (needsControllerTexture && !isTextureValid(menuControllerTexture.get(), menuControllerRTV.get())) {
		Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuControllerTexture.put(), menuControllerRTV.put());
	}
}

void VR::HideAllOverlays(vr::IVROverlay* gameOverlay)
{
	if (!gameOverlay) {
		return;
	}

	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		gameOverlay->HideOverlay(menuOverlayHandle);
	}
	if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		gameOverlay->HideOverlay(menuControllerOverlayHandle);
	}
}

void VR::HideOverlaysIfPresent()
{
	if (!openVRInfo.isCompatible || !openVRInfo.hasOverlayInterface) {
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* gameOverlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	HideAllOverlays(gameOverlay);
}

void VR::SubmitOverlayFrame()
{
	// Skip overlay operations if OpenVR is incompatible
	if (!openVRInfo.isCompatible) {
		return;
	}

	if (!globals::menu || !globals::d3d::context) {
		return;
	}

	const bool shouldUseInSceneOverlay = ShouldUseInSceneOverlay();
	if (shouldUseInSceneOverlay) {
		InstallSubmitHook();
	}
	const bool useInSceneOverlay = shouldUseInSceneOverlay && inSceneResources.submitHookInstalled;
	const bool useIVROverlay = !shouldUseInSceneOverlay;

	if (useIVROverlay && !openVRInfo.hasOverlayInterface) {
		static bool loggedMissingOverlayInterface = false;
		if (!loggedMissingOverlayInterface) {
			logger::error("VR: IVROverlay menu path is forced, but the runtime does not expose IVROverlay");
			loggedMissingOverlayInterface = true;
		}
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		logger::error("SubmitOverlayFrame: BSOpenVR or vrSystem is nullptr");
		return;
	}

	const bool hasOverlayHandles =
		menuOverlayHandle != vr::k_ulOverlayHandleInvalid ||
		menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid;
	auto* gameOverlay = openVRInfo.hasOverlayInterface && (useIVROverlay || hasOverlayHandles) ?
	                         RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) :
	                         nullptr;
	auto* cleanOverlay = useIVROverlay ? RE::BSOpenVR::GetCleanIVROverlay() : nullptr;

	static bool cleanOverlayLogged = false;
	if (useIVROverlay && !cleanOverlayLogged) {
		if (cleanOverlay) {
			logger::debug("VR: Successfully acquired clean IVROverlay interface via CommonLib: 0x{:X}", reinterpret_cast<uintptr_t>(cleanOverlay));
		} else {
			logger::error("VR: Failed to get clean IVROverlay interface via CommonLib");
		}
		cleanOverlayLogged = true;
	}

	if (useIVROverlay && (!gameOverlay || !cleanOverlay)) {
		return;
	}

	// Update drag logic for all modes - only when overlay is visible
	auto& enabled = globals::menu->IsEnabled;
	auto& overlayVisible = globals::menu->overlayVisible;
	const bool shouldShowAutoHide = ShouldShowAutoHideOverlay();
	const bool shouldRenderOverlay = enabled || overlayVisible || shouldShowAutoHide;
	static bool wasMenuEnabled = false;
	const bool menuJustOpened = enabled && !wasMenuEnabled;
	wasMenuEnabled = enabled;

	// In fixed-world mode, recenter once on menu open using current HMD pose,
	// then keep it world-locked for the rest of the session.
	if (menuJustOpened &&
	    settings.VRMenuPositioningMethod == 1 &&
	    (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both)) {
		SetFixedOverlayToCurrentHMD();
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			savedPlayerWorldPos = player->GetPosition();
		}
	}

	const bool wantsHMDOverlay = settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both;
	const bool wantsControllerOverlay = settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both;
	const bool wantsAnyVROverlay = wantsHMDOverlay || wantsControllerOverlay;

	if (shouldRenderOverlay && wantsAnyVROverlay) {
		RecreateOverlayTexturesIfNeeded(useIVROverlay && wantsControllerOverlay);
	}

	if (shouldRenderOverlay && useIVROverlay && openVRInfo.hasOverlayInterface) {
		const bool missingRequiredHandles =
			(wantsHMDOverlay && menuOverlayHandle == vr::k_ulOverlayHandleInvalid) ||
			(wantsControllerOverlay && menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid);
		if (missingRequiredHandles) {
			EnsureOverlayInitialized();
		}
	}

	const bool hasMenuTexture = menuTexture.get() && menuRTV.get();
	const bool hasControllerTexture = menuControllerTexture.get() && menuControllerRTV.get();
	const bool hasRequiredTextures = hasMenuTexture && (!useIVROverlay || !wantsControllerOverlay || hasControllerTexture);
	const bool hasRequiredOverlayHandles =
		(!wantsHMDOverlay || menuOverlayHandle != vr::k_ulOverlayHandleInvalid) &&
		(!wantsControllerOverlay || menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid);
	const bool canUseIVROverlay = useIVROverlay && gameOverlay && cleanOverlay && hasRequiredOverlayHandles;

	if (shouldRenderOverlay && wantsAnyVROverlay && (useInSceneOverlay || canUseIVROverlay) && hasRequiredTextures) {
		// Update drag logic only when overlay is active
		UpdateOverlayDrag();
		// Copy ImGui output to overlay texture
		ID3D11RenderTargetView* oldRTV = nullptr;
		globals::d3d::context->OMGetRenderTargets(1, &oldRTV, nullptr);
		ID3D11RenderTargetView* menuRTVPtr = menuRTV.get();
		globals::d3d::context->OMSetRenderTargets(1, &menuRTVPtr, nullptr);
		float clearColor[4] = { 0, 0, 0, 0 };
		globals::d3d::context->ClearRenderTargetView(menuRTV.get(), clearColor);
		// Re-render ImGui for HMD overlay
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);

		if (useInSceneOverlay) {
			// The submit hook renders menuTexture into each eye. Keep the legacy
			// IVROverlay handles hidden or the menu appears twice with a stereo offset.
			const bool overlayBeingDragged = settings.EnableDragToReposition && overlayDragState.dragging;
			Util::ApplyHighlightTintToTexture(menuTexture.get(), overlayBeingDragged, settings.dragHighlightColor);
			UpdateFixedWorldPositioning();
			HideAllOverlays(gameOverlay);
		} else {
			// Apply highlight tint to HMD overlay if it's being dragged
			bool hmdBeingDragged = settings.EnableDragToReposition && overlayDragState.dragging &&
			                       (overlayDragState.mode == OverlayDragState::DragMode::HMD ||
									   overlayDragState.mode == OverlayDragState::DragMode::FixedWorld);
			Util::ApplyHighlightTintToTexture(menuTexture.get(), hmdBeingDragged, settings.dragHighlightColor);

			// Update overlay position and submit to SteamVR
			UpdateVROverlayPosition();
			vr::Texture_t tex = { menuTexture.get(), vr::TextureType_DirectX, vr::ColorSpace_Auto };
			if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
				Util::SetOverlayInputFlags(gameOverlay, menuOverlayHandle);
				vr::EVROverlayError err = cleanOverlay->SetOverlayTexture(menuOverlayHandle, &tex);
				if (err != vr::VROverlayError_None) {
					logger::error("SetOverlayTexture failed for menu overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
				}
				err = gameOverlay->ShowOverlay(menuOverlayHandle);
				if (err != vr::VROverlayError_None) {
					logger::error("ShowOverlay failed for menu overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
				}
			} else if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
				gameOverlay->HideOverlay(menuOverlayHandle);
			}
			// Controller overlay
			if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
				// Copy the same ImGui output to controller overlay texture
				ID3D11RenderTargetView* menuControllerRTVPtr = menuControllerRTV.get();
				globals::d3d::context->OMSetRenderTargets(1, &menuControllerRTVPtr, nullptr);
				globals::d3d::context->ClearRenderTargetView(menuControllerRTV.get(), clearColor);
				// Re-render ImGui for controller overlay
				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);

				// Apply highlight tint to controller overlay if it's being dragged
				bool controllerBeingDragged = overlayDragState.dragging &&
				                              overlayDragState.mode == OverlayDragState::DragMode::Controller;
				Util::ApplyHighlightTintToTexture(menuControllerTexture.get(), controllerBeingDragged, settings.dragHighlightColor);

				// Position controller overlay and submit
				UpdateVROverlayControllerPosition();

				vr::Texture_t controllerTex = { menuControllerTexture.get(), vr::TextureType_DirectX, vr::ColorSpace_Auto };
				Util::SetOverlayInputFlags(gameOverlay, menuControllerOverlayHandle);
				vr::EVROverlayError err = cleanOverlay->SetOverlayTexture(menuControllerOverlayHandle, &controllerTex);
				if (err != vr::VROverlayError_None) {
					logger::error("SetOverlayTexture failed for controller overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
				}
				err = gameOverlay->ShowOverlay(menuControllerOverlayHandle);
				if (err != vr::VROverlayError_None) {
					logger::error("ShowOverlay failed for controller overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
				}
			} else if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
				gameOverlay->HideOverlay(menuControllerOverlayHandle);
			}
		}

		// Release oldRTV after all usage is complete to prevent use-after-free
		if (oldRTV)
			oldRTV->Release();
	} else {
		HideAllOverlays(gameOverlay);
	}
}

// Helper to centralize VR depth buffer culling logic, reducing duplication between DataLoaded, EarlyPrepass, and Settings UI.
void VR::UpdateDepthBufferCulling()
{
	if (!gDepthBufferCulling) {
		return;
	}

	const auto* tes = globals::game::tes;
	const bool inInterior = tes && tes->interiorCell != nullptr;
	const bool desired = inInterior ? settings.EnableDepthBufferCullingInterior : settings.EnableDepthBufferCullingExterior;

	const bool previous = *gDepthBufferCulling;
	*gDepthBufferCulling = desired;

	if (previous != desired) {
		logger::info("VR depth buffer culling set to {}", desired);
	}
}

//=============================================================================
// OPENVR VERSION DETECTION AND COMPATIBILITY
//=============================================================================

void VR::DetectOpenVRInfo()
{
	openVRInfo = VRDetection::Detect();
}

bool VR::IsOpenVRCompatible() const
{
	return openVRInfo.isAvailable && openVRInfo.isCompatible;
}
