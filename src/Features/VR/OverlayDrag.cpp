#include "Features/VR.h"
#include "Features/VR/MenuPositioningPolicy.h"
#include "RE/B/BSOpenVR.h"
#include "RE/P/PlayerCharacter.h"
#include "Utils/VRUtils.h"

#include <SimpleMath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <openvr.h>

using namespace DirectX::SimpleMath;
using AttachMode = VR::Settings::OverlayAttachMode;

namespace
{
	bool IsFiniteVector(const Vector3& a_value)
	{
		return std::isfinite(a_value.x) &&
		       std::isfinite(a_value.y) &&
		       std::isfinite(a_value.z);
	}

	bool TryApplyOffsetDelta(
		const Vector3& a_initialOffset,
		const Vector3& a_delta,
		Vector3& a_offset)
	{
		const Vector3 candidate = a_initialOffset + a_delta;
		if (!IsFiniteVector(candidate))
			return false;

		a_offset = {
			VR::Config::SanitizeMenuOffset(candidate.x, a_initialOffset.x),
			VR::Config::SanitizeMenuOffset(candidate.y, a_initialOffset.y),
			VR::Config::SanitizeMenuOffset(candidate.z, a_initialOffset.z),
		};
		return true;
	}

	bool TryGetHMDWorld(Matrix& a_hmdWorld)
	{
		vr::TrackedDevicePose_t hmdPose{};
		if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(
				vr::TrackingUniverseStanding,
				0,
				&hmdPose,
				1) ||
			!hmdPose.bPoseIsValid) {
			return false;
		}

		a_hmdWorld = Util::HmdMatrix34ToMatrix(
			hmdPose.mDeviceToAbsoluteTracking);
		return true;
	}

	bool MakeVerticalFacingTransform(
		const Vector3& a_anchor,
		const Vector3& a_hmdPosition,
		Matrix& a_transform)
	{
		Vector3 panelToPlayer{
			a_hmdPosition.x - a_anchor.x,
			0.0f,
			a_hmdPosition.z - a_anchor.z,
		};
		if (!IsFiniteVector(a_anchor) ||
			!IsFiniteVector(a_hmdPosition) ||
			panelToPlayer.LengthSquared() < 1e-6f) {
			return false;
		}
		panelToPlayer.Normalize();

		const Vector3 up = Vector3::UnitY;
		Vector3 right = up.Cross(panelToPlayer);
		right.Normalize();
		a_transform = Matrix(
			right.x, right.y, right.z, 0.0f,
			up.x, up.y, up.z, 0.0f,
			panelToPlayer.x, panelToPlayer.y, panelToPlayer.z, 0.0f,
			a_anchor.x, a_anchor.y, a_anchor.z, 1.0f);
		return true;
	}
}

bool VR::GetGripPressed(bool isLeft, bool isRight) const
{
	bool isLeftHanded = lastKnownLeftHandedMode;

	if (isLeft) {
		if (isLeftHanded) {
			return primaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		} else {
			return secondaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		}
	}
	if (isRight) {
		if (isLeftHanded) {
			return secondaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		} else {
			return primaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		}
	}
	return false;
}

static bool CanStartAny(vr::ETrackedControllerRole role)
{
	return role != vr::TrackedControllerRole_Invalid;
}

void VR::UpdateOverlayDrag()
{
	if (!CanPerformDrag()) {
		overlayDragState = {};
		return;
	}

	if (overlayDragState.dragging) {
		UpdateActiveDrag();
	} else {
		TryStartNewDrag();
	}
}

bool VR::CanPerformDrag()
{
	if (!VRMenuPositioningPolicy::ShouldAllowOverlayDrag(
			settings.UnlockMenuPositionAndSize,
			settings.EnableDragToReposition))
		return false;

	if (!globals::menu || !globals::menu->IsEnabled)
		return false;

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return false;

	if (settings.VRMenuControllerDiagnosticsTestMode) {
		return false;
	}

	return true;
}

