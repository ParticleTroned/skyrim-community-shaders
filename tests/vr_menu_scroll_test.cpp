#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

namespace vr
{
	enum EVRInitError
	{
		VRInitError_None,
		VRInitError_Unknown
	};
	constexpr auto IVRSystem_Version = "IVRSystem_019";
	void* VR_GetGenericInterface(const char*, EVRInitError*);
	using TrackedDeviceIndex_t = uint32_t;
	constexpr auto k_unTrackedDeviceIndexInvalid = ~TrackedDeviceIndex_t{};
	constexpr uint32_t k_unMaxTrackedDeviceCount = 4;
	enum ETrackingUniverseOrigin
	{
		TrackingUniverseStanding
	};
	enum EVRCompositorError
	{
		VRCompositorError_None,
		VRCompositorError_RequestFailed
	};
	constexpr uint32_t k_EButton_SteamVR_Trigger = 33;
	constexpr uint64_t ButtonMaskFromId(uint32_t button) { return uint64_t{ 1 } << button; }
	struct TrackedDevicePose_t
	{
		bool bPoseIsValid = true, bDeviceIsConnected = true;
	};
	struct VRControllerState_t
	{
		uint64_t ulButtonPressed = 0;
		std::array<ImVec2, 5> rAxis{};
	};
}

enum class ControllerDevice
{
	Primary,
	Secondary
};

namespace Runtime
{
	struct System
	{
		uint32_t reads = 0;
		std::array<vr::VRControllerState_t, vr::k_unMaxTrackedDeviceCount> states{};
		std::array<bool, vr::k_unMaxTrackedDeviceCount> readable{ true, true, true, true };
		bool GetControllerState(vr::TrackedDeviceIndex_t index, vr::VRControllerState_t* out, uint32_t size)
		{
			Require(index < states.size() && size == sizeof(*out), "controller reads must use valid indices and state size");
			++reads;
			*out = states[index];
			return readable[index];
		}
	} system;
	bool available = true, posesAvailable = true, leftHanded = false;
	bool isVR = true, exportAvailable = true, interfaceAvailable = true, interfaceError = false;
	bool compositorAvailable = true;
	std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
	struct Compositor
	{
		uint32_t reads = 0;
		vr::EVRCompositorError GetLastPoseForTrackedDeviceIndex(vr::TrackedDeviceIndex_t index, vr::TrackedDevicePose_t* out, std::nullptr_t)
		{
			Require(index < poses.size(), "invalid controller indices must not reach the compositor");
			++reads;
			*out = poses[index];
			return posesAvailable ? vr::VRCompositorError_None : vr::VRCompositorError_RequestFailed;
		}
	} compositor;
	std::array<vr::TrackedDeviceIndex_t, 2> indices{ 2, 3 };
	void Reset(bool handedness)
	{
		system = {};
		compositor = {};
		available = posesAvailable = true;
		compositorAvailable = true;
		isVR = exportAvailable = interfaceAvailable = true;
		interfaceError = false;
		leftHanded = handedness;
		poses = {};
		indices = { 2, 3 };
	}
	size_t PhysicalHand(size_t role) { return role ^ static_cast<size_t>(leftHanded); }
	ImVec2& Stick(size_t role) { return system.states[indices[PhysicalHand(role)]].rAxis[0]; }
	void* GetGenericInterface(const char* version, vr::EVRInitError* error)
	{
		Require(std::strcmp(version, vr::IVRSystem_Version) == 0, "runtime reads must request the compiled OpenVR ABI");
		*error = interfaceError ? vr::VRInitError_Unknown : vr::VRInitError_None;
		return interfaceAvailable ? &system : nullptr;
	}
}

namespace vr
{
	using IVRSystem = Runtime::System;
}

namespace REL
{
	struct Module
	{
		static bool IsVR() { return Runtime::isVR; }
	};
}

namespace REX::W32
{
	Runtime::System* GetModuleHandleA(const char* name)
	{
		Require(std::strcmp(name, "openvr_api.dll") == 0, "passive reads must resolve the loaded OpenVR runtime");
		return Runtime::available ? &Runtime::system : nullptr;
	}

	auto GetProcAddress(Runtime::System*, const char* name) -> decltype(&Runtime::GetGenericInterface)
	{
		Require(std::strcmp(name, "VR_GetGenericInterface") == 0, "passive reads must use the runtime export");
		return Runtime::exportAvailable ? Runtime::GetGenericInterface : nullptr;
	}
}

