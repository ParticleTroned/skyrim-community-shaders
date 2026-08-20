#include "Features/VR.h"
#include "Features/VR/WandInteractionPolicy.h"
#include "Features/VR/WandSurfaceGeometry.h"
#include "RE/B/BSOpenVR.h"
#include "Utils/VRUtils.h"

#include <SimpleMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui_internal.h>
#include <openvr.h>
#include <string>
#include <vector>

using namespace DirectX::SimpleMath;
using AttachMode = VR::Settings::OverlayAttachMode;
using PolicyHand = WandInteractionPolicy::Hand;

namespace
{
	enum class WandPoseUpdateMode
	{
		None,
		KeepAlive,
		Active
	};

	struct WandPoseHistory
	{
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		Vector3 rayOrigin = Vector3::Zero;
		Vector3 rayDirection = Vector3::Zero;
		std::uint32_t activeFramesRemaining = 0;
		bool valid = false;
	};

	constexpr std::uint32_t kWandCursorActiveFrames = 24;
	constexpr float kWandIdlePositionMotionThresholdSq = 0.0075f * 0.0075f;
	constexpr float kWandIdleDirectionMotionThreshold = 0.00008f;
	constexpr float kWandScreenMotionThresholdSq = 1.5f * 1.5f;
	constexpr float kWandHoverHapticDuration = 4.0f;

	std::array<WandPoseHistory, 2> g_wandPoseHistory{};
	std::array<ImVec2, 2> g_previousWandScreenPos{};
	std::array<bool, 2> g_hasPreviousWandScreenPos{};
	std::array<std::string, vr::k_unMaxTrackedDeviceCount> g_controllerRenderModelNames{};

	bool IsIndividualController(ControllerDevice a_controller)
	{
		return a_controller == ControllerDevice::Primary || a_controller == ControllerDevice::Secondary;
	}

	std::size_t GetControllerSlot(ControllerDevice a_controller)
	{
		return a_controller == ControllerDevice::Secondary ? 1u : 0u;
	}

	PolicyHand ToPolicyHand(ControllerDevice a_controller)
	{
		return a_controller == ControllerDevice::Primary ? PolicyHand::Primary :
		       a_controller == ControllerDevice::Secondary ? PolicyHand::Secondary :
		                                                     PolicyHand::None;
	}

	ControllerDevice FromPolicyHand(PolicyHand a_hand)
	{
		return a_hand == PolicyHand::Primary ? ControllerDevice::Primary :
		       a_hand == PolicyHand::Secondary ? ControllerDevice::Secondary :
		                                         ControllerDevice::Both;
	}

	WandPoseUpdateMode GetWandPoseUpdateMode(
		ControllerDevice a_controller,
		bool a_forceCursorUpdate,
		vr::TrackedDeviceIndex_t a_controllerIndex,
		const Vector3& a_rayOrigin,
		const Vector3& a_rayDirection)
	{
		auto& history = g_wandPoseHistory[GetControllerSlot(a_controller)];
		bool moved = true;
		if (!a_forceCursorUpdate && history.valid && history.controllerIndex == a_controllerIndex) {
			const float positionDeltaSq = (a_rayOrigin - history.rayOrigin).LengthSquared();
			const float directionDot = std::clamp(a_rayDirection.Dot(history.rayDirection), -1.0f, 1.0f);
			const float directionDelta = 1.0f - directionDot;
			moved = positionDeltaSq > kWandIdlePositionMotionThresholdSq ||
			        directionDelta > kWandIdleDirectionMotionThreshold;
		}

		history.controllerIndex = a_controllerIndex;
		history.rayOrigin = a_rayOrigin;
		history.rayDirection = a_rayDirection;
		history.valid = true;

		if (a_forceCursorUpdate || moved) {
			history.activeFramesRemaining = kWandCursorActiveFrames;
			return WandPoseUpdateMode::Active;
		}
		if (history.activeFramesRemaining > 0) {
			--history.activeFramesRemaining;
			return WandPoseUpdateMode::KeepAlive;
		}
		return WandPoseUpdateMode::None;
	}