void VR::UpdateActiveDrag()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system) {
		overlayDragState = {};
		return;
	}

	auto resetDragState = [&]() {
		overlayDragState.dragging = false;
		overlayDragState.controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		overlayDragState.isPrimary = false;
		overlayDragState.isSecondary = false;
	};

	float rawMatrix[3][4];
	if (!Util::GetControllerWorldMatrix(overlayDragState.controllerIndex, rawMatrix)) {
		resetDragState();
		return;
	}
	vr::HmdMatrix34_t mat = Util::Float3x4ToHmdMatrix34(rawMatrix);
	Matrix controllerMatrix = Util::HmdMatrix34ToMatrix(mat);
	if (!IsFiniteVector(controllerMatrix.Translation())) {
		resetDragState();
		return;
	}

	switch (overlayDragState.mode) {
	case OverlayDragState::DragMode::Controller:
		{
			vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(GetEffectiveMenuAttachController(), lastKnownLeftHandedMode);

			if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
				float attachedM[3][4];
				if (!Util::GetControllerWorldMatrix(attachedControllerIndex, attachedM)) {
					resetDragState();
					return;
				}
				{
					Matrix attachedControllerMatrix = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(attachedM));

					Vector3 worldDelta(
						controllerMatrix._41 - overlayDragState.initialControllerMatrix._41,
						controllerMatrix._42 - overlayDragState.initialControllerMatrix._42,
						controllerMatrix._43 - overlayDragState.initialControllerMatrix._43);

					Matrix worldToLocal = attachedControllerMatrix.Invert();
					Vector3 localDelta = Vector3::TransformNormal(worldDelta, worldToLocal);
					Vector3 offset;
					if (!IsFiniteVector(worldDelta) ||
						!TryApplyOffsetDelta(overlayDragState.initialControllerOffset, localDelta, offset)) {
						resetDragState();
						return;
					}

					settings.VRMenuControllerOffsetX = offset.x;
					settings.VRMenuControllerOffsetY = offset.y;
					settings.VRMenuControllerOffsetZ = offset.z;
				}
			} else {
				resetDragState();
				return;
			}
			break;
		}
	case OverlayDragState::DragMode::FixedWorld:
		{
			Vector3 worldDelta(
				controllerMatrix._41 - overlayDragState.initialControllerMatrix._41,
				controllerMatrix._42 - overlayDragState.initialControllerMatrix._42,
				controllerMatrix._43 - overlayDragState.initialControllerMatrix._43);
			if (!IsFiniteVector(worldDelta)) {
				resetDragState();
				return;
			}
			Matrix translated = overlayDragState.initialOverlayMatrix;
			translated._41 += worldDelta.x;
			translated._42 += worldDelta.y;
			translated._43 += worldDelta.z;
			if (!IsFiniteVector(translated.Translation())) {
				resetDragState();
				return;
			}
			fixedWorldOverlayPosition.m = translated;
			break;
		}
	case OverlayDragState::DragMode::HMD:
		{
			vr::TrackedDevicePose_t hmdPose{};
			if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(
					vr::TrackingUniverseStanding,
					0,
					&hmdPose,
					1) ||
				!hmdPose.bPoseIsValid) {
				resetDragState();
				return;
			}
			Matrix hmdMatrix = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);

			Vector3 worldDelta(
				controllerMatrix._41 - overlayDragState.initialControllerMatrix._41,
				controllerMatrix._42 - overlayDragState.initialControllerMatrix._42,
				controllerMatrix._43 - overlayDragState.initialControllerMatrix._43);

			Matrix worldToLocal = hmdMatrix.Invert();
			Vector3 localDelta = Vector3::TransformNormal(worldDelta, worldToLocal);
			Vector3 offset;
			if (!IsFiniteVector(worldDelta) ||
				!TryApplyOffsetDelta(overlayDragState.initialHMDOffset, localDelta, offset)) {
				resetDragState();
				return;
			}

			static auto lastDeltaLog = std::chrono::steady_clock::now();
			auto nowDelta = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(nowDelta - lastDeltaLog).count() > 500) {
				logger::debug("VR Drag Delta - Local: ({:.3f}, {:.3f}, {:.3f})", localDelta.x, localDelta.y, localDelta.z);
				lastDeltaLog = nowDelta;
			}

			settings.VRMenuOffsetX = offset.x;
			settings.VRMenuOffsetY = offset.y;
			settings.VRMenuOffsetZ = offset.z;
			settings.VRMenuScale = overlayDragState.initialHMDScale;

			static std::chrono::steady_clock::time_point lastLog = std::chrono::steady_clock::now();
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLog).count() > 500) {
				logger::debug("VR Dragging (3D Mode): Offset ({:.2f}, {:.2f}, {:.2f}), Scale {:.2f}",
					settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ, settings.VRMenuScale);
				lastLog = now;
			}
			break;
		}
	default:
		break;
	}

	bool gripPressed = GetGripPressed(overlayDragState.isPrimary, overlayDragState.isSecondary);
	if (!gripPressed) {
		resetDragState();
	}
}

