#include "Features/VR.h"
#include "RE/B/BSOpenVR.h"
#include "Utils/VRUtils.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <openvr.h>

using namespace DirectX::SimpleMath;
using AttachMode = VR::Settings::OverlayAttachMode;

namespace
{
	enum class WandPoseUpdateMode
	{
		None,
		KeepAlive,
		Active
	};

	constexpr uint32_t kWandCursorActiveFrames = 24;
	constexpr float kWandActivePositionMotionThresholdSq = 0.0015f * 0.0015f;
	constexpr float kWandIdlePositionMotionThresholdSq = 0.0075f * 0.0075f;
	constexpr float kWandActiveDirectionMotionThreshold = 0.00002f;
	constexpr float kWandIdleDirectionMotionThreshold = 0.00008f;
	constexpr float kWandActiveScreenMotionThresholdSq = 2.0f * 2.0f;
	constexpr float kWandIdleScreenMotionThresholdSq = 8.0f * 8.0f;
	constexpr float kWandEdgeStickOverscan = 0.08f;

	vr::TrackedDeviceIndex_t g_previousWandController = vr::k_unTrackedDeviceIndexInvalid;
	Vector3 g_previousWandRayOrigin = Vector3::Zero;
	Vector3 g_previousWandRayDirection = Vector3::Zero;
	ImVec2 g_previousWandScreenPos = ImVec2(0.0f, 0.0f);
	uint32_t g_wandCursorActiveFramesRemaining = 0;
	bool g_hasPreviousWandRay = false;
	bool g_hasPreviousWandScreenPos = false;

	WandPoseUpdateMode GetWandPoseUpdateMode(bool a_forceCursorUpdate, vr::TrackedDeviceIndex_t a_controllerIndex, const Vector3& a_rayOrigin, const Vector3& a_rayDirection)
	{
		const float positionMotionThresholdSq = a_forceCursorUpdate ?
		                                            kWandActivePositionMotionThresholdSq :
		                                            kWandIdlePositionMotionThresholdSq;
		const float directionMotionThreshold = a_forceCursorUpdate ?
		                                           kWandActiveDirectionMotionThreshold :
		                                           kWandIdleDirectionMotionThreshold;

		bool moved = true;
		if (g_hasPreviousWandRay && g_previousWandController == a_controllerIndex) {
			const float positionDeltaSq = (a_rayOrigin - g_previousWandRayOrigin).LengthSquared();
			const float directionDot = std::clamp(a_rayDirection.Dot(g_previousWandRayDirection), -1.0f, 1.0f);
			const float directionDelta = 1.0f - directionDot;
			moved = positionDeltaSq > positionMotionThresholdSq ||
			        directionDelta > directionMotionThreshold;
		}

		if (a_forceCursorUpdate || moved) {
			g_previousWandController = a_controllerIndex;
			g_previousWandRayOrigin = a_rayOrigin;
			g_previousWandRayDirection = a_rayDirection;
			g_hasPreviousWandRay = true;
			g_wandCursorActiveFramesRemaining = kWandCursorActiveFrames;
			return WandPoseUpdateMode::Active;
		}

		if (g_wandCursorActiveFramesRemaining > 0) {
			--g_wandCursorActiveFramesRemaining;
			g_previousWandController = a_controllerIndex;
			g_previousWandRayOrigin = a_rayOrigin;
			g_previousWandRayDirection = a_rayDirection;
			g_hasPreviousWandRay = true;
			return WandPoseUpdateMode::KeepAlive;
		}

		return WandPoseUpdateMode::None;
	}

	void ResetWandPoseTracking()
	{
		g_previousWandController = vr::k_unTrackedDeviceIndexInvalid;
		g_previousWandRayOrigin = Vector3::Zero;
		g_previousWandRayDirection = Vector3::Zero;
		g_previousWandScreenPos = ImVec2(0.0f, 0.0f);
		g_wandCursorActiveFramesRemaining = 0;
		g_hasPreviousWandRay = false;
		g_hasPreviousWandScreenPos = false;
	}
}