	void ResetWandPoseTracking()
	{
		g_wandPoseHistory = {};
		g_previousWandScreenPos = {};
		g_hasPreviousWandScreenPos = {};
	}

	bool TryGetControllerRenderModelName(vr::TrackedDeviceIndex_t a_controllerIndex, std::string& a_name)
	{
		if (a_controllerIndex >= g_controllerRenderModelNames.size())
			return false;
		auto& cachedName = g_controllerRenderModelNames[a_controllerIndex];
		if (!cachedName.empty()) {
			a_name = cachedName;
			return true;
		}

		auto* system = RE::BSOpenVR::GetIVRSystem();
		if (!system)
			return false;
		vr::ETrackedPropertyError error = vr::TrackedProp_Success;
		const std::uint32_t requiredLength = system->GetStringTrackedDeviceProperty(
			a_controllerIndex, vr::Prop_RenderModelName_String, nullptr, 0, &error);
		if (requiredLength <= 1 || error != vr::TrackedProp_BufferTooSmall)
			return false;

		std::vector<char> buffer(requiredLength);
		error = vr::TrackedProp_Success;
		if (system->GetStringTrackedDeviceProperty(
				a_controllerIndex,
				vr::Prop_RenderModelName_String,
				buffer.data(),
				static_cast<std::uint32_t>(buffer.size()),
				&error) == 0 ||
			error != vr::TrackedProp_Success) {
			return false;
		}
		cachedName.assign(buffer.data());
		a_name = cachedName;
		return !a_name.empty();
	}

	bool TryGetControllerAimTransform(vr::TrackedDeviceIndex_t a_controllerIndex, Matrix& a_controllerToAim)
	{
		auto* renderModels = RE::BSOpenVR::GetIVRRenderModels();
		auto* system = RE::BSOpenVR::GetIVRSystem();
		if (!renderModels || !system)
			return false;

		vr::VRControllerState_t controllerState{};
		if (!system->GetControllerState(a_controllerIndex, &controllerState, sizeof(controllerState)))
			return false;

		std::vector<std::string> renderModelCandidates;
		const auto addCandidate = [&](std::string a_name) {
			if (!a_name.empty() &&
				std::find(renderModelCandidates.begin(), renderModelCandidates.end(), a_name) == renderModelCandidates.end()) {
				renderModelCandidates.push_back(std::move(a_name));
			}
		};
		const auto role = system->GetControllerRoleForTrackedDeviceIndex(a_controllerIndex);
		const std::string canonicalHandName =
			role == vr::TrackedControllerRole_LeftHand ? "renderLeftHand" :
			role == vr::TrackedControllerRole_RightHand ? "renderRightHand" :
			                                                "";
		std::string hardwareRenderModelName;
		TryGetControllerRenderModelName(a_controllerIndex, hardwareRenderModelName);

		// OCU associates its live OpenXR aim component with these canonical hand
		// names. Its physical controller property name is accepted but resolves to
		// an identity component, which is only the raw grip pose. SteamVR normally
		// wants the physical model name, so preserve that as the first choice there.
		const bool preferCanonicalHandName =
			globals::features::vr.openVRInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;
		if (preferCanonicalHandName) {
			addCandidate(canonicalHandName);
			addCandidate(hardwareRenderModelName);
		} else {
			addCandidate(hardwareRenderModelName);
			addCandidate(canonicalHandName);
		}

		for (const auto& renderModelName : renderModelCandidates) {
			vr::RenderModel_ControllerMode_State_t modeState{};
			vr::RenderModel_ComponentState_t componentState{};
			if (!renderModels->GetComponentState(
					renderModelName.c_str(), "tip", &controllerState, &modeState, &componentState)) {
				continue;
			}

			const Matrix candidate = Util::HmdMatrix34ToMatrix(componentState.mTrackingToComponentLocal);
			const Vector3 aimForward = candidate.Forward();
			if (std::isfinite(aimForward.x) &&
				std::isfinite(aimForward.y) &&
				std::isfinite(aimForward.z) &&
				aimForward.LengthSquared() > 0.25f) {
				a_controllerToAim = candidate;
				return true;
			}
		}
		return false;
	}

