#include "Features/VR.h"
#include "Menu.h"
#include "State.h"
#include "Utils/PerfUtils.h"
#include "Utils/VRUtils.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

using AttachMode = VR::Settings::OverlayAttachMode;

namespace
{
	constexpr size_t kNumVRInputMappings = 6;
	constexpr size_t kNumVRTriggerMappings = 1;

	struct ScrollAccum
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	bool gPrevPrimaryVRInputStates[kNumVRInputMappings] = {};
	bool gPrevSecondaryVRInputStates[kNumVRInputMappings] = {};
	bool gLastVRInputHandedness = false;
	std::unordered_map<size_t, ScrollAccum> gVRScrollAccums;

	void ResetVRImGuiButtonState()
	{
		std::fill_n(gPrevPrimaryVRInputStates, kNumVRInputMappings, false);
		std::fill_n(gPrevSecondaryVRInputStates, kNumVRInputMappings, false);
	}
}

void VR::UpdateOverlayMenuStateFromInput()
{
	if (globals::menu == nullptr)
		return;

	bool& isEnabled = globals::menu->IsEnabled;
	bool& overlayEnabled = globals::menu->overlayVisible;
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;

	if (this->isCapturingCombo) {
		if (!isEnabled) {
			ResetMenuInputRuntimeState();
		}
		return;
	}

	if (testMode) {
		if (!isEnabled) {
			ResetMenuInputRuntimeState();
			return;
		}
		return;
	}

	bool uiMenusOpen = globals::state->isMainMenuOpen ||
	                   (globals::game::ui && globals::game::ui->IsMenuOpen(RE::TweenMenu::MENU_NAME));

	bool inValidMenuState = uiMenusOpen || (globals::game::ui && (isEnabled || overlayEnabled));

	if (!inValidMenuState)
		return;

	struct MenuStateMapping
	{
		std::function<bool()> condition;
		std::function<void()> action;
		bool allowWhenUIMenusClosed = false;
	};

	auto CheckCombo = [&](const std::vector<ButtonCombo>& combos) -> bool {
		if (combos.empty())
			return false;

		for (size_t i = 0; i < combos.size(); ++i) {
			const auto& combo = combos[i];
			bool buttonPressed = false;

			switch (combo.GetDevice()) {
			case ControllerDevice::Both:
				buttonPressed = primaryControllerState[combo.GetKey()].isPressed &&
				                secondaryControllerState[combo.GetKey()].isPressed;
				break;
			case ControllerDevice::Primary:
				buttonPressed = primaryControllerState[combo.GetKey()].isPressed;
				break;
			case ControllerDevice::Secondary:
				buttonPressed = secondaryControllerState[combo.GetKey()].isPressed;
				break;
			}

			if (!buttonPressed) {
				return false;
			}
		}

		return true;
	};

	std::vector<MenuStateMapping> mappings = {
		// Open Community Shaders menu when closed
		{ [&]() {
			 return CheckCombo(settings.VRMenuOpenKeys) && !isEnabled;
		 },
			[&]() {
				isEnabled = true;
				ResetMenuInputRuntimeState();
			} },

		// Close Community Shaders menu when open
		{ [&]() {
			 return CheckCombo(settings.VRMenuCloseKeys) && isEnabled;
		 },
			[&]() {
				isEnabled = false;
				ResetMenuInputRuntimeState();
			},
			true },

		// Open VR overlay when closed (only when CS menu is open)
		{ [&]() {
			 return CheckCombo(settings.VROverlayOpenKeys) && !overlayEnabled && isEnabled;
		 },
			[&]() { overlayEnabled = true; } },

		// Close VR overlay when open (only when CS menu is open)
		{ [&]() {
			 return CheckCombo(settings.VROverlayCloseKeys) && overlayEnabled && isEnabled;
		 },
			[&]() { overlayEnabled = false; } }
	};

	bool onlyAllowClose = isEnabled && !uiMenusOpen;

	for (const auto& mapping : mappings) {
		if (onlyAllowClose && !mapping.allowWhenUIMenusClosed)
			continue;

		if (mapping.condition()) {
			mapping.action();
			break;
		}
	}
}

