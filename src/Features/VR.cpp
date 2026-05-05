#include "VR.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "RE/B/BSOpenVR.h"
#include "RE/N/NiPoint3.h"
#include "RE/P/PlayerCharacter.h"
#include "Upscaling.h"
#include <openvr.h>

#include "State.h"
#include "Utils/D3D.h"
#include "Utils/PerfUtils.h"
#include "Utils/UI.h"
#include "Utils/VRUtils.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <cmath>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <unordered_map>
#include <windows.h>
#include <winver.h>
#pragma comment(lib, "version.lib")

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
	EnableWandPointing)

//=============================================================================
// FEATURE BASE CLASS OVERRIDES
//=============================================================================

void VR::LoadSettings(json& o_json)
{
	settings = o_json.get<Settings>();
	// Validate and clamp loaded settings to ensure they're within valid ranges
	settings.ClampToValidRanges();
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
		logger::info("  Compatible: {}", openVRInfo.isCompatible ? "Yes" : "No");

		if (!openVRInfo.isCompatible) {
			logger::info("OpenVR version is incompatible.");
			logger::info("Community Shaders VR menus will be disabled for stability");
		}
	} else {
		logger::info("OpenVR DLL not available in current process");
	}
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
	       globals::state->isMainMenuOpen &&
	       globals::menu &&
	       !globals::menu->IsEnabled;
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
		ImGui::SetNextWindowSize(ImVec2(400.0f * Util::GetUIScale(), 200.0f * Util::GetUIScale()), ImGuiCond_FirstUseEver);
		if (ImGui::BeginPopupModal("Record Combo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

			ImGui::EndPopup();
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
			const char* attachModes[] = { "HMD Only", "Controller Only", "Both" };
			int attachModeInt = static_cast<int>(settings.attachMode);
			if (ImGui::Combo("Attach Mode", &attachModeInt, attachModes, IM_ARRAYSIZE(attachModes))) {
				settings.attachMode = static_cast<VR::Settings::OverlayAttachMode>(attachModeInt);
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
					std::string reason = std::format("{} {}", "OpenVR version is incompatible.", "VR menus disabled.");
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Reason: %s", reason.c_str());
				}
				ImGui::Text("DLL Path: %s", info.dllPath.c_str());
				ImGui::Text("DLL Version: %s", info.version.c_str());
				ImGui::Text("DLL Size: %llu bytes", info.fileSize);
				ImGui::Text("Modified: %s", info.modificationTime.c_str());
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
			auto overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
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
	Util::OpenVRContext ctx;
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
	Util::OpenVRContext ctx;
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
		logger::warn("OpenVR version is incompatible.");
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	logger::debug("BSOpenVR: 0x{:X}", reinterpret_cast<uintptr_t>(openvr));
	if (!openvr) {
		logger::error("BSOpenVR::GetSingleton() returned nullptr");
		return;
	}
	auto* vrSystem = openvr->vrSystem;
	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
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
	logger::debug("menuOverlayHandle: 0x{:X}", menuOverlayHandle);
	logger::debug("menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
	if (!overlay) {
		logger::error("IVROverlay is nullptr after GetIVROverlay");
		return;
	}
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuTexture.put(), menuRTV.put());
	std::string key = "communityshaders.menu";
	std::string name = "Community Shaders Menu";
	vr::EVROverlayError err = overlay->CreateOverlay(key.c_str(), name.c_str(), &menuOverlayHandle);
	if (err == vr::VROverlayError_None) {
		logger::debug("CreateOverlay succeeded for menuOverlayHandle: 0x{:X}", menuOverlayHandle);
		Util::SetOverlayInputFlags(overlay, menuOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuOverlayHandle, 1.0f);
	} else {
		logger::error("CreateOverlay failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
	}
	// Controller overlay
	std::string controllerKey = "communityshaders.menu.controller";
	std::string controllerName = "Community Shaders Menu (Controller)";
	err = overlay->CreateOverlay(controllerKey.c_str(), controllerName.c_str(), &menuControllerOverlayHandle);
	if (err == vr::VROverlayError_None) {
		logger::debug("CreateOverlay succeeded for menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
		Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuControllerTexture.put(), menuControllerRTV.put());
		Util::SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, 1.0f);
	} else {
		logger::error("CreateOverlay failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
	}
}

//=============================================================================
// PRIVATE IMPLEMENTATION
//=============================================================================

void VR::DestroyOverlay()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!overlay) {
		logger::error("DestroyOverlay: IVROverlay is nullptr");
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

void VR::RecreateOverlayTexturesIfNeeded()
{
	// Smart pointers automatically release existing resources when put() assigns new ones
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuTexture.put(), menuRTV.put());
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuControllerTexture.put(), menuControllerRTV.put());
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
	if (!openVRInfo.isCompatible) {
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

	InstallSubmitHook();

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* gameOverlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	auto* cleanOverlay = RE::BSOpenVR::GetCleanIVROverlay();

	static bool cleanOverlayLogged = false;
	if (!cleanOverlayLogged) {
		if (cleanOverlay) {
			logger::debug("VR: Successfully acquired clean IVROverlay interface via CommonLib: 0x{:X}", reinterpret_cast<uintptr_t>(cleanOverlay));
		} else {
			logger::error("VR: Failed to get clean IVROverlay interface via CommonLib");
		}
		cleanOverlayLogged = true;
	}

	if (!gameOverlay || !cleanOverlay) {
		return;
	}

	if (!openvr || !openvr->vrSystem) {
		logger::error("SubmitOverlayFrame: BSOpenVR or vrSystem is nullptr");
		return;
	}

	// Update drag logic for all modes - only when overlay is visible
	auto& enabled = globals::menu->IsEnabled;
	auto& overlayVisible = globals::menu->overlayVisible;
	const bool shouldShowAutoHide = ShouldShowAutoHideOverlay();
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

	if ((enabled || overlayVisible || shouldShowAutoHide) && menuOverlayHandle != vr::k_ulOverlayHandleInvalid && menuTexture.get() && menuRTV.get()) {
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
	// Reset info
	openVRInfo = {};

	// Find the OpenVR DLL module
	HMODULE hModule = GetModuleHandleA("openvr_api.dll");
	if (!hModule) {
		openVRInfo.isAvailable = false;
		return;
	}

	openVRInfo.isAvailable = true;

	// Get the full path to the DLL
	char dllPath[MAX_PATH];
	if (GetModuleFileNameA(hModule, dllPath, MAX_PATH) == 0) {
		openVRInfo.isCompatible = false;
		return;
	}

	openVRInfo.dllPath = dllPath;

	// Get file version information
	DWORD dwSize = GetFileVersionInfoSizeA(dllPath, nullptr);
	if (dwSize > 0) {
		std::vector<BYTE> buffer(dwSize);
		if (GetFileVersionInfoA(dllPath, 0, dwSize, buffer.data())) {
			VS_FIXEDFILEINFO* pFileInfo = nullptr;
			UINT len = 0;
			if (VerQueryValueA(buffer.data(), "\\", (LPVOID*)&pFileInfo, &len)) {
				DWORD major = HIWORD(pFileInfo->dwFileVersionMS);
				DWORD minor = LOWORD(pFileInfo->dwFileVersionMS);
				DWORD build = HIWORD(pFileInfo->dwFileVersionLS);
				DWORD revision = LOWORD(pFileInfo->dwFileVersionLS);
				openVRInfo.version = std::format("{}.{}.{}.{}", major, minor, build, revision);
			}
		}
	}

	if (openVRInfo.version.empty()) {
		openVRInfo.version = "Unknown";
	}

	// Get file size and timestamp
	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(dllPath, &findData);
	if (hFind != INVALID_HANDLE_VALUE) {
		FindClose(hFind);
		ULARGE_INTEGER fileSize;
		fileSize.LowPart = findData.nFileSizeLow;
		fileSize.HighPart = findData.nFileSizeHigh;
		openVRInfo.fileSize = fileSize.QuadPart;

		// Convert file time to readable format
		SYSTEMTIME st;
		FileTimeToSystemTime(&findData.ftLastWriteTime, &st);
		openVRInfo.modificationTime = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	}

	// Check compatibility
	openVRInfo.isCompatible = IsOpenVRCompatible();
}

bool VR::IsOpenVRCompatible() const
{
	if (!openVRInfo.isAvailable) {
		return false;
	}

	// Whitelist: Only allow explicitly known compatible versions
	struct WhitelistedVersion
	{
		std::string version;
		uint64_t fileSize;
		std::string modificationTime;
	};

	static const std::vector<WhitelistedVersion> whitelist = {
		{ "1.0.10.0", 598816, "2022-04-18 00:47:59" },
		// Add more known compatible versions here
	};

	for (const auto& entry : whitelist) {
		if (openVRInfo.version == entry.version) {
			return true;
		}
	}

	return false;  // Not compatible unless explicitly whitelisted
}