	bool TryGetControllerPointingRay(
		vr::TrackedDeviceIndex_t a_controllerIndex,
		float a_pitchAdjustmentDegrees,
		Vector3& a_rayOrigin,
		Vector3& a_rayDirection,
		bool& a_usedOCUAimPose)
	{
		float controllerM[3][4];
		if (!Util::GetControllerWorldMatrix(a_controllerIndex, controllerM))
			return false;

		const Matrix controllerWorld = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(controllerM));
		Matrix pointingWorld = controllerWorld;
		Matrix controllerToAim;
		// OCU's render-model "tip" is a stable grip-relative transform calculated
		// from the OpenXR aim space. Its shared OC_AIM_POSES slot can transiently
		// contain the raw grip/render pose, whose -Z axis exits the controller face.
		a_usedOCUAimPose = TryGetControllerAimTransform(a_controllerIndex, controllerToAim);
		if (a_usedOCUAimPose)
			pointingWorld = controllerToAim * controllerWorld;
		if (std::abs(a_pitchAdjustmentDegrees) > 1e-4f) {
			const Matrix pitchAdjustment = Matrix::CreateRotationX(
				DirectX::XMConvertToRadians(a_pitchAdjustmentDegrees));
			pointingWorld = pitchAdjustment * pointingWorld;
		}