bool VR::ComputeWandIntersectionForOverlayType(OverlayType type, vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV)
{
	float controllerM[3][4];
	if (!Util::GetControllerWorldMatrix(controllerIndex, controllerM)) {
		return false;
	}
	Matrix controllerWorld = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(controllerM));
	Vector3 rayOrigin = controllerWorld.Translation();
	Vector3 rayDir = controllerWorld.Forward();

	// Update debug state
	wandState.rayOrigin = rayOrigin;
	wandState.rayDirection = rayDir;
	Matrix overlayWorld;
	if (type == OverlayType::HMD) {
		if (settings.VRMenuPositioningMethod == 1) {  // Fixed World
			overlayWorld = fixedWorldOverlayPosition.m;
		} else {  // HMD Relative
			vr::TrackedDevicePose_t hmdPose;
			if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, &hmdPose, 1))
				return false;
			if (!hmdPose.bPoseIsValid)
				return false;
			Matrix hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
			Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
			overlayWorld = offset * hmdWorld;
		}
	} else {  // Controller Relative
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex == vr::k_unTrackedDeviceIndexInvalid)
			return false;

		float attachM[3][4];
		if (!Util::GetControllerWorldMatrix(attachIndex, attachM))
			return false;
		Matrix attachWorld = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(attachM));

		Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
		overlayWorld = offset * attachWorld;
	}

	if (settings.VRMenuScale < 1e-4f)
		return false;
	const float overlayAspect = type == OverlayType::HMD ? Config::kHMDOverlayAspect : Config::kOverlayAspect;
	overlayWorld = Config::CreateOverlayScaleMatrix(settings.VRMenuScale, overlayAspect) * overlayWorld;

	Matrix worldToOverlay = overlayWorld.Invert();
	Vector3 localOrigin = Vector3::Transform(rayOrigin, worldToOverlay);
	Vector3 localDir = Vector3::TransformNormal(rayDir, worldToOverlay);

	if (std::abs(localDir.z) < 1e-6f)
		return false;

	float t = -localOrigin.z / localDir.z;
	if (t < 0.0f)
		return false;

	Vector3 hit = localOrigin + t * localDir;

	if (hit.x < -0.5f - kWandEdgeStickOverscan ||
		hit.x > 0.5f + kWandEdgeStickOverscan ||
		hit.y < -0.5f - kWandEdgeStickOverscan ||
		hit.y > 0.5f + kWandEdgeStickOverscan) {
		return false;
	}

	outUV.x = std::clamp(hit.x + 0.5f, 0.0f, 1.0f);
	outUV.y = std::clamp(0.5f - hit.y, 0.0f, 1.0f);

	return true;
}

bool VR::ComputeWandIntersection(vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV)
{
	bool intersected = false;
	if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
		if (ComputeWandIntersectionForOverlayType(OverlayType::HMD, controllerIndex, outUV)) {
			intersected = true;
			wandState.overlayType = OverlayType::HMD;
		}
	}
	if (!intersected && (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both)) {
		if (ComputeWandIntersectionForOverlayType(OverlayType::Controller, controllerIndex, outUV)) {
			intersected = true;
			wandState.overlayType = OverlayType::Controller;
		}
	}

	if (intersected) {
		wandState.isIntersecting = true;
		wandState.uvCoordinates = outUV;
		wandState.controllerIndex = controllerIndex;
	} else {
		wandState.isIntersecting = false;
	}

	return intersected;
}

ControllerDevice VR::GetWandPointingControllerDevice() const
{
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

void VR::UpdateCursorFromWandPointing(bool a_forceCursorUpdate)
{
	ImGuiIO& io = ImGui::GetIO();
	io.WantSetMousePos = false;

	if (!CanUseWandPointing() || !globals::menu || !globals::menu->IsEnabled)
		return;

	wandState.isActivelyDrivingCursor = false;

	const vr::TrackedDeviceIndex_t pointingController = GetWandPointingControllerIndex();
	if (pointingController == vr::k_unTrackedDeviceIndexInvalid) {
		wandState.isIntersecting = false;
		return;
	}

	ImVec2 uv;
	bool intersected = ComputeWandIntersection(pointingController, uv);
	const WandPoseUpdateMode updateMode = GetWandPoseUpdateMode(a_forceCursorUpdate, pointingController, wandState.rayOrigin, wandState.rayDirection);
	if (updateMode == WandPoseUpdateMode::None) {
		io.WantSetMousePos = false;
		return;
	}

	if (intersected) {
		float screenX = uv.x * io.DisplaySize.x;
		float screenY = uv.y * io.DisplaySize.y;

		screenX = std::clamp(screenX, 0.0f, io.DisplaySize.x);
		screenY = std::clamp(screenY, 0.0f, io.DisplaySize.y);

		ImVec2 stableScreenPos(screenX, screenY);
		const float screenMotionThresholdSq = a_forceCursorUpdate ?
		                                          kWandActiveScreenMotionThresholdSq :
		                                          kWandIdleScreenMotionThresholdSq;
		if (g_hasPreviousWandScreenPos) {
			const float dx = screenX - g_previousWandScreenPos.x;
			const float dy = screenY - g_previousWandScreenPos.y;
			if ((dx * dx + dy * dy) <= screenMotionThresholdSq) {
				stableScreenPos = g_previousWandScreenPos;
			}
		}

		g_previousWandScreenPos = stableScreenPos;
		g_hasPreviousWandScreenPos = true;
		wandState.isActivelyDrivingCursor = updateMode == WandPoseUpdateMode::Active;

		io.MousePos = stableScreenPos;
		io.AddMousePosEvent(stableScreenPos.x, stableScreenPos.y);
		io.WantSetMousePos = true;
	} else {
		wandState.isIntersecting = false;
		io.WantSetMousePos = false;
	}
}

void VR::ResetWandPointingRuntimeState()
{
	wandState = {};
	ResetWandPoseTracking();
}