namespace RE
{
	enum class ControllerRole
	{
		Primary,
		Secondary
	};
	struct BSOpenVRControllerDevice
	{
		static bool IsLeftHandedMode() { return Runtime::leftHanded; }
		static bool IsPrimaryController(size_t device) { return device == Runtime::PhysicalHand(0); }
		static bool IsSecondaryController(size_t device) { return device == Runtime::PhysicalHand(1); }
	};
	struct BSOpenVR
	{
		static Runtime::Compositor* GetIVRCompositor() { return Runtime::compositorAvailable ? &Runtime::compositor : nullptr; }
		static Runtime::System* GetIVRSystem()
		{
			throw std::runtime_error("render-thread controller reads must not dispatch the game's input hooks");
		}
	};
	struct VRControllerState
	{
		std::array<ImVec2, 2> thumbsticks{};
		struct Button
		{
			bool isPressed = false;
		};
		std::array<Button, 2> buttons{};
		const Button& operator[](size_t key) const { return buttons.at(key); }
	};
}

namespace Util
{
#include "ui_vr_runtime_state.h"
	vr::TrackedDeviceIndex_t GetControllerIndexForDevice(ControllerDevice role, bool leftHanded)
	{
		return Runtime::indices[static_cast<size_t>(role) ^ static_cast<size_t>(leftHanded)];
	}
	bool GetDeviceToAbsoluteTrackingPoseCompatible(vr::ETrackingUniverseOrigin, float, vr::TrackedDevicePose_t* out, uint32_t count)
	{
		Require(count == Runtime::poses.size(), "pose reads must use the complete compatible snapshot");
		std::copy_n(Runtime::poses.begin(), count, out);
		return Runtime::posesAvailable;
	}
}

struct Menu
{
	struct KeyEvent
	{
		size_t device;
		float thumbstickX, thumbstickY;
		int keyCode = 0;
		float value = 0;
		bool IsPressed() const { return value > 0; }
	};
};

struct VR
{
	struct VRControllerEventLog
	{
		int device, keyCode, value;
		bool pressed;
		double heldTime;
		std::string heldSource;
		float thumbstickX, thumbstickY;
		std::string controllerRole;
	};
	struct Settings
	{
		float mouseDeadzone = .15f;
	} settings;
	std::vector<VRControllerEventLog> vrControllerEventLog;
	RE::VRControllerState primaryControllerState, secondaryControllerState;
	bool lastKnownLeftHandedMode = false;
	bool customVRCursorVisible = false;
	ImVec2 customVRCursorPos{};
	void ReleaseOCUPointerState() {}
	void ReleaseMenuImGuiInputState();
	void PollThumbstickInput();
	void UpdateControllerState(const Menu::KeyEvent&);
	void ProcessThumbstickScroll(RE::VRControllerState&, size_t, float, ImGuiIO&);
	void ProcessThumbstickScrollMouseNavigation(RE::VRControllerState&, size_t, float, ImGuiIO&);
};

namespace
{
#include "ui_vr_cursor_delta.h"
#include "ui_vr_scroll_types.h"
	std::unordered_map<size_t, ScrollAccum> gVRScrollAccums;
#include "ui_vr_scroll_refresh.h"
	bool gWandActivationPending = false;
	bool gHasOCUPointerInputSource = false;
	bool gUsingOCUPointerInput = false;
	bool gHasLastMenuNavigationMode = false;
	bool gLastMenuNavigationUsesWand = false;
	std::array<bool, 2> gBlockRawTriggerUntilRelease{};
	bool gPrevPrimaryVRInputStates[2]{};
	bool gPrevSecondaryVRInputStates[2]{};
#include "ui_vr_trigger_release.h"
	struct Mapping
	{
		size_t keyCode;
	};
	std::array<Mapping, 2> GetVRInputMappings(bool secondary)
	{
		return secondary ? std::array{ Mapping{ 1 }, Mapping{ 0 } } : std::array{ Mapping{ 0 }, Mapping{ 1 } };
	}
	void CancelWandMouseInput(VR&) {}
	void ResetCursorOwnershipState() {}
}

// Runtime services are substituted; production polling, reset and scrolling use real ImGui.
#include "ui_vr_input_release.h"
#include "ui_vr_scroll.h"

void VR::PollThumbstickInput()
{
#include "ui_vr_scroll_poll.h"
}

struct ScrollFixture
{
	std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{ ImGui::CreateContext(), ImGui::DestroyContext };
	VR vr;
	size_t hand;
	bool mouseNavigation;
	ImGuiWindow* body = nullptr;