		a_rayOrigin = pointingWorld.Translation();
		a_rayDirection = pointingWorld.Forward();
		if (!std::isfinite(a_rayOrigin.x) ||
			!std::isfinite(a_rayOrigin.y) ||
			!std::isfinite(a_rayOrigin.z) ||
			!std::isfinite(a_rayDirection.x) ||
			!std::isfinite(a_rayDirection.y) ||
			!std::isfinite(a_rayDirection.z) ||
			a_rayDirection.LengthSquared() < 1e-6f) {
			return false;
		}
		a_rayDirection.Normalize();
		return true;
	}

	bool TryGetOverlayWorldMatrix(const VR& a_vr, VR::OverlayType a_type, Matrix& a_overlayWorld)
	{
		if (a_type == VR::OverlayType::HMD) {
			if (a_vr.UseFixedWorldMenuPositioning()) {
				a_overlayWorld = a_vr.fixedWorldOverlayPosition.m;
			} else {
				vr::TrackedDevicePose_t hmdPose;
				if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, &hmdPose, 1) || !hmdPose.bPoseIsValid)
					return false;
				const Matrix hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
				const Matrix offset = Matrix::CreateTranslation(
					a_vr.settings.VRMenuOffsetX, a_vr.settings.VRMenuOffsetY, a_vr.settings.VRMenuOffsetZ);
				a_overlayWorld = offset * hmdWorld;
			}
		} else {
			const vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(
				a_vr.settings.VRMenuAttachController, a_vr.lastKnownLeftHandedMode);
			if (attachIndex == vr::k_unTrackedDeviceIndexInvalid)
				return false;
			float attachM[3][4];
			if (!Util::GetControllerWorldMatrix(attachIndex, attachM))
				return false;
			const Matrix attachWorld = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(attachM));
			const Matrix offset = Matrix::CreateTranslation(
				a_vr.settings.VRMenuControllerOffsetX,
				a_vr.settings.VRMenuControllerOffsetY,
				a_vr.settings.VRMenuControllerOffsetZ);
			a_overlayWorld = offset * attachWorld;
		}

		if (a_vr.settings.VRMenuScale < 1e-4f)
			return false;
		const float overlayAspect = a_type == VR::OverlayType::HMD ? VR::Config::kHMDOverlayAspect : VR::Config::kOverlayAspect;
		a_overlayWorld = VR::Config::CreateOverlayScaleMatrix(a_vr.settings.VRMenuScale, overlayAspect) * a_overlayWorld;
		return true;
	}

	bool TryComputeOverlayIntersection(
		const VR& a_vr,
		VR::OverlayType a_type,
		const Vector3& a_rayOrigin,
		const Vector3& a_rayDirection,
		ImVec2& a_outUV,
		float& a_outDistance,
		bool& a_outUsedPresentedSurface)
	{
		VR::PresentedMenuSurface presentedSurface;
		a_outUsedPresentedSurface = a_vr.TryGetPresentedMenuSurface(a_type, presentedSurface);
		if (!a_outUsedPresentedSurface) {
			// Startup and legacy IVROverlay fallback. The in-scene renderer replaces
			// this with the exact world-space vertices after its first presentation.
			Matrix overlayWorld;
			if (!TryGetOverlayWorldMatrix(a_vr, a_type, overlayWorld))
				return false;
			presentedSurface.topLeft = Vector3::Transform(Vector3(-0.5f, 0.5f, 0.0f), overlayWorld);
			presentedSurface.topRight = Vector3::Transform(Vector3(0.5f, 0.5f, 0.0f), overlayWorld);
			presentedSurface.bottomLeft = Vector3::Transform(Vector3(-0.5f, -0.5f, 0.0f), overlayWorld);
		}

		// These vertices are the same geometry the player sees. The Gram solve
		// recovers independent coordinates along both transformed quad axes.
		const auto toGeometryVector = [](const Vector3& a_value) {
			return WandSurfaceGeometry::Vector{ a_value.x, a_value.y, a_value.z };
		};
		const WandSurfaceGeometry::Surface surface{
			toGeometryVector(presentedSurface.topLeft),
			toGeometryVector(presentedSurface.topRight - presentedSurface.topLeft),
			toGeometryVector(presentedSurface.bottomLeft - presentedSurface.topLeft)
		};
		WandSurfaceGeometry::Hit hit{};
		if (!WandSurfaceGeometry::TryIntersect(
				surface,
				toGeometryVector(a_rayOrigin),
				toGeometryVector(a_rayDirection),
				hit)) {
			return false;
		}

		a_outUV = ImVec2(hit.u, hit.v);
		a_outDistance = hit.distance;
		return true;
	}

	bool TryComputeNearestIntersection(
		const VR& a_vr,
		const Vector3& a_rayOrigin,
		const Vector3& a_rayDirection,
		ImVec2& a_outUV,
		float& a_outDistance,
		VR::OverlayType& a_outType,
		bool& a_outUsedPresentedSurface)
	{
		bool intersected = false;
		a_outDistance = (std::numeric_limits<float>::max)();
		auto considerOverlay = [&](VR::OverlayType a_type) {
			ImVec2 uv;
			float distance = 0.0f;
			bool usedPresentedSurface = false;
			if (TryComputeOverlayIntersection(a_vr, a_type, a_rayOrigin, a_rayDirection, uv, distance, usedPresentedSurface) && distance < a_outDistance) {
				intersected = true;
				a_outUV = uv;
				a_outDistance = distance;
				a_outType = a_type;
				a_outUsedPresentedSurface = usedPresentedSurface;
			}
		};

		if (a_vr.settings.attachMode == AttachMode::HMDOnly || a_vr.settings.attachMode == AttachMode::Both)
			considerOverlay(VR::OverlayType::HMD);
		if (a_vr.settings.attachMode == AttachMode::ControllerOnly || a_vr.settings.attachMode == AttachMode::Both)
			considerOverlay(VR::OverlayType::Controller);
		return intersected;
	}

	void SampleWandHand(VR& a_vr, ControllerDevice a_controller, bool a_forceCursorUpdate)
	{
		auto& hand = a_vr.wandHandStates[GetControllerSlot(a_controller)];
		hand = {};
		hand.controllerIndex = Util::GetControllerIndexForDevice(a_controller, a_vr.lastKnownLeftHandedMode);
		if (hand.controllerIndex == vr::k_unTrackedDeviceIndexInvalid)
			return;
		if (!TryGetControllerPointingRay(
				hand.controllerIndex,
				a_vr.settings.WandAimPitchTrimDegrees,
				hand.rayOrigin,
				hand.rayDirection,
				hand.usingOCUAimPose))
			return;

		hand.poseValid = true;
		const WandPoseUpdateMode updateMode = GetWandPoseUpdateMode(
			a_controller,
			a_forceCursorUpdate,
			hand.controllerIndex,
			hand.rayOrigin,
			hand.rayDirection);
		hand.moved = updateMode == WandPoseUpdateMode::Active;
		hand.isIntersecting = TryComputeNearestIntersection(
			a_vr,
			hand.rayOrigin,
			hand.rayDirection,
			hand.uvCoordinates,
			hand.hitDistance,
			hand.overlayType,
			hand.usingPresentedSurface);
	}
}

