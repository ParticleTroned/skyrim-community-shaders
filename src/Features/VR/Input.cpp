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

	enum class CursorOwner
	{
		Desktop,
		Wand
	};

	bool gPrevPrimaryVRInputStates[kNumVRInputMappings] = {};
	bool gPrevSecondaryVRInputStates[kNumVRInputMappings] = {};
	bool gVRMouseButtonDown[ImGuiMouseButton_COUNT] = {};
	bool gLastObservedMouseButtonDown[ImGuiMouseButton_COUNT] = {};
	bool gLastVRInputHandedness = false;
	std::unordered_map<size_t, ScrollAccum> gVRScrollAccums;
	CursorOwner gCursorOwner = CursorOwner::Desktop;
	ImVec2 gLastDesktopMousePos = ImVec2(0.0f, 0.0f);
	ImVec2 gLastControllerCursorPos = ImVec2(0.0f, 0.0f);
	bool gHasLastDesktopMousePos = false;
	bool gHasLastControllerCursorPos = false;
	bool gHasLastMenuNavigationMode = false;
	bool gLastMenuNavigationUsesWand = false;
	constexpr float kDesktopCursorMotionThresholdSq = 2.0f * 2.0f;

	void ResetVRImGuiButtonState()
	{
		std::fill_n(gPrevPrimaryVRInputStates, kNumVRInputMappings, false);
		std::fill_n(gPrevSecondaryVRInputStates, kNumVRInputMappings, false);
		std::fill_n(gVRMouseButtonDown, ImGuiMouseButton_COUNT, false);
		std::fill_n(gLastObservedMouseButtonDown, ImGuiMouseButton_COUNT, false);
	}

	bool IsThumbstickActive(const RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone)
	{
		return std::abs(controllerState.thumbsticks[thumbstickIndex].x) > deadzone ||
		       std::abs(controllerState.thumbsticks[thumbstickIndex].y) > deadzone;
	}

	bool HasUsableCursorPos(const ImVec2& pos)
	{
		return std::isfinite(pos.x) &&
		       std::isfinite(pos.y) &&
		       pos.x > -100000.0f &&
		       pos.y > -100000.0f;
	}

	size_t GetThumbstickIndexForController(const RE::VRControllerState* controllerState, const RE::VRControllerState& primaryControllerState)
	{
		return controllerState == &primaryControllerState ?
		           static_cast<size_t>(RE::ControllerRole::Primary) :
		           static_cast<size_t>(RE::ControllerRole::Secondary);
	}

	void ApplyThumbstickCursor(RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone, float mouseSpeed, ImGuiIO& io)
	{
		float thumbstickX = controllerState.thumbsticks[thumbstickIndex].x;
		float thumbstickY = controllerState.thumbsticks[thumbstickIndex].y;
		if (std::abs(thumbstickX) <= deadzone && std::abs(thumbstickY) <= deadzone)
			return;

		ImVec2 mousePos = HasUsableCursorPos(io.MousePos) ?
		                      io.MousePos :
		                      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
		mousePos.x += thumbstickX * mouseSpeed;
		mousePos.y -= thumbstickY * mouseSpeed;
		mousePos.x = std::clamp(mousePos.x, 0.0f, io.DisplaySize.x);
		mousePos.y = std::clamp(mousePos.y, 0.0f, io.DisplaySize.y);
		io.MousePos = mousePos;
		io.AddMousePosEvent(mousePos.x, mousePos.y);
		io.MouseDrawCursor = true;
		io.WantSetMousePos = true;
	}

	void ResetCursorOwnershipState()
	{
		gCursorOwner = CursorOwner::Desktop;
		gLastDesktopMousePos = ImVec2(0.0f, 0.0f);
		gLastControllerCursorPos = ImVec2(0.0f, 0.0f);
		gHasLastDesktopMousePos = false;
		gHasLastControllerCursorPos = false;
	}

	void UpdateMenuNavigationPathState(bool a_useWandNavigation)
	{
		if (!gHasLastMenuNavigationMode || gLastMenuNavigationUsesWand != a_useWandNavigation) {
			gVRScrollAccums.clear();
			ResetCursorOwnershipState();
		}

		gHasLastMenuNavigationMode = true;
		gLastMenuNavigationUsesWand = a_useWandNavigation;
	}
}