	ScrollFixture(bool mouse, size_t role, bool trickle, bool leftHanded) : hand(role), mouseNavigation(mouse)
	{
		Runtime::Reset(leftHanded);
		gVRScrollAccums.clear();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = { 1024, 768 };
		io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
		io.ConfigInputTrickleEventQueue = trickle;
		io.Fonts->AddFontDefault();
		io.Fonts->Build();
		io.AddMousePosEvent(100, 100);
		Frames(2);
		Require(body->ScrollMax.x > 0 && body->ScrollMax.y > 0, "fixture must scroll on both axes");
	}
	RE::VRControllerState& Controller() { return hand == 0 ? vr.primaryControllerState : vr.secondaryControllerState; }
	void Event(float x, float y) { vr.UpdateControllerState({ Runtime::PhysicalHand(hand), x, y }); }
	void Stick(float x, float y)
	{
		Runtime::Stick(hand) = { x, y };
		Event(x, y);
	}
	void Frame(bool dispatchScroll = true)
	{
		vr.PollThumbstickInput();
		auto& io = ImGui::GetIO();
		if (dispatchScroll && mouseNavigation)
			vr.ProcessThumbstickScrollMouseNavigation(Controller(), hand, vr.settings.mouseDeadzone, io);
		else if (dispatchScroll)
			vr.ProcessThumbstickScroll(Controller(), hand, vr.settings.mouseDeadzone, io);
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 20, 20 });
		ImGui::SetNextWindowSize({ 400, 300 });
		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoSavedSettings);
		ImGui::BeginChild("Body", {}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		body = ImGui::GetCurrentWindow();
		for (int item = 0; item < 200; ++item) {
			ImGui::PushID(item);
			ImGui::Button("Setting", { 650, 30 });
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::End();
		ImGui::Render();
	}
	void Frames(int count)
	{
		for (int i = 0; i < count; ++i) Frame();
	}
	void Key(ImGuiKey key)
	{
		ImGui::GetIO().AddKeyEvent(key, true);
		Frame();
		ImGui::GetIO().AddKeyEvent(key, false);
		Frames(2);
	}
	void RequireIdle()
	{
		const auto stoppedAt = body->Scroll;
		Frames(30);
		Require(body->Scroll.x == stoppedAt.x && body->Scroll.y == stoppedAt.y,
			"menu must stay still without current physical stick input");
		ImVec2 cursor{ 200, 200 };
		Require(!ApplyThumbstickCursorDelta(Controller(), hand, vr.settings.mouseDeadzone, 10, cursor, ImGui::GetIO().DisplaySize),
			"inactive input must also stop cursor motion");
	}
};

static void CheckNavigation(ScrollFixture& f)
{
	// The physical stick returned to centre without delivering a neutral event.
	f.Event(1, -1);
	f.Controller().buttons[0].isPressed = true;
	f.vr.ReleaseMenuImGuiInputState();
	f.RequireIdle();
	Require(f.Controller().buttons[0].isPressed, "menu resets must preserve held shortcut buttons");
	for (bool secondary : { false, true }) {
		const auto mappings = GetVRInputMappings(secondary);
		const auto& physical = secondary ? f.vr.secondaryControllerState : f.vr.primaryControllerState;
		const auto* previous = secondary ? gPrevSecondaryVRInputStates : gPrevPrimaryVRInputStates;
		for (size_t index = 0; index < mappings.size(); ++index)
			Require(previous[index] == physical[mappings[index].keyCode].isPressed, "reset must latch physical held buttons in mapping order");
	}

	f.Stick(0, -1);
	f.Frames(12);
	Require(f.body->Scroll.y > 0, "held input must scroll without repeated thumbstick events");
	f.Stick(0, 0);
	f.Frame();
	f.RequireIdle();
	for (bool enterBody : { false, true }) {
		if (enterBody)
			f.Key(ImGuiKey_Enter);
		f.Key(ImGuiKey_Tab);
		Require(GImGui->NavId != 0 && GImGui->NavCursorVisible && (!enterBody || GImGui->NavWindow == f.body),
			"Tab must focus the requested window before scrolling");
		const float afterTab = f.body->Scroll.y;
		f.Stick(0, -1);
		f.Frames(12);
		Require(f.body->Scroll.y > afterTab, "Tab must not disable controller scrolling");
		Runtime::Stick(f.hand) = {};
		f.vr.ReleaseMenuImGuiInputState();
		f.RequireIdle();
	}
	f.Stick(1, 0);
	f.Frames(12);
	Require(f.mouseNavigation ? f.body->Scroll.x > 0 : f.body->Scroll.x == 0, "horizontal scrolling must retain the navigation policy");
	ImVec2 cursor{ 200, 200 };
	Require(ApplyThumbstickCursorDelta(f.Controller(), f.hand, .15f, 10, cursor, ImGui::GetIO().DisplaySize) && cursor.x > 200,
		"current input must still move the cursor");
	Runtime::Stick(f.hand) = {};
	f.vr.ReleaseMenuImGuiInputState();
	f.RequireIdle();
}