bool VR::ComputeWandIntersectionForOverlayType(OverlayType a_type, vr::TrackedDeviceIndex_t a_controllerIndex, ImVec2& a_outUV)
{
	Vector3 rayOrigin = Vector3::Zero;
	Vector3 rayDirection = Vector3::Zero;
	bool usedOCUAimPose = false;
	if (!TryGetControllerPointingRay(a_controllerIndex, settings.WandAimPitchTrimDegrees, rayOrigin, rayDirection, usedOCUAimPose))
		return false;
	float distance = 0.0f;
	bool usedPresentedSurface = false;
	const bool intersected = TryComputeOverlayIntersection(*this, a_type, rayOrigin, rayDirection, a_outUV, distance, usedPresentedSurface);
	wandState.rayOrigin = rayOrigin;
	wandState.rayDirection = rayDirection;
	wandState.usingOCUAimPose = usedOCUAimPose;
	wandState.usingPresentedSurface = usedPresentedSurface;
	return intersected;
}

bool VR::ComputeWandIntersection(vr::TrackedDeviceIndex_t a_controllerIndex, ImVec2& a_outUV)
{
	Vector3 rayOrigin = Vector3::Zero;
	Vector3 rayDirection = Vector3::Zero;
	bool usedOCUAimPose = false;
	if (!TryGetControllerPointingRay(a_controllerIndex, settings.WandAimPitchTrimDegrees, rayOrigin, rayDirection, usedOCUAimPose)) {
		wandState.isIntersecting = false;
		return false;
	}
	float distance = 0.0f;
	OverlayType overlayType = OverlayType::HMD;
	bool usedPresentedSurface = false;
	const bool intersected = TryComputeNearestIntersection(*this, rayOrigin, rayDirection, a_outUV, distance, overlayType, usedPresentedSurface);
	wandState.rayOrigin = rayOrigin;
	wandState.rayDirection = rayDirection;
	wandState.usingOCUAimPose = usedOCUAimPose;
	wandState.usingPresentedSurface = usedPresentedSurface;
	wandState.isIntersecting = intersected;
	if (intersected) {
		wandState.uvCoordinates = a_outUV;
		wandState.overlayType = overlayType;
		wandState.controllerIndex = a_controllerIndex;
	}
	return intersected;
}

ControllerDevice VR::GetWandPointingControllerDevice() const
{
	if (IsIndividualController(activeWandController))
		return activeWandController;
	if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
		return settings.VRMenuAttachController == ControllerDevice::Primary ?
		           ControllerDevice::Secondary :
		           ControllerDevice::Primary;
	}
	return ControllerDevice::Primary;
}

