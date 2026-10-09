#include "Features/VR.h"
#include "Features/VR/OCUPointerClient.h"
#include "Features/VR/WandCursorFilter.h"
#include "Features/VR/WandInteractionPolicy.h"
#include "Features/VR/WandSurfaceGeometry.h"
#include "RE/B/BSOpenVR.h"
#include "Utils/VRUtils.h"

#include <SimpleMath.h>
#include <algorithm>
#include <array>
#include <chrono>
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
	constexpr float kWandHoverHapticDuration = 4.0f;

	OCUPointer::Client g_ocuPointerClient;
	OCUPointer::Client::Frame g_ocuPointerFrame;
	std::uint64_t g_ocuPointerSession = 0;
	bool g_ocuNeedsPresentedSurface = false;
	std::array<WandPoseHistory, 2> g_wandPoseHistory{};
	struct WandCursorHistory
	{
		WandCursorFilter::Filter filter;
		WandCursorFilter::Position filteredPosition{};
		int sampleFrame = -1;
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		VR::OverlayType overlayType = VR::OverlayType::HMD;
		ImVec2 displaySize{};
		float pitchTrimDegrees = 0.0f;
		bool usingAimComponent = false;
	};
	std::array<WandCursorHistory, 2> g_wandCursorHistory{};
	std::array<std::string, vr::k_unMaxTrackedDeviceCount> g_controllerRenderModelNames{};

	bool IsIndividualController(ControllerDevice a_controller)
	{
		return a_controller == ControllerDevice::Primary || a_controller == ControllerDevice::Secondary;
	}

	std::size_t GetControllerSlot(ControllerDevice a_controller)
	{
		return a_controller == ControllerDevice::Secondary ? 1u : 0u;
	}

	std::uint32_t GetPhysicalHand(const VR& a_vr, ControllerDevice a_controller)
	{
		const bool left = (a_controller == ControllerDevice::Primary) == a_vr.lastKnownLeftHandedMode;
		return left ? 0u : 1u;
	}

	constexpr float kWandBeamColors[2][4]{
		{ 1.0f, 240.0f / 255.0f, 220.0f / 255.0f, 200.0f / 255.0f },
		{ 55.0f / 255.0f, 145.0f / 255.0f, 1.0f, 220.0f / 255.0f }
	};
	constexpr float kWandDotColors[2][4]{
		{ 1.0f, 250.0f / 255.0f, 240.0f / 255.0f, 0.95f },
		{ 225.0f / 255.0f, 245.0f / 255.0f, 1.0f, 0.95f }
	};

	ImVec4 ResolveWandPointerColor(const VR& a_vr, bool a_dot)
	{
		bool pressed = ImGui::GetCurrentContext() && ImGui::GetIO().MouseDown[ImGuiMouseButton_Left];
		if (g_ocuPointerFrame.valid) {
			pressed = IsIndividualController(a_vr.activeWandController) &&
			          (g_ocuPointerFrame.snapshot.hands[GetPhysicalHand(a_vr, a_vr.activeWandController)].flags & ocu_pointer::TriggerDown);
			const auto& style = g_ocuPointerFrame.snapshot.style;
			const auto* color = a_dot ? style.dotColor[pressed ? 1 : 0] : style.beamColor[pressed ? 1 : 0];
			return ImVec4(color[0], color[1], color[2], color[3]);
		}
		const auto* color = a_dot ? kWandDotColors[pressed ? 1 : 0] : kWandBeamColors[pressed ? 1 : 0];
		return ImVec4(color[0], color[1], color[2], color[3]);
	}

	PolicyHand ToPolicyHand(ControllerDevice a_controller)
	{
		return a_controller == ControllerDevice::Primary   ? PolicyHand::Primary :
		       a_controller == ControllerDevice::Secondary ? PolicyHand::Secondary :
		                                                     PolicyHand::None;
	}

	ControllerDevice FromPolicyHand(PolicyHand a_hand)
	{
		return a_hand == PolicyHand::Primary   ? ControllerDevice::Primary :
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
		// Accumulate deliberate slow movement instead of losing it below a per-sample threshold.
		if (moved) {
			history.rayOrigin = a_rayOrigin;
			history.rayDirection = a_rayDirection;
		}
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
		g_wandCursorHistory = {};
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
		if (requiredLength <= 1 || requiredLength > vr::k_unMaxPropertyStringSize ||
			(error != vr::TrackedProp_BufferTooSmall && error != vr::TrackedProp_Success))
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
			role == vr::TrackedControllerRole_LeftHand  ? "renderLeftHand" :
			role == vr::TrackedControllerRole_RightHand ? "renderRightHand" :
														  "";
		std::string hardwareRenderModelName;
		TryGetControllerRenderModelName(a_controllerIndex, hardwareRenderModelName);

		// OpenComposite resolves component transforms through canonical hand names;
		// SteamVR resolves them through the controller's physical render model.
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

	using WandPoses = std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount>;

	bool TryGetWandPoses(WandPoses& a_poses)
	{
		auto* compositor = RE::BSOpenVR::GetIVRCompositor();
		return compositor && compositor->GetLastPoses(a_poses.data(),
								 static_cast<std::uint32_t>(a_poses.size()), nullptr, 0) == vr::VRCompositorError_None;
	}

	bool TryGetWandDeviceWorld(vr::TrackedDeviceIndex_t a_index, Matrix& a_world, const WandPoses* a_sampledPoses = nullptr)
	{
		WandPoses localPoses{};
		if (!a_sampledPoses) {
			// Input must never advance or block the compositor's frame lifecycle.
			if (!TryGetWandPoses(localPoses))
				return false;
			a_sampledPoses = &localPoses;
		}
		if (a_index >= a_sampledPoses->size())
			return false;
		const auto& pose = (*a_sampledPoses)[a_index];
		if (!pose.bPoseIsValid || !pose.bDeviceIsConnected)
			return false;
		a_world = Util::HmdMatrix34ToMatrix(pose.mDeviceToAbsoluteTracking);
		return true;
	}

	bool TryGetControllerPointingRay(
		vr::TrackedDeviceIndex_t a_controllerIndex,
		float a_pitchAdjustmentDegrees,
		Vector3& a_rayOrigin,
		Vector3& a_rayDirection,
		bool& a_usedAimComponent,
		const WandPoses* a_sampledPoses = nullptr)
	{
		if (g_ocuPointerFrame.providerAvailable) {
			a_usedAimComponent = false;
			auto* system = RE::BSOpenVR::GetIVRSystem();
			if (!g_ocuPointerFrame.valid || !system)
				return false;
			const auto role = system->GetControllerRoleForTrackedDeviceIndex(a_controllerIndex);
			if (role != vr::TrackedControllerRole_LeftHand && role != vr::TrackedControllerRole_RightHand)
				return false;
			const auto& hand = g_ocuPointerFrame.snapshot.hands[role == vr::TrackedControllerRole_LeftHand ? 0 : 1];
			if ((hand.flags & (ocu_pointer::RayValid | ocu_pointer::InputAvailable)) !=
				(ocu_pointer::RayValid | ocu_pointer::InputAvailable))
				return false;
			a_rayOrigin = Vector3(hand.origin[0], hand.origin[1], hand.origin[2]);
			a_rayDirection = Vector3(hand.direction[0], hand.direction[1], hand.direction[2]);
			return true;
		}

		Matrix controllerWorld;
		if (!TryGetWandDeviceWorld(a_controllerIndex, controllerWorld, a_sampledPoses))
			return false;
		Matrix pointingWorld = controllerWorld;
		Matrix controllerToAim;
		// A render-model tip may be a fixed profile transform. It does not include
		// OpenComposite's independent menu-laser calibration or smoothing.
		a_usedAimComponent = TryGetControllerAimTransform(a_controllerIndex, controllerToAim);
		if (a_usedAimComponent)
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
				Matrix hmdWorld;
				if (!TryGetWandDeviceWorld(vr::k_unTrackedDeviceIndex_Hmd, hmdWorld))
					return false;
				const Matrix offset = Matrix::CreateTranslation(a_vr.GetEffectiveHMDMenuOffset());
				a_overlayWorld = offset * hmdWorld;
			}
		} else {
			const vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(
				a_vr.GetEffectiveMenuAttachController(), a_vr.lastKnownLeftHandedMode);
			if (attachIndex == vr::k_unTrackedDeviceIndexInvalid)
				return false;
			Matrix attachWorld;
			if (!TryGetWandDeviceWorld(attachIndex, attachWorld))
				return false;
			const Matrix offset = Matrix::CreateTranslation(a_vr.GetEffectiveControllerMenuOffset());
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
			if ((!a_vr.ShouldUseInSceneOverlay() && a_vr.openVRInfo.runtimeType == VRDetection::RuntimeType::SteamVR) ||
				(g_ocuPointerFrame.providerAvailable && g_ocuNeedsPresentedSurface))
				return false;
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

		const auto attachMode = a_vr.GetEffectiveMenuAttachMode();
		if (attachMode == AttachMode::HMDOnly || attachMode == AttachMode::Both)
			considerOverlay(VR::OverlayType::HMD);
		if (attachMode == AttachMode::ControllerOnly || attachMode == AttachMode::Both)
			considerOverlay(VR::OverlayType::Controller);
		return intersected;
	}

	void UpdateWandScreenPosition(VR& a_vr, ControllerDevice a_controller, double a_sampleSeconds, bool a_holdPosition)
	{
		const auto slot = GetControllerSlot(a_controller);
		auto& hand = a_vr.wandHandStates[slot];
		auto& history = g_wandCursorHistory[slot];
		const auto displaySize = ImGui::GetIO().DisplaySize;
		if (!hand.isIntersecting || !std::isfinite(hand.uvCoordinates.x) || !std::isfinite(hand.uvCoordinates.y) ||
			!std::isfinite(displaySize.x) || !std::isfinite(displaySize.y) ||
			displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
			history = {};
			return;
		}
		if (history.controllerIndex != hand.controllerIndex || history.overlayType != hand.overlayType ||
			history.displaySize.x != displaySize.x || history.displaySize.y != displaySize.y ||
			history.pitchTrimDegrees != a_vr.settings.WandAimPitchTrimDegrees ||
			history.usingAimComponent != hand.usingAimComponent) {
			history = {};
			history.controllerIndex = hand.controllerIndex;
			history.overlayType = hand.overlayType;
			history.displaySize = displaySize;
			history.pitchTrimDegrees = a_vr.settings.WandAimPitchTrimDegrees;
			history.usingAimComponent = hand.usingAimComponent;
		}
		if (hand.usingOCUPointer) {
			// The shared ray already includes OCU smoothing; its beam and this cursor must agree.
			history = {};
			hand.screenPosition = ImVec2(hand.uvCoordinates.x * displaySize.x, hand.uvCoordinates.y * displaySize.y);
			hand.hasScreenPosition = true;
			return;
		}
		const WandCursorFilter::Position input{
			hand.uvCoordinates.x * displaySize.x / displaySize.y, hand.uvCoordinates.y
		};
		const int sampleFrame = ImGui::GetFrameCount();
		if (history.sampleFrame != sampleFrame) {
			history.filteredPosition = history.filter.Update(input, a_sampleSeconds, a_holdPosition);
			history.sampleFrame = sampleFrame;
		}
		const auto filtered = history.filteredPosition;
		hand.screenPosition = ImVec2(
			std::clamp(filtered.x * displaySize.y, 0.0f, displaySize.x),
			std::clamp(filtered.y * displaySize.y, 0.0f, displaySize.y));
		hand.hasScreenPosition = true;
	}

	void SampleWandHand(VR& a_vr, ControllerDevice a_controller, bool a_forceCursorUpdate, const WandPoses& a_poses)
	{
		auto& hand = a_vr.wandHandStates[GetControllerSlot(a_controller)];
		hand = {};
		hand.usingOCUPointer = a_vr.ocuPointerAvailable;
		hand.controllerIndex = Util::GetControllerIndexForDevice(a_controller, a_vr.lastKnownLeftHandedMode);
		if (hand.controllerIndex == vr::k_unTrackedDeviceIndexInvalid)
			return;
		if (!TryGetControllerPointingRay(
				hand.controllerIndex,
				a_vr.settings.WandAimPitchTrimDegrees,
				hand.rayOrigin,
				hand.rayDirection,
				hand.usingAimComponent, &a_poses))
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
	bool usedAimComponent = false;
	if (!TryGetControllerPointingRay(a_controllerIndex, settings.WandAimPitchTrimDegrees, rayOrigin, rayDirection, usedAimComponent))
		return false;
	float distance = 0.0f;
	bool usedPresentedSurface = false;
	const bool intersected = TryComputeOverlayIntersection(*this, a_type, rayOrigin, rayDirection, a_outUV, distance, usedPresentedSurface);
	wandState.rayOrigin = rayOrigin;
	wandState.rayDirection = rayDirection;
	wandState.usingAimComponent = usedAimComponent;
	wandState.usingOCUPointer = ocuPointerAvailable;
	wandState.ocuFrameId = ocuPointerFrameId;
	wandState.usingPresentedSurface = usedPresentedSurface;
	return intersected;
}