static void CheckLostEvents(ScrollFixture& f)
{
	f.Stick(0, -1);
	f.Frames(60);
	const float heldAt = f.body->Scroll.y;
	f.Frames(30);
	Require(f.body->Scroll.y > heldAt, "unchanged held input must not time out without repeated events");
	Runtime::Stick(f.hand) = {};
	Runtime::Stick(1 - f.hand) = { 0, 1 };
	f.RequireIdle();
	const auto& other = f.hand == 0 ? f.vr.secondaryControllerState : f.vr.primaryControllerState;
	Require(other.thumbsticks[1 - f.hand].y == 1 && f.Controller().buttons[0].isPressed,
		"a missing neutral event must not clear the other hand or shortcut buttons");

	// A delayed queue batch after a render stall must not restore old movement.
	f.Event(1, -1);
	f.RequireIdle();
	Runtime::Stick(f.hand) = { 0, 1 };
	const float resumeAt = f.body->Scroll.y;
	f.Frames(12);
	Require(f.body->Scroll.y < resumeAt, "physical input must resume without an engine thumbstick event");
	Runtime::Stick(f.hand) = {};
	Runtime::Stick(1 - f.hand) = {};
	f.Frame();
}

static void CheckPartialSteps(ScrollFixture& f)
{
	f.Stick(0, -.75f);
	f.Frames(3);
	f.vr.ReleaseMenuImGuiInputState();
	const float beforeReset = f.body->Scroll.y;
	f.Frames(2);
	Require(f.body->Scroll.y == beforeReset, "reset must discard partial scrolling");
	Runtime::Stick(f.hand) = { .15f, -.15f };
	f.RequireIdle();

	f.Stick(0, -.75f);
	f.Frames(3);
	Runtime::Stick(f.hand) = {};
	f.Event(1, -1);
	f.Frame(false);
	const float beforeNeutral = f.body->Scroll.y;
	f.Stick(0, -.75f);
	f.Frames(2);
	Require(f.body->Scroll.y == beforeNeutral, "a lost neutral event must discard partial scrolling");

	f.Stick(0, .75f);
	f.Frames(3);
	const float beforeReverse = f.body->Scroll.y;
	f.Stick(0, -.75f);
	f.Frames(5);
	Require(f.body->Scroll.y > beforeReverse, "direction reversal must not inherit the opposite partial step");
	Runtime::Stick(f.hand) = {};
	f.Frame();

	f.Stick(0, -.75f);
	f.Frames(3);
	Runtime::Stick(1 - f.hand) = { 0, -.75f };
	Runtime::leftHanded = !Runtime::leftHanded;
	const float beforeRemap = f.body->Scroll.y;
	f.Frames(2);
	Require(f.body->Scroll.y == beforeRemap, "handedness changes must not transfer partial steps between devices");
	Runtime::Stick(f.hand) = Runtime::Stick(1 - f.hand) = {};
	f.Frame();
}

static void CheckUnavailableInput(ScrollFixture& f)
{
	const auto physical = Runtime::PhysicalHand(f.hand);
	const auto index = Runtime::indices[physical];
	for (int failure = 0; failure < 7; ++failure) {
		f.Stick(0, -.75f);
		f.Frames(3);
		switch (failure) {
		case 0:
			Runtime::available = false;
			break;
		case 1:
			Runtime::posesAvailable = false;
			break;
		case 2:
			Runtime::poses[index].bPoseIsValid = false;
			break;
		case 3:
			Runtime::poses[index].bDeviceIsConnected = false;
			break;
		case 4:
			Runtime::system.readable[index] = false;
			break;
		case 5:
			Runtime::indices[physical] = vr::k_unTrackedDeviceIndexInvalid;
			break;
		case 6:
			Runtime::indices[physical] = vr::k_unMaxTrackedDeviceCount;
			break;
		}
		f.RequireIdle();
		Runtime::available = Runtime::posesAvailable = true;
		Runtime::poses[index] = {};
		Runtime::system.readable[index] = true;
		Runtime::indices[physical] = index;
		const float beforeRecovery = f.body->Scroll.y;
		f.Frames(2);
		Require(f.body->Scroll.y == beforeRecovery, "input failure must discard partial steps before recovery");
		Runtime::Stick(f.hand) = {};
		f.Frame();
	}
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1.01f, -1.01f }) {
		for (bool vertical : { false, true }) {
			Runtime::Stick(f.hand) = vertical ? ImVec2{ .75f, invalid } : ImVec2{ invalid, -.75f };
			f.Event(1, -1);
			f.RequireIdle();
		}
	}
}