vr::TrackedDeviceIndex_t VR::GetWandPointingControllerIndex() const
{
	return Util::GetControllerIndexForDevice(GetWandPointingControllerDevice(), lastKnownLeftHandedMode);
}

void VR::UpdateCursorFromWandPointing(bool a_forceCursorUpdate, ControllerDevice a_preferredController)
{
	if (!CanUseWandPointing() || !globals::menu || !globals::menu->IsEnabled)
		return;
	ImGuiIO& io = ImGui::GetIO();
	io.WantSetMousePos = false;

	SampleWandHand(*this, ControllerDevice::Primary, a_forceCursorUpdate && a_preferredController == ControllerDevice::Primary);
	SampleWandHand(*this, ControllerDevice::Secondary, a_forceCursorUpdate && a_preferredController == ControllerDevice::Secondary);
	const auto& primary = wandHandStates[GetControllerSlot(ControllerDevice::Primary)];
	const auto& secondary = wandHandStates[GetControllerSlot(ControllerDevice::Secondary)];
	const WandInteractionPolicy::Candidate primaryCandidate{ primary.isIntersecting, primary.moved, primary.hitDistance };
	const WandInteractionPolicy::Candidate secondaryCandidate{ secondary.isIntersecting, secondary.moved, secondary.hitDistance };
	activeWandController = FromPolicyHand(WandInteractionPolicy::SelectActiveHand(
		ToPolicyHand(capturedWandController),
		ToPolicyHand(a_preferredController),
		ToPolicyHand(activeWandController),
		primaryCandidate,
		secondaryCandidate));

	if (!IsIndividualController(activeWandController)) {
		wandState = {};
		return;
	}
	auto& activeHand = wandHandStates[GetControllerSlot(activeWandController)];
	wandState.isIntersecting = activeHand.isIntersecting;
	wandState.isActivelyDrivingCursor = activeHand.isIntersecting && activeHand.moved;
	wandState.uvCoordinates = activeHand.uvCoordinates;
	wandState.overlayType = activeHand.overlayType;
	wandState.controllerIndex = activeHand.controllerIndex;
	wandState.rayOrigin = activeHand.rayOrigin;
	wandState.rayDirection = activeHand.rayDirection;
	wandState.usingOCUAimPose = activeHand.usingOCUAimPose;
	wandState.usingPresentedSurface = activeHand.usingPresentedSurface;
	if (!activeHand.isIntersecting)
		return;

	const std::size_t slot = GetControllerSlot(activeWandController);
	const ImVec2 rawScreenPosition(
		std::clamp(activeHand.uvCoordinates.x * io.DisplaySize.x, 0.0f, io.DisplaySize.x),
		std::clamp(activeHand.uvCoordinates.y * io.DisplaySize.y, 0.0f, io.DisplaySize.y));
	ImVec2 screenPosition = rawScreenPosition;
	if (!a_forceCursorUpdate && g_hasPreviousWandScreenPos[slot]) {
		const float dx = rawScreenPosition.x - g_previousWandScreenPos[slot].x;
		const float dy = rawScreenPosition.y - g_previousWandScreenPos[slot].y;
		if (dx * dx + dy * dy <= kWandScreenMotionThresholdSq)
			screenPosition = g_previousWandScreenPos[slot];
	}
	g_previousWandScreenPos[slot] = screenPosition;
	g_hasPreviousWandScreenPos[slot] = true;
	activeHand.screenPosition = screenPosition;
	activeHand.hasScreenPosition = true;
	io.MousePos = screenPosition;
	io.AddMousePosEvent(screenPosition.x, screenPosition.y);
	io.WantSetMousePos = true;
}