void VR::ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents)
{
	bool currentLeftHandedMode = RE::BSOpenVRControllerDevice::IsLeftHandedMode();
	static bool firstCall = true;
	if (firstCall || currentLeftHandedMode != lastKnownLeftHandedMode) {
		if (!firstCall) {
			logger::debug("VR handedness changed: {} -> {}", lastKnownLeftHandedMode ? "Left" : "Right", currentLeftHandedMode ? "Left" : "Right");
		}
		firstCall = false;
		lastKnownLeftHandedMode = currentLeftHandedMode;
		primaryControllerState = {};
		secondaryControllerState = {};
	}

	double nowSecs = Util::GetNowSecs();
	for (auto& event : vrEvents) {
		bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
		bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
		struct VRButtonDescriptor
		{
			const char* name;
			bool (*isButton)(std::uint32_t);
			std::uint32_t keyCode;
		};
		static const VRButtonDescriptor kVRButtons[] = {
			{ "Grip", RE::BSOpenVRControllerDevice::IsGripButton, RE::BSOpenVRControllerDevice::Keys::kGrip },
			{ "GripAlt", RE::BSOpenVRControllerDevice::IsGripButton, RE::BSOpenVRControllerDevice::Keys::kGripAlt },
			{ "Trigger", RE::BSOpenVRControllerDevice::IsTriggerButton, RE::BSOpenVRControllerDevice::Keys::kTrigger },
			{ "Stick Click", RE::BSOpenVRControllerDevice::IsStickClick, RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger },
			{ "Touchpad Click", RE::BSOpenVRControllerDevice::IsTouchpadClick, RE::BSOpenVRControllerDevice::Keys::kTouchpadClick },
			{ "Touchpad Alt", RE::BSOpenVRControllerDevice::IsTouchpadClick, RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt },
			{ "A/X", RE::BSOpenVRControllerDevice::IsAButton, RE::BSOpenVRControllerDevice::Keys::kXA },
			{ "B/Y", RE::BSOpenVRControllerDevice::IsBButton, RE::BSOpenVRControllerDevice::Keys::kBY },
		};
		for (const auto& desc : kVRButtons) {
			if (event.keyCode == desc.keyCode) {
				RE::ButtonState* state = isPrimary ? &primaryControllerState[desc.keyCode] : isSecondary ? &secondaryControllerState[desc.keyCode] :
				                                                                                           nullptr;
				if (state) {
					state->OnEvent(event.IsPressed(), nowSecs);
				}
				break;
			}
		}
		switch (event.eventType) {
		case RE::INPUT_EVENT_TYPE::kButton:
			ProcessVRButtonEvent(event);
			break;
		case RE::INPUT_EVENT_TYPE::kThumbstick:
			UpdateControllerState(event);
			break;
		default:
			break;
		}
	}
}