void VR::UpdateOverlayMenuStateFromInput()
{
	if (this->isCapturingCombo) {
		if (globals::menu && !globals::menu->IsEnabled) {
			ResetMenuInputRuntimeState();
		}
		return;
	}

	if (globals::menu == nullptr)
		return;

	bool& isEnabled = globals::menu->IsEnabled;
	bool& overlayEnabled = globals::menu->overlayVisible;
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;

	if (testMode) {
		if (!isEnabled) {
			ResetMenuInputRuntimeState();
			return;
		}
		return;
	}

	bool uiMenusOpen = globals::state->isMainMenuOpen ||
	                   (globals::game::ui && globals::game::ui->IsMenuOpen(RE::TweenMenu::MENU_NAME));

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

	const bool menuOpenPressed = CheckCombo(settings.VRMenuOpenKeys);
	const bool menuClosePressed = CheckCombo(settings.VRMenuCloseKeys);
	const bool overlayOpenPressed = CheckCombo(settings.VROverlayOpenKeys);
	const bool overlayClosePressed = CheckCombo(settings.VROverlayCloseKeys);
	const bool canOpenMenuFromWorld =
		!ShouldUseInSceneOverlay() &&
		openVRInfo.hasOverlayInterface &&
		settings.attachMode != AttachMode::None;
	const bool canUseMenuBindings = uiMenusOpen || isEnabled || canOpenMenuFromWorld;

	bool inValidMenuState =
		uiMenusOpen ||
		isEnabled ||
		overlayEnabled ||
		(canUseMenuBindings && (menuOpenPressed || menuClosePressed)) ||
		overlayOpenPressed ||
		overlayClosePressed;

	if (!inValidMenuState)
		return;

	struct MenuStateMapping
	{
		std::function<bool()> condition;
		std::function<void()> action;
	};

	std::vector<MenuStateMapping> mappings = {
		// Open Community Shaders menu when closed
		{ [&]() {
			 return menuOpenPressed && !isEnabled && canUseMenuBindings;
		 },
			[&]() {
				isEnabled = true;
				ResetMenuInputRuntimeState();
			} },

		// Close Community Shaders menu when open
		{ [&]() {
			 return menuClosePressed && isEnabled;
		 },
			[&]() {
				isEnabled = false;
				ResetMenuInputRuntimeState();
			} },

		// Open VR overlay when closed
		{ [&]() {
			 return overlayOpenPressed && !overlayEnabled;
		 },
			[&]() { overlayEnabled = true; } },

		// Close VR overlay when open
		{ [&]() {
			 return overlayClosePressed && overlayEnabled;
		 },
			[&]() { overlayEnabled = false; } }
	};

	for (const auto& mapping : mappings) {
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

	bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);

	if (globals::menu && globals::menu->IsEnabled && (isPrimary || isSecondary)) {
		ImGuiIO& io = ImGui::GetIO();
		bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;
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
					if (curr && !testMode && CanUseWandPointing()) {
						const ControllerDevice pointingController = GetWandPointingControllerDevice();
						const bool eventFromPointingController =
							(pointingController == ControllerDevice::Primary && isPrimary) ||
							(pointingController == ControllerDevice::Secondary && isSecondary);
						if (!eventFromPointingController) {
							continue;
						}
						UpdateCursorFromWandPointing(true);
						if (!io.WantSetMousePos || !HasUsableCursorPos(io.MousePos)) {
							continue;
						}
						gCursorOwner = CursorOwner::Wand;
						gLastControllerCursorPos = io.MousePos;
						gHasLastControllerCursorPos = true;
					}
					if (mappings[i].logicalButton >= 0 && mappings[i].logicalButton < ImGuiMouseButton_COUNT) {
						gVRMouseButtonDown[mappings[i].logicalButton] = curr;
					}
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
	bool usingScrollStickY = (std::abs(controllerState.thumbsticks[thumbstickIndex].y) > deadzone);

	if (usingScrollStickY) {
		ScrollAccum& accum = gVRScrollAccums[thumbstickIndex];
		accum.x = 0.0f;
		accum.y += controllerState.thumbsticks[thumbstickIndex].y * 0.1f;
		float scrollEventY = 0.0f;

		if (std::abs(accum.y) > 0.3f) {
			scrollEventY = accum.y > 0 ? 1.0f : -1.0f;
			accum.y = 0.0f;
		}

		if (scrollEventY != 0.0f) {
			io.AddMouseWheelEvent(0.0f, scrollEventY);
		}
	} else {
		gVRScrollAccums[thumbstickIndex] = {};
	}
}

void VR::ProcessThumbstickScrollMouseNavigation(RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone, ImGuiIO& io)
{
	bool usingScrollStickX = (std::abs(controllerState.thumbsticks[thumbstickIndex].x) > deadzone);
	bool usingScrollStickY = (std::abs(controllerState.thumbsticks[thumbstickIndex].y) > deadzone);

	if (usingScrollStickX || usingScrollStickY) {
		ScrollAccum& accum = gVRScrollAccums[thumbstickIndex];

		if (usingScrollStickX) {
			accum.x += controllerState.thumbsticks[thumbstickIndex].x * 0.1f;
		} else {
			accum.x = 0.0f;
		}

		if (usingScrollStickY) {
			accum.y += controllerState.thumbsticks[thumbstickIndex].y * 0.1f;
		} else {
			accum.y = 0.0f;
		}

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
	} else {
		gVRScrollAccums[thumbstickIndex] = {};
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
}

void VR::ReleaseMenuImGuiInputState()
{
	ResetVRImGuiButtonState();
	gLastVRInputHandedness = lastKnownLeftHandedMode;
	gVRScrollAccums.clear();
	gHasLastMenuNavigationMode = false;
	gLastMenuNavigationUsesWand = false;

	ImGuiIO& io = ImGui::GetIO();
	io.ClearInputKeys();
	for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
		io.AddMouseButtonEvent(button, false);
	}
	io.MouseDrawCursor = false;
	io.WantSetMousePos = false;
	customVRCursorVisible = false;
	customVRCursorPos = ImVec2(-FLT_MAX, -FLT_MAX);
	ResetCursorOwnershipState();
}

void VR::ResetMenuInputRuntimeState()
{
	ResetComboRecordingState();

	settings.VRMenuControllerDiagnosticsTestMode = false;
	overlayDragState = {};
	ResetWandPointingRuntimeState();
	primaryControllerState = {};
	secondaryControllerState = {};
	ReleaseMenuImGuiInputState();
}

void VR::ProcessControllerInputForWandPointingPath(bool testMode, float mouseDeadzone, ImGuiIO& io)
{
	const ImVec2 desktopBaselineMousePos = io.MousePos;
	const bool desktopCursorUsable = HasUsableCursorPos(desktopBaselineMousePos);
	bool desktopMouseMoved = false;
	bool desktopMouseClicked = false;
	if (desktopCursorUsable) {
		if (gCursorOwner == CursorOwner::Wand && gHasLastControllerCursorPos) {
			const float dx = desktopBaselineMousePos.x - gLastControllerCursorPos.x;
			const float dy = desktopBaselineMousePos.y - gLastControllerCursorPos.y;
			desktopMouseMoved = (dx * dx + dy * dy) > kDesktopCursorMotionThresholdSq;
		} else if (gHasLastDesktopMousePos) {
			const float dx = desktopBaselineMousePos.x - gLastDesktopMousePos.x;
			const float dy = desktopBaselineMousePos.y - gLastDesktopMousePos.y;
			desktopMouseMoved = (dx * dx + dy * dy) > kDesktopCursorMotionThresholdSq;
		} else {
			desktopMouseMoved = true;
		}
	}
	for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
		const bool mouseButtonDown = io.MouseDown[button];
		if (desktopCursorUsable &&
			mouseButtonDown &&
			!gLastObservedMouseButtonDown[button] &&
			!gVRMouseButtonDown[button]) {
			desktopMouseClicked = true;
		}
		gLastObservedMouseButtonDown[button] = mouseButtonDown;
	}

	if (desktopCursorUsable && (gCursorOwner == CursorOwner::Desktop || desktopMouseMoved || desktopMouseClicked)) {
		gLastDesktopMousePos = desktopBaselineMousePos;
		gHasLastDesktopMousePos = true;
	} else {
		gHasLastDesktopMousePos = false;
	}

	const bool thumbstickScrollActive =
		IsThumbstickActive(primaryControllerState, static_cast<size_t>(RE::ControllerRole::Primary), mouseDeadzone) ||
		IsThumbstickActive(secondaryControllerState, static_cast<size_t>(RE::ControllerRole::Secondary), mouseDeadzone);

	if (desktopMouseMoved || desktopMouseClicked) {
		gCursorOwner = CursorOwner::Desktop;
	}

	const bool useWandPointing = CanUseWandPointing() && !testMode;
	const bool wandCursorStateActive =
		gCursorOwner == CursorOwner::Wand ||
		gHasLastControllerCursorPos ||
		wandState.isIntersecting ||
		wandState.isActivelyDrivingCursor;
	if (!useWandPointing && wandCursorStateActive) {
		ResetWandPointingRuntimeState();
		ResetCursorOwnershipState();
	}

	bool wandHandledCursor = false;
	if (useWandPointing) {
		UpdateCursorFromWandPointing(false);
		wandHandledCursor = wandState.isIntersecting;
		const bool canAdoptWandCursor =
			io.WantSetMousePos &&
			HasUsableCursorPos(io.MousePos) &&
			((wandState.isActivelyDrivingCursor && !desktopMouseMoved && !desktopMouseClicked) ||
				gCursorOwner == CursorOwner::Wand);
		if (canAdoptWandCursor) {
			gCursorOwner = CursorOwner::Wand;
			gLastControllerCursorPos = io.MousePos;
			gHasLastControllerCursorPos = true;
		}
	} else {
		wandState.isIntersecting = false;
		wandState.isActivelyDrivingCursor = false;
	}

	if (!testMode) {
		bool isDragging = overlayDragState.dragging;

		if (wandHandledCursor && !isDragging) {
			ProcessThumbstickScroll(primaryControllerState, static_cast<size_t>(RE::ControllerRole::Primary), mouseDeadzone, io);
			ProcessThumbstickScroll(secondaryControllerState, static_cast<size_t>(RE::ControllerRole::Secondary), mouseDeadzone, io);
		} else if (!isDragging && thumbstickScrollActive) {
			bool useAttachedControllerForCursor = (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
												   settings.attachMode == VR::Settings::OverlayAttachMode::Both);

			RE::VRControllerState* scrollController = nullptr;

			if (useAttachedControllerForCursor) {
				if (settings.VRMenuAttachController == ControllerDevice::Primary) {
					scrollController = &secondaryControllerState;
				} else {
					scrollController = &primaryControllerState;
				}
			} else {
				scrollController = &secondaryControllerState;
			}

			if (scrollController) {
				size_t thumbstickIndex = (scrollController == &primaryControllerState) ?
				                             static_cast<size_t>(RE::ControllerRole::Primary) :
				                             static_cast<size_t>(RE::ControllerRole::Secondary);
				ProcessThumbstickScroll(*scrollController, thumbstickIndex, mouseDeadzone, io);
			}
		}
	}

	if (desktopCursorUsable && gCursorOwner == CursorOwner::Desktop) {
		wandState.isIntersecting = false;
		wandState.isActivelyDrivingCursor = false;
		io.MousePos = desktopBaselineMousePos;
		io.AddMousePosEvent(desktopBaselineMousePos.x, desktopBaselineMousePos.y);
		io.WantSetMousePos = true;
		customVRCursorVisible = true;
		customVRCursorPos = desktopBaselineMousePos;
		customVRCursorOverlayType = settings.attachMode == AttachMode::ControllerOnly ? OverlayType::Controller : OverlayType::HMD;
	} else if (gHasLastControllerCursorPos && gCursorOwner == CursorOwner::Wand) {
		io.MousePos = gLastControllerCursorPos;
		io.AddMousePosEvent(gLastControllerCursorPos.x, gLastControllerCursorPos.y);
		io.WantSetMousePos = true;
	}
}