bool VR::UpdateWandPoseOwnershipSignal()
{
	bool anyMeaningfulMovement = false;
	for (const ControllerDevice controller : { ControllerDevice::Primary, ControllerDevice::Secondary }) {
		const vr::TrackedDeviceIndex_t controllerIndex = Util::GetControllerIndexForDevice(controller, lastKnownLeftHandedMode);
		if (controllerIndex == vr::k_unTrackedDeviceIndexInvalid)
			continue;
		Vector3 rayOrigin = Vector3::Zero;
		Vector3 rayDirection = Vector3::Zero;
		bool usedOCUAimPose = false;
		if (!TryGetControllerPointingRay(controllerIndex, settings.WandAimPitchTrimDegrees, rayOrigin, rayDirection, usedOCUAimPose))
			continue;
		auto& history = g_wandPoseHistory[GetControllerSlot(controller)];
		const bool hadPreviousPose = history.valid && history.controllerIndex == controllerIndex;
		const WandPoseUpdateMode updateMode = GetWandPoseUpdateMode(
			controller, false, controllerIndex, rayOrigin, rayDirection);
		anyMeaningfulMovement = anyMeaningfulMovement || (hadPreviousPose && updateMode == WandPoseUpdateMode::Active);
	}
	wandState.isIntersecting = false;
	wandState.isActivelyDrivingCursor = false;
	return anyMeaningfulMovement;
}

bool VR::IsWandControllerIntersecting(ControllerDevice a_controller) const
{
	return IsIndividualController(a_controller) && wandHandStates[GetControllerSlot(a_controller)].isIntersecting;
}

bool VR::TryCaptureWandController(ControllerDevice a_controller)
{
	if (!IsIndividualController(a_controller) || !IsWandControllerIntersecting(a_controller))
		return false;
	if (IsIndividualController(capturedWandController) && capturedWandController != a_controller)
		return false;
	capturedWandController = a_controller;
	activeWandController = a_controller;
	return true;
}

void VR::ReleaseWandControllerCapture(ControllerDevice a_controller)
{
	if (capturedWandController == a_controller)
		capturedWandController = ControllerDevice::Both;
}

void VR::TriggerWandHaptic(ControllerDevice a_controller, float a_duration)
{
	if (!IsIndividualController(a_controller))
		return;
	auto* openVR = RE::BSOpenVR::GetSingleton();
	auto* system = openVR ? openVR->vrSystem : nullptr;
	const vr::TrackedDeviceIndex_t controllerIndex = Util::GetControllerIndexForDevice(a_controller, lastKnownLeftHandedMode);
	if (!openVR || !system || controllerIndex == vr::k_unTrackedDeviceIndexInvalid)
		return;
	const auto role = system->GetControllerRoleForTrackedDeviceIndex(controllerIndex);
	if (role != vr::TrackedControllerRole_LeftHand && role != vr::TrackedControllerRole_RightHand)
		return;
	openVR->TriggerHapticPulse(role == vr::TrackedControllerRole_RightHand, std::clamp(a_duration, 0.0f, 25.0f));
}

void VR::UpdateWandHoverFeedback()
{
	if (!CanUseWandPointing() || !wandState.isIntersecting || !IsIndividualController(activeWandController)) {
		lastWandHoveredID = 0;
		lastWandHoveredController = ControllerDevice::Both;
		return;
	}
	const ImGuiID hoveredID = ImGui::GetHoveredID();
	if (hoveredID != 0 && (hoveredID != lastWandHoveredID || activeWandController != lastWandHoveredController))
		TriggerWandHaptic(activeWandController, kWandHoverHapticDuration);
	lastWandHoveredID = hoveredID;
	lastWandHoveredController = activeWandController;
}

void VR::ResetWandPointingRuntimeState()
{
	InvalidatePresentedMenuSurfaces();
	wandState = {};
	wandHandStates = {};
	activeWandController = ControllerDevice::Both;
	capturedWandController = ControllerDevice::Both;
	lastWandHoveredID = 0;
	lastWandHoveredController = ControllerDevice::Both;
	ResetWandPoseTracking();
}