static void CheckTriggerReleaseReads(bool leftHanded)
{
	const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{ ImGui::CreateContext(), ImGui::DestroyContext };
	ImGui::GetIO().IniFilename = nullptr;
	Runtime::Reset(leftHanded);
	VR vrFeature;
	vrFeature.lastKnownLeftHandedMode = leftHanded;
	for (auto* controller : { &vrFeature.primaryControllerState, &vrFeature.secondaryControllerState })
		for (auto& button : controller->buttons)
			button.isPressed = true;
	const auto primaryIndex = Runtime::indices[Runtime::PhysicalHand(0)];
	const auto secondaryIndex = Runtime::indices[Runtime::PhysicalHand(1)];
	for (auto index : { primaryIndex, secondaryIndex }) {
		Runtime::system.states[index].rAxis[1].x = .8f;
		Runtime::system.states[index].ulButtonPressed = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
	}
	// A closed menu rearms the gate on every frame that processes gameplay input.
	for (int frame = 0; frame < 32; ++frame) {
		vrFeature.ReleaseMenuImGuiInputState();
		ObserveRawTriggerReleases(vrFeature);
		Require(gBlockRawTriggerUntilRelease[0] && gBlockRawTriggerUntilRelease[1] &&
					gPrevPrimaryVRInputStates[0] && gPrevSecondaryVRInputStates[0],
			"repeated closed-menu input resets must retain held triggers without dispatching game hooks");
	}
	Require(Runtime::system.reads == 64, "both physical controllers must be sampled through the runtime");
	auto& primary = Runtime::system.states[primaryIndex];
	primary.rAxis[1].x = 0;
	ObserveRawTriggerReleases(vrFeature);
	Require(gBlockRawTriggerUntilRelease[0], "digital trigger press must retain the gate after analog release");
	primary.ulButtonPressed = 0;
	primary.rAxis[1].x = .3f;
	ObserveRawTriggerReleases(vrFeature);
	Require(gBlockRawTriggerUntilRelease[0], "analog trigger threshold must retain the gate without a digital press");
	primary.rAxis[1].x = .299f;
	ObserveRawTriggerReleases(vrFeature);
	Require(!gBlockRawTriggerUntilRelease[0] && !gPrevPrimaryVRInputStates[0] &&
				gBlockRawTriggerUntilRelease[1] && gPrevSecondaryVRInputStates[0],
		"a physical release must clear only its own controller gate");
	Runtime::system.states[secondaryIndex] = {};
	ObserveRawTriggerReleases(vrFeature);
	Require(!gBlockRawTriggerUntilRelease[1] && !gPrevSecondaryVRInputStates[0] &&
				gPrevPrimaryVRInputStates[1] && gPrevSecondaryVRInputStates[1],
		"trigger release must preserve other held menu buttons");
	const auto readsAfterRelease = Runtime::system.reads + Runtime::compositor.reads;
	ObserveRawTriggerReleases(vrFeature);
	Require(Runtime::system.reads + Runtime::compositor.reads == readsAfterRelease, "released gates must not issue extra runtime queries");

	for (int failure = 0; failure < 17; ++failure) {
		Runtime::Reset(leftHanded);
		vrFeature.ReleaseMenuImGuiInputState();
		gBlockRawTriggerUntilRelease[1] = false;
		switch (failure) {
		case 0:
			Runtime::isVR = false;
			break;
		case 1:
			Runtime::available = false;
			break;
		case 2:
			Runtime::exportAvailable = false;
			break;
		case 3:
			Runtime::interfaceAvailable = false;
			break;
		case 4:
			Runtime::interfaceError = true;
			break;
		case 5:
			Runtime::system.readable[primaryIndex] = false;
			break;
		case 6:
			Runtime::indices[Runtime::PhysicalHand(0)] = vr::k_unTrackedDeviceIndexInvalid;
			break;
		case 7:
			Runtime::indices[Runtime::PhysicalHand(0)] = vr::k_unMaxTrackedDeviceCount;
			break;
		case 8:
			Runtime::compositorAvailable = false;
			break;
		case 9:
			Runtime::posesAvailable = false;
			break;
		case 10:
			Runtime::poses[primaryIndex].bPoseIsValid = false;
			break;
		case 11:
			Runtime::poses[primaryIndex].bDeviceIsConnected = false;
			break;
		case 12:
			Runtime::system.states[primaryIndex].rAxis[1].x = std::numeric_limits<float>::quiet_NaN();
			break;
		case 13:
			Runtime::system.states[primaryIndex].rAxis[1].x = std::numeric_limits<float>::infinity();
			break;
		case 14:
			Runtime::system.states[primaryIndex].rAxis[1].x = -.01f;
			break;
		case 15:
			Runtime::system.states[primaryIndex].rAxis[1].x = 1.01f;
			break;
		case 16:
			Runtime::system.states[primaryIndex].rAxis[1].x = -std::numeric_limits<float>::infinity();
			break;
		}
		ObserveRawTriggerReleases(vrFeature);
		Require(gBlockRawTriggerUntilRelease[0] && gPrevPrimaryVRInputStates[0], "failed or invalid reads must not infer a trigger release");
		if (failure == 6 || failure == 7)
			Require(Runtime::compositor.reads == 0, "invalid indices must be rejected before any compositor query");
		Runtime::Reset(leftHanded);
		ObserveRawTriggerReleases(vrFeature);
		Require(!gBlockRawTriggerUntilRelease[0] && !gPrevPrimaryVRInputStates[0], "a verified neutral sample must release the gate after recovery");
	}
}

