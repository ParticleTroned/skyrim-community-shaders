#include "Features/VR/WandInteractionPolicy.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>

enum class ControllerDevice
{
	Primary,
	Secondary,
	Both
};
enum class CursorOwner
{
	Desktop,
	Wand
};
namespace RE
{
	enum class ControllerRole
	{
		Primary,
		Secondary
	};
	struct VRControllerState
	{};
}
struct VR
{
	struct Settings
	{
		enum class OverlayAttachMode
		{
			None,
			HMDOnly,
			ControllerOnly,
			Both
		};
	};
	enum class OverlayType
	{
		HMD,
		Controller
	};
	struct Hand
	{
		bool isIntersecting = false, moved = false, poseValid = false, hasScreenPosition = false;
		bool ocuOwnsInput = false, usingOCUPointer = false, usingAimComponent = false, usingPresentedSurface = true;
		float hitDistance = 1;
		ImVec2 screenPosition{ 100, 100 }, uvCoordinates{};
		OverlayType overlayType = OverlayType::HMD;
		int controllerIndex = 0, rayOrigin = 0, rayDirection = 0;
	};
	using WandHandState = Hand;
	struct
	{
		bool isIntersecting = false, isActivelyDrivingCursor = false;
		bool usingOCUPointer = false, usingAimComponent = false, usingPresentedSurface = false;
		std::uint64_t ocuFrameId = 0;
		ImVec2 uvCoordinates{};
		OverlayType overlayType = OverlayType::HMD;
		int controllerIndex = 0, rayOrigin = 0, rayDirection = 0;
	} wandState;
	std::array<Hand, 2> wandHandStates{};
	bool ocuPointerAvailable = false, wandEnabled = true;
	std::uint64_t ocuPointerFrameId = 1;
	ControllerDevice activeWandController = ControllerDevice::Primary, capturedWandController = ControllerDevice::Both;
	RE::VRControllerState primaryControllerState, secondaryControllerState;
	struct
	{
		bool dragging = false;
	} overlayDragState;
	bool customVRCursorVisible = false;
	ImVec2 customVRCursorPos{};
	OverlayType customVRCursorOverlayType = OverlayType::HMD;
	Settings::OverlayAttachMode GetEffectiveMenuAttachMode() const { return Settings::OverlayAttachMode::HMDOnly; }
	ControllerDevice GetEffectiveMenuAttachController() const { return ControllerDevice::Primary; }
	bool CanUseWandPointing() const { return wandEnabled; }
	void RefreshOCUPointerState() {}
	void ResetWandPointingRuntimeState() { wandState = {}; }
	void UpdateOCUPointerOwnership();
	void UpdateCursorFromWandPointing(bool, ControllerDevice = ControllerDevice::Both);
	void ProcessControllerInputForWandPointingPath(bool, float, ImGuiIO&);
};
using AttachMode = VR::Settings::OverlayAttachMode;
using PolicyHand = WandInteractionPolicy::Hand;
using WandPoses = std::array<int, 1>;

namespace globals
{
	struct Menu
	{
		bool IsMenuSessionOpen() const { return true; }
	} menuValue;
	Menu* menu = &menuValue;
}

std::array<VR::Hand, 2> samples;
std::array<int, 2> g_wandPoseHistory{}, g_controllerRenderModelNames{};
unsigned grantedHands = 3;
unsigned ownershipUpdates = 0;
bool revokeOnSecondUpdate = false;
bool trackingAvailable = true;
CursorOwner gCursorOwner = CursorOwner::Wand;
ImVec2 gLastDesktopMousePos{}, gLastControllerCursorPos{};
bool gHasLastDesktopMousePos = true, gHasLastControllerCursorPos = false;
bool gWandActivationPending = false, gWandClaimedCursorThisFrame = false;
bool gLastObservedMouseButtonDown[ImGuiMouseButton_COUNT]{};
bool gVRMouseButtonDown[ImGuiMouseButton_COUNT]{};
bool gDispatchedWandMouseButtonDown[ImGuiMouseButton_COUNT]{};
constexpr float kDesktopCursorMotionThresholdSq = 4;