void VR::ProcessControllerInputForMouseNavigationPath(bool testMode, float mouseDeadzone, float mouseSpeed, ImGuiIO& io)
{
	wandState.isIntersecting = false;
	wandState.isActivelyDrivingCursor = false;

	if (!testMode && !overlayDragState.dragging) {
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
			const size_t thumbstickIndex = GetThumbstickIndexForController(cursorController, primaryControllerState);
			ApplyThumbstickCursor(*cursorController, thumbstickIndex, mouseDeadzone, mouseSpeed, io);
		}

		if (scrollController) {
			const size_t thumbstickIndex = GetThumbstickIndexForController(scrollController, primaryControllerState);
			ProcessThumbstickScrollMouseNavigation(*scrollController, thumbstickIndex, mouseDeadzone, io);
		}
	}

	// The VR menu renderer suppresses ImGui's native mouse cursor in the HMD pass,
	// so the mouse-navigation path still needs to surface its resolved cursor position here.
	if (HasUsableCursorPos(io.MousePos)) {
		customVRCursorVisible = true;
		customVRCursorPos = io.MousePos;
		customVRCursorOverlayType = settings.attachMode == AttachMode::ControllerOnly ? OverlayType::Controller : OverlayType::HMD;
	}
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
	io.MouseDrawCursor = false;
	io.WantSetMousePos = false;
	customVRCursorVisible = false;
	customVRCursorPos = ImVec2(-FLT_MAX, -FLT_MAX);
	customVRCursorOverlayType = OverlayType::HMD;

	const bool useWandNavigation = CanUseWandPointing();
	UpdateMenuNavigationPathState(useWandNavigation);

	if (useWandNavigation) {
		ProcessControllerInputForWandPointingPath(testMode, mouseDeadzone, io);
	} else {
		ProcessControllerInputForMouseNavigationPath(testMode, mouseDeadzone, mouseSpeed, io);
	}
}