static void CheckPassiveRuntimeReads()
{
	for (int failure = 0; failure < 7; ++failure) {
		Runtime::Reset(false);
		Runtime::Stick(0) = { .5f, -.75f };
		auto index = Runtime::indices[0];
		vr::VRControllerState_t state{};
		state.ulButtonPressed = 0x123456789ABCDEF0;
		for (size_t axis = 0; axis < state.rAxis.size(); ++axis)
			state.rAxis[axis] = { static_cast<float>(axis) + .1f, static_cast<float>(axis) + .2f };
		const auto original = state;
		switch (failure) {
		case 0:
			Runtime::isVR = false;
			break;
		case 1:
			Runtime::available = false;
			break;
		case 2:
			Runtime::exportAvailable = false;
			break;
		case 3:
			Runtime::interfaceAvailable = false;
			break;
		case 4:
			Runtime::interfaceError = true;
			break;
		case 5:
			Runtime::system.readable[index] = false;
			break;
		case 6:
			index = vr::k_unMaxTrackedDeviceCount;
			break;
		}
		Require(!Util::TryGetRuntimeControllerState(index, state), "unavailable passive reads must fail without using the game's proxy");
		Require(state.ulButtonPressed == original.ulButtonPressed, "failed reads must preserve the caller's buttons");
		for (size_t axis = 0; axis < state.rAxis.size(); ++axis)
			Require(state.rAxis[axis].x == original.rAxis[axis].x && state.rAxis[axis].y == original.rAxis[axis].y,
				"failed reads must preserve all caller axes");
		Runtime::Reset(false);
		Runtime::Stick(0) = { .5f, -.75f };
		Require(Util::TryGetRuntimeControllerState(Runtime::indices[0], state) && state.rAxis[0].x == .5f && state.rAxis[0].y == -.75f,
			"passive reads must recover after runtime failures and retain current physical input");
	}
}

int main()
{
	try {
		CheckPassiveRuntimeReads();
		for (bool leftHanded : { false, true })
			CheckTriggerReleaseReads(leftHanded);
		for (bool mouseNavigation : { false, true })
			for (size_t hand : { 0u, 1u })
				for (bool trickleEvents : { false, true })
					for (bool leftHanded : { false, true }) {
						ScrollFixture fixture(mouseNavigation, hand, trickleEvents, leftHanded);
						CheckNavigation(fixture);
						CheckLostEvents(fixture);
						CheckPartialSteps(fixture);
						CheckUnavailableInput(fixture);
					}
		std::cout << "VR menu scrolling passed (16 navigation/hand/queue/handedness combinations)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