std::size_t GetControllerSlot(ControllerDevice hand) { return hand == ControllerDevice::Primary ? 0 : 1; }
PolicyHand ToPolicyHand(ControllerDevice hand)
{
	return hand == ControllerDevice::Primary ? PolicyHand::Primary : hand == ControllerDevice::Secondary ? PolicyHand::Secondary :
	                                                                                                       PolicyHand::None;
}
ControllerDevice FromPolicyHand(PolicyHand hand)
{
	return hand == PolicyHand::Primary ? ControllerDevice::Primary : hand == PolicyHand::Secondary ? ControllerDevice::Secondary :
	                                                                                                 ControllerDevice::Both;
}
bool IsIndividualController(ControllerDevice hand) { return hand != ControllerDevice::Both; }
bool TryGetWandPoses(WandPoses&) { return trackingAvailable; }
void ResetWandPoseTracking() {}
void SampleWandHand(VR& vr, ControllerDevice hand, bool, const WandPoses&)
{
	vr.wandHandStates[GetControllerSlot(hand)] = samples[GetControllerSlot(hand)];
}
void UpdateWandScreenPosition(VR&, ControllerDevice, double, bool) {}
void VR::UpdateOCUPointerOwnership()
{
	if (!ocuPointerAvailable)
		return;
	++ownershipUpdates;
	if (revokeOnSecondUpdate && ownershipUpdates == 2)
		grantedHands = 0;
	for (unsigned slot = 0; slot < 2; ++slot) {
		auto& hand = wandHandStates[slot];
		hand.ocuOwnsInput = (grantedHands & (1u << slot)) != 0;
		if (!hand.ocuOwnsInput) {
			hand.isIntersecting = false;
			hand.hasScreenPosition = false;
		}
	}
}
bool HasUsableCursorPos(const ImVec2& pos)
{
	return std::isfinite(pos.x) && std::isfinite(pos.y) && pos.x > -100000 && pos.y > -100000;
}
ImVec2 ClampCursorToDisplay(const ImVec2& pos, const ImVec2& size)
{
	return { std::clamp(pos.x, 0.0f, size.x), std::clamp(pos.y, 0.0f, size.y) };
}
bool IsThumbstickActive(const RE::VRControllerState&, std::size_t, float) { return false; }
void ProcessThumbstickScroll(RE::VRControllerState&, std::size_t, float, ImGuiIO&) {}
void ResetCursorOwnershipState()
{
	gCursorOwner = CursorOwner::Desktop;
	gHasLastControllerCursorPos = false;
}