void VR::ProcessVRButtonEvent(const Menu::KeyEvent& event)
{
	if (this->isCapturingCombo) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;

	if (isPrimary || isSecondary) {
		RE::ButtonMapping mappings[kNumVRInputMappings] = {
			{ RE::BSOpenVRControllerDevice::Keys::kTrigger, ImGuiMouseButton_Left, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kGrip, ImGuiMouseButton_Right, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kTouchpadClick, ImGuiMouseButton_Middle, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger, ImGuiMouseButton_Middle, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kBY, -1, true, Util::Input::VirtualKeyToImGuiKey(VK_TAB), isSecondary },
			{ RE::BSOpenVRControllerDevice::Keys::kXA, -1, true, Util::Input::VirtualKeyToImGuiKey(VK_RETURN), false },
		};

		if (gLastVRInputHandedness != lastKnownLeftHandedMode) {
			ResetVRImGuiButtonState();
			gLastVRInputHandedness = lastKnownLeftHandedMode;
		}
		bool* prevStates = isPrimary ? gPrevPrimaryVRInputStates : gPrevSecondaryVRInputStates;

		RE::InputDeviceState& controllerState = isPrimary ? primaryControllerState : secondaryControllerState;

		size_t limit = testMode ? kNumVRTriggerMappings : kNumVRInputMappings;

		for (size_t i = 0; i < limit; ++i) {
			RE::ButtonState* state = &controllerState[mappings[i].keyCode];
			bool curr = state ? state->isPressed : false;
			if (curr != prevStates[i]) {
				if (mappings[i].isKeyEvent) {
					if (mappings[i].isShift)
						io.AddKeyEvent(ImGuiMod_Shift, curr);
					io.AddKeyEvent(static_cast<ImGuiKey>(mappings[i].key), curr);
				} else {
					io.AddMouseButtonEvent(mappings[i].logicalButton, curr);
				}
				prevStates[i] = curr;
			}
		}
	}

	VRControllerEventLog logEntry;
	logEntry.device = static_cast<int>(event.device);
	logEntry.keyCode = event.keyCode;
	logEntry.value = static_cast<int>(event.value);
	logEntry.pressed = event.IsPressed();
	logEntry.heldTime = 0.0;
	logEntry.heldSource = "button";
	logEntry.thumbstickX = 0.0f;
	logEntry.thumbstickY = 0.0f;
	logEntry.controllerRole = isPrimary ? "Primary" : isSecondary ? "Secondary" :
	                                                                "Unknown";
	vrControllerEventLog.push_back(logEntry);
	if (vrControllerEventLog.size() > 32) {
		vrControllerEventLog.erase(vrControllerEventLog.begin());
	}
}

void VR::UpdateControllerState(const Menu::KeyEvent& event)
{
	bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);

	if (isPrimary) {
		primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x = event.thumbstickX;
		primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y = event.thumbstickY;
	} else if (isSecondary) {
		secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x = event.thumbstickX;
		secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y = event.thumbstickY;
	}

	VRControllerEventLog logEntry;
	logEntry.device = static_cast<int>(event.device);
	logEntry.keyCode = event.keyCode;
	logEntry.value = static_cast<int>(event.value);
	logEntry.pressed = event.IsPressed();
	logEntry.heldTime = 0.0;
	logEntry.heldSource = "thumbstick";
	logEntry.thumbstickX = event.thumbstickX;
	logEntry.thumbstickY = event.thumbstickY;
	logEntry.controllerRole = isPrimary ? "Primary" : "Secondary";
	vrControllerEventLog.push_back(logEntry);
	if (vrControllerEventLog.size() > 32) {
		vrControllerEventLog.erase(vrControllerEventLog.begin());
	}
}

void VR::ProcessThumbstickScroll(RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone, ImGuiIO& io)
{
	bool usingScrollStickX = (std::abs(controllerState.thumbsticks[thumbstickIndex].x) > deadzone);
	bool usingScrollStickY = (std::abs(controllerState.thumbsticks[thumbstickIndex].y) > deadzone);

	if (usingScrollStickX || usingScrollStickY) {
		ScrollAccum& accum = gVRScrollAccums[thumbstickIndex];

		accum.x += controllerState.thumbsticks[thumbstickIndex].x * 0.1f;
		accum.y += controllerState.thumbsticks[thumbstickIndex].y * 0.1f;

		float scrollEventX = 0.0f;
		float scrollEventY = 0.0f;

		if (std::abs(accum.x) > 0.3f) {
			scrollEventX = accum.x > 0 ? 1.0f : -1.0f;
			accum.x = 0.0f;
		}
		if (std::abs(accum.y) > 0.3f) {
			scrollEventY = accum.y > 0 ? 1.0f : -1.0f;
			accum.y = 0.0f;
		}

		if (scrollEventX != 0.0f || scrollEventY != 0.0f) {
			io.AddMouseWheelEvent(-scrollEventX, scrollEventY);
		}
	}
}

void VR::ResetComboRecordingState()
{
	isCapturingCombo = false;
	currentComboType = ComboType::None;
	currentComboName = nullptr;
	recordedCombo.clear();
	comboStartTime = 0.0;
	recordingButtonControllers.clear();
	recordingIgnoredButtons.clear();
}