bool VR::ComputeWandIntersection(vr::TrackedDeviceIndex_t a_controllerIndex, ImVec2& a_outUV)
{
	Vector3 rayOrigin = Vector3::Zero;
	Vector3 rayDirection = Vector3::Zero;
	bool usedAimComponent = false;
	if (!TryGetControllerPointingRay(a_controllerIndex, settings.WandAimPitchTrimDegrees, rayOrigin, rayDirection, usedAimComponent)) {
		wandState.isIntersecting = false;
		return false;
	}
	float distance = 0.0f;
	OverlayType overlayType = OverlayType::HMD;
	bool usedPresentedSurface = false;
	const bool intersected = TryComputeNearestIntersection(*this, rayOrigin, rayDirection, a_outUV, distance, overlayType, usedPresentedSurface);
	wandState.rayOrigin = rayOrigin;
	wandState.rayDirection = rayDirection;
	wandState.usingAimComponent = usedAimComponent;
	wandState.usingOCUPointer = ocuPointerAvailable;
	wandState.ocuFrameId = ocuPointerFrameId;
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
	const auto attachMode = GetEffectiveMenuAttachMode();
	if (attachMode == AttachMode::ControllerOnly || attachMode == AttachMode::Both) {
		return GetEffectiveMenuAttachController() == ControllerDevice::Primary ?
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
	RefreshOCUPointerState();
	if (!CanUseWandPointing() || !globals::menu || !globals::menu->IsEnabled)
		return;
	ImGuiIO& io = ImGui::GetIO();
	io.WantSetMousePos = false;

	WandPoses poses{};
	if (!ocuPointerAvailable && !TryGetWandPoses(poses)) {
		wandState = {};
		wandHandStates = {};
		ResetWandPoseTracking();
		g_controllerRenderModelNames = {};
		return;
	}
	const double sampleSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	for (const ControllerDevice controller : { ControllerDevice::Primary, ControllerDevice::Secondary }) {
		const bool pressed = a_forceCursorUpdate && a_preferredController == controller;
		SampleWandHand(*this, controller, pressed, poses);
		if (!wandHandStates[GetControllerSlot(controller)].poseValid)
			g_wandPoseHistory[GetControllerSlot(controller)] = {};
		UpdateWandScreenPosition(*this, controller, sampleSeconds, pressed && activeWandController == controller);
	}
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

	UpdateOCUPointerOwnership();
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
	wandState.usingAimComponent = activeHand.usingAimComponent;
	wandState.usingOCUPointer = activeHand.usingOCUPointer;
	wandState.ocuFrameId = ocuPointerFrameId;
	wandState.usingPresentedSurface = activeHand.usingPresentedSurface;
	if (!activeHand.isIntersecting)
		return;

	if (!activeHand.hasScreenPosition)
		return;
	const ImVec2 screenPosition = activeHand.screenPosition;
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
		bool usedAimComponent = false;
		if (!TryGetControllerPointingRay(controllerIndex, settings.WandAimPitchTrimDegrees, rayOrigin, rayDirection, usedAimComponent))
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
	if (ocuPointerAvailable && !wandHandStates[GetControllerSlot(a_controller)].ocuOwnsInput)
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
	ReleaseOCUPointerState();
	InvalidatePresentedMenuSurfaces();
	wandState = {};
	wandHandStates = {};
	activeWandController = ControllerDevice::Both;
	capturedWandController = ControllerDevice::Both;
	lastWandHoveredID = 0;
	lastWandHoveredController = ControllerDevice::Both;
	ResetWandPoseTracking();
	g_controllerRenderModelNames = {};
}

void VR::RefreshOCUPointerState()
{
	const bool enabled = CanUseWandPointing() && !settings.VRMenuControllerDiagnosticsTestMode &&
	                     globals::menu && globals::menu->IsEnabled;
	auto* compositor = enabled ? RE::BSOpenVR::GetIVRCompositor() : nullptr;
	const auto trackingOrigin = compositor ? static_cast<std::uint32_t>(compositor->GetTrackingSpace()) : 0u;
	g_ocuPointerFrame = g_ocuPointerClient.ReadForFrame(static_cast<std::uint64_t>(ImGui::GetFrameCount() + 1),
		trackingOrigin, settings.WandAimPitchTrimDegrees, enabled && compositor);
	ocuPointerAvailable = g_ocuPointerFrame.providerAvailable;
	ocuPointerValid = g_ocuPointerFrame.valid;
	ocuPointerGeneration = g_ocuPointerClient.GetGeneration();
	ocuPointerFrameId = ocuPointerValid ? g_ocuPointerFrame.snapshot.frameId : 0;
	ocuPointerOwnedHands = g_ocuPointerClient.GetOwnedHands();
	if (ocuPointerValid) {
		const auto session = g_ocuPointerFrame.snapshot.sessionId;
		if (g_ocuPointerSession != 0 && g_ocuPointerSession != session) {
			InvalidatePresentedMenuSurfaces();
			ResetWandPoseTracking();
			g_ocuNeedsPresentedSurface = true;
		}
		g_ocuPointerSession = session;
	}
}

void VR::UpdateOCUPointerOwnership(bool a_presentationResolved)
{
	if (!ocuPointerAvailable)
		return;
	std::uint32_t handMask = 0;
	std::uint32_t hitMask = 0;
	float distances[2]{};
	for (const auto controller : { ControllerDevice::Primary, ControllerDevice::Secondary }) {
		const auto& hand = wandHandStates[GetControllerSlot(controller)];
		const auto physicalHand = GetPhysicalHand(*this, controller);
		if (hand.poseValid && (hand.isIntersecting || capturedWandController == controller))
			handMask |= 1u << physicalHand;
		const bool currentDisplayedHit = a_presentationResolved && IsMenuPointerInHeadset() &&
		                                 customVRCursorVisible && wandState.isIntersecting && wandState.usingOCUPointer &&
		                                 wandState.ocuFrameId == ocuPointerFrameId && hand.hasScreenPosition &&
		                                 customVRCursorPos.x == hand.screenPosition.x && customVRCursorPos.y == hand.screenPosition.y &&
		                                 customVRCursorOverlayType == hand.overlayType;
		if (currentDisplayedHit && hand.isIntersecting && activeWandController == controller) {
			hitMask |= 1u << physicalHand;
			distances[physicalHand] = hand.hitDistance;
		}
	}
	g_ocuPointerClient.UpdateOwnership(handMask, hitMask, distances);
	g_ocuPointerFrame = g_ocuPointerClient.GetFrame();
	ocuPointerValid = g_ocuPointerFrame.valid;
	ocuPointerOwnedHands = g_ocuPointerClient.GetOwnedHands();
	ocuPointerGeneration = g_ocuPointerClient.GetGeneration();
	for (const auto controller : { ControllerDevice::Primary, ControllerDevice::Secondary }) {
		auto& hand = wandHandStates[GetControllerSlot(controller)];
		const auto physicalHand = GetPhysicalHand(*this, controller);
		hand.ocuOwnsInput = (ocuPointerOwnedHands & (1u << physicalHand)) != 0;
		hand.ocuTriggerDown = g_ocuPointerClient.TriggerDown(physicalHand);
		hand.ocuTriggerEligible = g_ocuPointerClient.TriggerEligible(physicalHand);
		hand.ocuTriggerSequence = g_ocuPointerClient.TriggerSequence(physicalHand);
		hand.ocuOwnershipGeneration = g_ocuPointerClient.GetHandGeneration(physicalHand);
		if (!hand.ocuOwnsInput) {
			hand.isIntersecting = false;
			hand.hasScreenPosition = false;
		}
	}
}

void VR::ReleaseOCUPointerState()
{
	g_ocuPointerClient.Release();
	g_ocuPointerFrame = {};
	ocuPointerAvailable = false;
	ocuPointerValid = false;
	ocuPointerGeneration = g_ocuPointerClient.GetGeneration();
	ocuPointerFrameId = 0;
	ocuPointerOwnedHands = 0;
	wandState = {};
	wandHandStates = {};
	g_ocuPointerSession = 0;
	g_ocuNeedsPresentedSurface = false;
}

ImVec4 VR::GetWandPointerColor() const
{
	return ResolveWandPointerColor(*this, false);
}

ImVec4 VR::GetWandPointerDotColor() const
{
	return ResolveWandPointerColor(*this, true);
}