#include "vr_wand_cursor_ownership_under_test.h"
#include "vr_wand_selection_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}
void Reset(VR& vr)
{
	vr = {};
	samples = {};
	for (auto& hand : samples)
		hand.isIntersecting = hand.moved = hand.poseValid = hand.hasScreenPosition = true;
	samples[1].screenPosition = { 400, 300 };
	grantedHands = 3;
	ownershipUpdates = 0;
	revokeOnSecondUpdate = false;
	trackingAvailable = true;
	gCursorOwner = CursorOwner::Wand;
	gLastDesktopMousePos = { 50, 50 };
	gLastControllerCursorPos = { 100, 100 };
	gHasLastDesktopMousePos = gHasLastControllerCursorPos = true;
	gWandActivationPending = gWandClaimedCursorThisFrame = false;
	std::fill_n(gLastObservedMouseButtonDown, ImGuiMouseButton_COUNT, false);
	std::fill_n(gVRMouseButtonDown, ImGuiMouseButton_COUNT, false);
	std::fill_n(gDispatchedWandMouseButtonDown, ImGuiMouseButton_COUNT, false);
	auto& io = ImGui::GetIO();
	io.DisplaySize = { 1280, 720 };
	io.MousePos = { 600, 500 };
	std::fill_n(io.MouseDown, ImGuiMouseButton_COUNT, false);
}
void TestHandArbitration()
{
	VR vr;
	Reset(vr);
	vr.ocuPointerAvailable = true;
	grantedHands = 2;
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Secondary && vr.wandState.isIntersecting && ImGui::GetIO().MousePos.x == 400,
		"a denied primary hit starved the granted secondary hand");
	Require(ownershipUpdates == 2, "changed active hand did not refresh the provider's visible beam");
	vr.UpdateCursorFromWandPointing(true, ControllerDevice::Primary);
	Require(vr.activeWandController == ControllerDevice::Secondary, "a denied preferred hand stole the pointer");
	Require(ownershipUpdates == 3, "stable selection added a redundant provider update");
	grantedHands = 0;
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Both && !vr.wandState.isIntersecting && !ImGui::GetIO().WantSetMousePos,
		"unowned tracked hands remained selectable");
	grantedHands = 1;
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Primary && vr.wandState.isIntersecting, "restored ownership did not recover navigation");
	vr.activeWandController = ControllerDevice::Secondary;
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Primary && vr.wandState.isIntersecting, "a denied secondary hit starved the granted primary hand");
	vr.capturedWandController = ControllerDevice::Primary;
	grantedHands = 2;
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Primary && !vr.wandState.isIntersecting,
		"lost capture transferred a held drag to the other hand before cancellation");
	Reset(vr);
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Primary && vr.wandState.isIntersecting && ownershipUpdates == 0,
		"standalone navigation incorrectly required shared pointer ownership");
	Reset(vr);
	vr.ocuPointerAvailable = true;
	grantedHands = 2;
	revokeOnSecondUpdate = true;
	vr.UpdateCursorFromWandPointing(false);
	Require(ownershipUpdates == 2 && !vr.wandState.isIntersecting && !ImGui::GetIO().WantSetMousePos,
		"ownership loss during beam refresh retained a stale cursor or retried without a bound");
	Reset(vr);
	samples[0].poseValid = samples[0].isIntersecting = false;
	vr.UpdateCursorFromWandPointing(false);
	Require(vr.activeWandController == ControllerDevice::Secondary, "tracking loss prevented standalone hand transfer");
}
void TestDragOwnership()
{
	VR vr;
	auto& io = ImGui::GetIO();
	for (bool captured : { true, false }) {
		Reset(vr);
		vr.capturedWandController = captured ? ControllerDevice::Primary : ControllerDevice::Both;
		gDispatchedWandMouseButtonDown[ImGuiMouseButton_Left] = true;
		gVRMouseButtonDown[ImGuiMouseButton_Left] = captured;
		io.MouseDown[ImGuiMouseButton_Left] = true;
		vr.ProcessControllerInputForWandPointingPath(false, 0.2f, io);
		Require(gCursorOwner == CursorOwner::Wand && io.MousePos.x == 100 && io.MousePos.y == 100 && vr.wandState.isIntersecting,
			"desktop movement stole a held wand drag or its queued release");
		Require(gLastDesktopMousePos.x == 600, "protected drag did not consume the desktop motion baseline");
	}
	Reset(vr);
	vr.capturedWandController = ControllerDevice::Primary;
	samples[0].isIntersecting = samples[1].isIntersecting = false;
	vr.ProcessControllerInputForWandPointingPath(false, 0.2f, io);
	Require(gCursorOwner == CursorOwner::Wand && !HasUsableCursorPos(io.MousePos) && !vr.customVRCursorVisible,
		"off-surface drag snapped onto a desktop widget");
	Reset(vr);
	vr.ProcessControllerInputForWandPointingPath(false, 0.2f, io);
	Require(gCursorOwner == CursorOwner::Desktop && io.MousePos.x == 600 && !vr.wandState.isIntersecting,
		"idle wands prevented deliberate desktop takeover");
	Reset(vr);
	io.MousePos = gLastDesktopMousePos;
	io.MouseDown[ImGuiMouseButton_Left] = true;
	vr.ProcessControllerInputForWandPointingPath(false, 0.2f, io);
	Require(gCursorOwner == CursorOwner::Desktop, "idle wand blocked a desktop click");
	Reset(vr);
	gCursorOwner = CursorOwner::Desktop;
	io.MousePos = gLastDesktopMousePos;
	vr.ProcessControllerInputForWandPointingPath(false, 0.2f, io);
	Require(gCursorOwner == CursorOwner::Wand && io.MousePos.x == 100, "wand movement failed to reclaim an idle desktop cursor");
	for (bool testMode : { false, true }) {
		Reset(vr);
		vr.wandEnabled = testMode;
		vr.capturedWandController = ControllerDevice::Primary;
		gDispatchedWandMouseButtonDown[ImGuiMouseButton_Left] = true;
		vr.ProcessControllerInputForWandPointingPath(testMode, 0.2f, io);
		Require(gCursorOwner == CursorOwner::Desktop && io.MousePos.x == 600,
			"stale capture blocked desktop control after wand navigation was disabled");
	}
}
int main()
{
	ImGui::CreateContext();
	int failures = 0;
	for (auto test : { TestDragOwnership, TestHandArbitration }) {
		try {
			test();
		} catch (const std::exception& e) {
			std::cerr << e.what() << '\n';
			++failures;
		}
	}
	ImGui::DestroyContext();
	if (failures == 0)
		std::cout << "VR wand input: drag ownership, desktop handoff and provider arbitration passed\n";
	return failures ? 1 : 0;
}