void VR::ReleaseMenuImGuiInputState()
{
	ResetVRImGuiButtonState();
	gLastVRInputHandedness = lastKnownLeftHandedMode;
	gVRScrollAccums.clear();

	ImGuiIO& io = ImGui::GetIO();
	io.ClearInputKeys();
	for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
		io.AddMouseButtonEvent(button, false);
	}
	io.MouseDrawCursor = false;
	io.WantSetMousePos = false;
}

void VR::ResetMenuInputRuntimeState()
{
	ResetComboRecordingState();

	settings.VRMenuControllerDiagnosticsTestMode = false;
	overlayDragState = {};
	wandState = {};
	primaryControllerState.Clear();
	secondaryControllerState.Clear();
	ReleaseMenuImGuiInputState();
}

void VR::ProcessControllerInputForImGui()
{
	if (!globals::menu || !globals::menu->IsEnabled)
		return;
	bool testMode = settings.VRMenuControllerDiagnosticsTestMode;
	float mouseDeadzone = settings.mouseDeadzone;
	float mouseSpeed = settings.mouseSpeed;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
	io.WantSetMousePos = false;

	bool wandHandledCursor = false;
	if (!testMode && settings.EnableWandPointing) {
		UpdateCursorFromWandPointing();
		wandHandledCursor = wandState.isIntersecting;
	}

	if (!testMode) {
		bool isDragging = overlayDragState.dragging;

		if (wandHandledCursor && !isDragging) {
			ProcessThumbstickScroll(primaryControllerState, static_cast<size_t>(RE::ControllerRole::Primary), mouseDeadzone, io);
			ProcessThumbstickScroll(secondaryControllerState, static_cast<size_t>(RE::ControllerRole::Secondary), mouseDeadzone, io);
		} else if (!isDragging) {
			bool useAttachedControllerForCursor = (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
												   settings.attachMode == VR::Settings::OverlayAttachMode::Both);

			RE::VRControllerState* cursorController = nullptr;
			RE::VRControllerState* scrollController = nullptr;

			if (useAttachedControllerForCursor) {
				if (settings.VRMenuAttachController == ControllerDevice::Primary) {
					cursorController = &primaryControllerState;
					scrollController = &secondaryControllerState;
				} else {
					cursorController = &secondaryControllerState;
					scrollController = &primaryControllerState;
				}
			} else {
				cursorController = &primaryControllerState;
				scrollController = &secondaryControllerState;
			}

			if (cursorController) {
				size_t thumbstickIndex = (cursorController == &primaryControllerState) ?
				                             static_cast<size_t>(RE::ControllerRole::Primary) :
				                             static_cast<size_t>(RE::ControllerRole::Secondary);

				float thumbstickX = cursorController->thumbsticks[thumbstickIndex].x;
				float thumbstickY = cursorController->thumbsticks[thumbstickIndex].y;
				bool usingCursorStick = (std::abs(thumbstickX) > mouseDeadzone || std::abs(thumbstickY) > mouseDeadzone);

				if (usingCursorStick) {
					ImVec2 mousePos = io.MousePos;
					mousePos.x += thumbstickX * mouseSpeed;
					mousePos.y -= thumbstickY * mouseSpeed;
					mousePos.x = std::clamp(mousePos.x, 0.0f, io.DisplaySize.x);
					mousePos.y = std::clamp(mousePos.y, 0.0f, io.DisplaySize.y);
					io.MousePos = mousePos;
					io.AddMousePosEvent(mousePos.x, mousePos.y);
					io.MouseDrawCursor = true;
					io.WantSetMousePos = true;
				}
			}

			if (scrollController) {
				size_t thumbstickIndex = (scrollController == &primaryControllerState) ?
				                             static_cast<size_t>(RE::ControllerRole::Primary) :
				                             static_cast<size_t>(RE::ControllerRole::Secondary);
				ProcessThumbstickScroll(*scrollController, thumbstickIndex, mouseDeadzone, io);
			}
		}
	}
}