void VR::TryStartNewDrag()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return;

	struct DragMode
	{
		OverlayDragState::DragMode mode;
		bool isActive;
		std::function<bool(vr::ETrackedControllerRole)> canStart;
		std::function<void()> onInit;
	};

	std::vector<DragMode> dragModes;
	const auto attachMode = GetEffectiveMenuAttachMode();
	const auto attachController = GetEffectiveMenuAttachController();

	// Controller mode - only for opposite hand (highest priority)
	if (attachMode == AttachMode::ControllerOnly || attachMode == AttachMode::Both) {
		dragModes.push_back({ OverlayDragState::DragMode::Controller,
			true,
			[&](vr::ETrackedControllerRole role) {
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(attachController, lastKnownLeftHandedMode);
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid)
					return false;

				ControllerDevice oppositeDevice = (attachController == ControllerDevice::Primary) ?
			                                          ControllerDevice::Secondary :
			                                          ControllerDevice::Primary;
				vr::TrackedDeviceIndex_t oppositeControllerIndex = Util::GetControllerIndexForDevice(oppositeDevice, lastKnownLeftHandedMode);
				if (oppositeControllerIndex == vr::k_unTrackedDeviceIndexInvalid)
					return false;

				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (deviceRole == role && i == oppositeControllerIndex)
							return true;
					}
				}
				return false;
			},
			[&]() {
				overlayDragState.initialControllerOffset.x = settings.VRMenuControllerOffsetX;
				overlayDragState.initialControllerOffset.y = settings.VRMenuControllerOffsetY;
				overlayDragState.initialControllerOffset.z = settings.VRMenuControllerOffsetZ;
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
			} });
	}

	// Fixed world mode
	if (UseFixedWorldMenuPositioning() && fixedWorldOverlayPosition.initialized) {
		std::function<bool(vr::ETrackedControllerRole)> fixedWorldCanStart;
		if (attachMode == AttachMode::Both) {
			fixedWorldCanStart = [&](vr::ETrackedControllerRole role) {
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(attachController, lastKnownLeftHandedMode);
				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::ETrackedControllerRole actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(attachedControllerIndex);
					return role == actualAttachedRole;
				}
				return false;
			};
		} else {
			fixedWorldCanStart = CanStartAny;
		}

		dragModes.push_back({ OverlayDragState::DragMode::FixedWorld,
			true,
			fixedWorldCanStart,
			[&]() {
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
				overlayDragState.initialOverlayMatrix = fixedWorldOverlayPosition.m;
			} });
	}

	// HMD mode
	if (attachMode == AttachMode::HMDOnly || attachMode == AttachMode::Both) {
		std::function<bool(vr::ETrackedControllerRole)> hmdCanStart;
		if (attachMode == AttachMode::Both) {
			hmdCanStart = [&](vr::ETrackedControllerRole role) {
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(attachController, lastKnownLeftHandedMode);
				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::ETrackedControllerRole actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(attachedControllerIndex);
					return role == actualAttachedRole;
				}
				return false;
			};
		} else {
			hmdCanStart = CanStartAny;
		}

		dragModes.push_back({ OverlayDragState::DragMode::HMD,
			true,
			hmdCanStart,
			[&]() {
				overlayDragState.initialHMDOffset.x = settings.VRMenuOffsetX;
				overlayDragState.initialHMDOffset.y = settings.VRMenuOffsetY;
				overlayDragState.initialHMDOffset.z = settings.VRMenuOffsetZ;
				overlayDragState.initialHMDScale = settings.VRMenuScale;
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
			} });
	}

	for (const auto& mode : dragModes) {
		if (!mode.isActive)
			continue;
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
			if (system->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller)
				continue;
			vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
			bool isLeft = (role == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
			bool isRight = (role == vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
			if (!mode.canStart(role))
				continue;
			bool gripPressed = GetGripPressed(isLeft, isRight);
			if (!gripPressed)
				continue;
			float rawMatrix[3][4];
			if (!Util::GetControllerWorldMatrix(i, rawMatrix))
				continue;
			vr::HmdMatrix34_t mat = Util::Float3x4ToHmdMatrix34(rawMatrix);
			Matrix controllerMatrix = Util::HmdMatrix34ToMatrix(mat);
			if (!IsFiniteVector(controllerMatrix.Translation()))
				continue;
			overlayDragState.dragging = true;
			overlayDragState.mode = mode.mode;
			overlayDragState.controllerIndex = i;
			overlayDragState.isPrimary = isLeft;
			overlayDragState.isSecondary = isRight;
			overlayDragState.startControllerMatrix = controllerMatrix;
			mode.onInit();

			if (system && globals::menu->IsEnabled) {
				for (vr::TrackedDeviceIndex_t deviceIdx = 0; deviceIdx < vr::k_unMaxTrackedDeviceCount; ++deviceIdx) {
					if (system->GetTrackedDeviceClass(deviceIdx) == vr::TrackedDeviceClass_Controller) {
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(deviceIdx);
						bool isRightController = (deviceRole == vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
						if (isRightController == isRight) {
							openvr->TriggerHapticPulse(isRightController, 25.0f);
							break;
						}
					}
				}
			}

			return;
		}
	}
}

void VR::SetFixedOverlayToCurrentHMD()
{
	fixedWorldOverlayReanchorRequested = true;
	Matrix hmdWorld;
	if (!TryGetHMDWorld(hmdWorld))
		return;
	UpdateFixedWorldPositioning(hmdWorld);
}

void VR::RequestFixedWorldMenuReanchor()
{
	fixedWorldOverlayReanchorRequested = true;
}

void VR::UpdateFixedWorldPositioning(const Matrix& a_hmdWorld)
{
	if (!UseFixedWorldMenuPositioning())
		return;

	const Vector3 hmdPosition = a_hmdWorld.Translation();
	if (!IsFiniteVector(hmdPosition))
		return;
	if (!fixedWorldOverlayPosition.initialized || fixedWorldOverlayReanchorRequested) {
		// The safe anchor uses fixed defaults, rather than accumulating mutable
		// offsets, so opening the menu can always recover it.
		Vector3 hmdBackward{ a_hmdWorld._31, 0.0f, a_hmdWorld._33 };
		if (!IsFiniteVector(hmdBackward) || hmdBackward.LengthSquared() < 1e-6f)
			hmdBackward = Vector3::UnitZ;
		else
			hmdBackward.Normalize();
		Vector3 hmdRight = Vector3::UnitY.Cross(hmdBackward);
		if (hmdRight.LengthSquared() < 1e-6f)
			hmdRight = Vector3::UnitX;
		else
			hmdRight.Normalize();

		const Vector3 anchor{
			hmdPosition.x + hmdRight.x * Config::kDefaultHMDOffsetX + hmdBackward.x * Config::kDefaultHMDOffsetZ,
			hmdPosition.y,
			hmdPosition.z + hmdRight.z * Config::kDefaultHMDOffsetX + hmdBackward.z * Config::kDefaultHMDOffsetZ,
		};
		Matrix transform;
		if (!MakeVerticalFacingTransform(anchor, hmdPosition, transform))
			return;

		fixedWorldOverlayPosition.m = transform;
		fixedWorldOverlayPosition.initialized = true;
		fixedWorldOverlayReanchorRequested = false;
		return;
	}

	if (!VRMenuPositioningPolicy::ShouldTrackHMDYaw(settings.UnlockMenuPositionAndSize))
		return;

	// Preserve the anchor exactly; only yaw the vertical panel toward the HMD.
	const Vector3 anchor = fixedWorldOverlayPosition.m.Translation();
	Matrix facingTransform;
	if (MakeVerticalFacingTransform(anchor, hmdPosition, facingTransform))
		fixedWorldOverlayPosition.m = facingTransform;
}

void VR::UpdateFixedWorldPositioning()
{
	if (!UseFixedWorldMenuPositioning())
		return;

	Matrix hmdWorld;
	if (!TryGetHMDWorld(hmdWorld))
		return;
	UpdateFixedWorldPositioning(hmdWorld);
}
