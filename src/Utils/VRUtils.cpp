#include "VRUtils.h"
#include "Features/VR.h"  // For ButtonCombo and ControllerDevice definitions
#include "RE/B/BSOpenVR.h"
#include "UI.h"
#include <cmath>
#include <imgui.h>

namespace Util
{
	namespace
	{
		struct OverlayBasisVector
		{
			float x;
			float y;
			float z;
		};

		OverlayBasisVector Cross(const OverlayBasisVector& a, const OverlayBasisVector& b)
		{
			return {
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x
			};
		}

		OverlayBasisVector NormalizeOr(const OverlayBasisVector& value, const OverlayBasisVector& fallback)
		{
			const float lengthSq = value.x * value.x + value.y * value.y + value.z * value.z;
			if (lengthSq <= 1.0e-6f)
				return fallback;

			const float invLength = 1.0f / std::sqrt(lengthSq);
			return { value.x * invLength, value.y * invLength, value.z * invLength };
		}
	}

	void DrawButtonCombo(const std::vector<ButtonCombo>& combo, bool showControllerLabels)
	{
		bool anyDrawn = false;
		for (size_t i = 0; i < combo.size(); ++i) {
			if (combo[i].GetKey() == 0)
				continue;
			if (i > 0) {
				ImGui::SameLine();
				ImGui::Text("+");
				ImGui::SameLine();
			}
			ImVec4 color;
			switch (combo[i].GetDevice()) {
			case InputDeviceType::Primary:
				color = Util::GetControllerPrimaryColor();
				break;
			case InputDeviceType::Secondary:
				color = Util::GetControllerSecondaryColor();
				break;
			case InputDeviceType::Both:
				color = Util::GetControllerBothColor();
				break;
			default:
				color = Util::GetControllerDefaultColor();
				break;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", RE::GetOpenVRButtonName(combo[i].GetKey()));
			ImGui::PopStyleColor();
			anyDrawn = true;
			if (showControllerLabels) {
				ImGui::SameLine();
				ImVec4 labelColor = Util::GetControllerDefaultColor();
				const char* label = "";
				switch (combo[i].GetDevice()) {
				case InputDeviceType::Primary:
					label = "(Primary Controller)";
					labelColor = Util::GetControllerPrimaryColor();
					break;
				case InputDeviceType::Secondary:
					label = "(Secondary Controller)";
					labelColor = Util::GetControllerSecondaryColor();
					break;
				case InputDeviceType::Both:
					label = "(Both Controllers)";
					labelColor = Util::GetControllerBothColor();
					break;
				default:
					break;
				}
				ImGui::TextColored(labelColor, "%s", label);
				if (i < combo.size() - 1)
					ImGui::SameLine();
			}
		}
		if (anyDrawn) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				Util::DrawColoredMultiLineTooltip({ { "Color coding:", Util::GetControllerDefaultColor() },
					{ "Yellow = Primary controller", Util::GetControllerPrimaryColor() },
					{ "Blue = Secondary controller", Util::GetControllerSecondaryColor() },
					{ "Green = Both controllers (Yellow + Blue)", Util::GetControllerBothColor() } });
			}
		}
	}

	vr::HmdMatrix34_t BuildLevelOverlayTransform(const vr::HmdMatrix34_t& hmdTransform, float offsetX, float offsetY, float offsetZ)
	{
		const OverlayBasisVector worldUp{ 0.0f, 1.0f, 0.0f };
		const OverlayBasisVector fallbackBack{ 0.0f, 0.0f, 1.0f };
		const OverlayBasisVector fallbackRight{ 1.0f, 0.0f, 0.0f };
		const float kMinHorizontalLengthSq = 1.0e-6f;

		OverlayBasisVector projectedBack{ hmdTransform.m[0][2], 0.0f, hmdTransform.m[2][2] };
		OverlayBasisVector projectedRight{ hmdTransform.m[0][0], 0.0f, hmdTransform.m[2][0] };

		const float projectedBackLenSq = projectedBack.x * projectedBack.x + projectedBack.z * projectedBack.z;
		const float projectedRightLenSq = projectedRight.x * projectedRight.x + projectedRight.z * projectedRight.z;

		OverlayBasisVector back = fallbackBack;
		if (projectedBackLenSq > kMinHorizontalLengthSq) {
			back = NormalizeOr(projectedBack, fallbackBack);
		} else if (projectedRightLenSq > kMinHorizontalLengthSq) {
			OverlayBasisVector rightFromHmd = NormalizeOr(projectedRight, fallbackRight);
			back = NormalizeOr(Cross(rightFromHmd, worldUp), fallbackBack);
		}

		OverlayBasisVector right = NormalizeOr(Cross(worldUp, back), fallbackRight);
		OverlayBasisVector up = NormalizeOr(Cross(back, right), worldUp);

		vr::HmdMatrix34_t transform = {};
		transform.m[0][0] = right.x;
		transform.m[0][1] = up.x;
		transform.m[0][2] = back.x;
		transform.m[0][3] = hmdTransform.m[0][3] + right.x * offsetX + up.x * offsetY + back.x * offsetZ;

		transform.m[1][0] = right.y;
		transform.m[1][1] = up.y;
		transform.m[1][2] = back.y;
		transform.m[1][3] = hmdTransform.m[1][3] + right.y * offsetX + up.y * offsetY + back.y * offsetZ;

		transform.m[2][0] = right.z;
		transform.m[2][1] = up.z;
		transform.m[2][2] = back.z;
		transform.m[2][3] = hmdTransform.m[2][3] + right.z * offsetX + up.z * offsetY + back.z * offsetZ;

		return transform;
	}

	void ScaleOverlayTransform(vr::HmdMatrix34_t& transform, float width, float height)
	{
		for (int row = 0; row < 3; ++row) {
			transform.m[row][0] *= width;
			transform.m[row][1] *= height;
		}
	}

	vr::HmdMatrix34_t ComputeOverlayTransformFromHMD(float offsetX, float offsetY, float offsetZ)
	{
		// Initialize as identity matrix to ensure valid transform on early returns
		vr::HmdMatrix34_t transform = {};
		transform.m[0][0] = 1.0f;
		transform.m[1][1] = 1.0f;
		transform.m[2][2] = 1.0f;
		// All other elements remain 0.0f from the {} initialization

		// Use the same OpenVR access pattern as the VR class
		RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
		if (!openvr)
			return transform;

		auto* system = openvr->vrSystem;
		if (!system)
			return transform;

		vr::TrackedDevicePose_t hmdPose;
		if (!GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, &hmdPose, 1))
			return transform;
		if (!hmdPose.bPoseIsValid)
			return transform;

		return BuildLevelOverlayTransform(hmdPose.mDeviceToAbsoluteTracking, offsetX, offsetY, offsetZ);
	}

	void SetOverlayInputFlags(vr::IVROverlay* overlay, vr::VROverlayHandle_t handle)
	{
		if (!overlay || handle == vr::k_ulOverlayHandleInvalid)
			return;

		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRScrollEvents, true);
		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
		overlay->SetOverlayFlag(handle, vr::VROverlayFlags_VisibleInDashboard, true);
	}

	vr::HmdMatrix34_t CreateControllerOverlayTransform(float offsetX, float offsetY, float offsetZ, float width, float height)
	{
		vr::HmdMatrix34_t transform;
		transform.m[0][0] = width;
		transform.m[0][1] = 0.0f;
		transform.m[0][2] = 0.0f;
		transform.m[0][3] = offsetX;
		transform.m[1][0] = 0.0f;
		transform.m[1][1] = height;
		transform.m[1][2] = 0.0f;
		transform.m[1][3] = offsetY;
		transform.m[2][0] = 0.0f;
		transform.m[2][1] = 0.0f;
		transform.m[2][2] = 1.0f;
		transform.m[2][3] = offsetZ;
		return transform;
	}

	//=============================================================================
	// NEW ACTIVE FUNCTIONS FROM VR.CPP
	//=============================================================================

	// NOTE: OpenComposite Compatibility
	// The functions below provide compatibility with OpenComposite, which has issues
	// with GetDeviceToAbsoluteTrackingPose when requesting poses. We completely avoid
	// using GetDeviceToAbsoluteTrackingPose and instead use VRCompositor interfaces
	// obtained through BSOpenVR to avoid static linking issues on non-VR systems.

	OpenVRContext::OpenVRContext(bool requireOverlay)
	{
		openvr = RE::BSOpenVR::GetSingleton();
		if (openvr) {
			system = openvr->vrSystem;
			overlay = openvr->vrContext.vrOverlay;
			if (requireOverlay) {
				EnsureOverlay();
			}
		}
	}

	bool OpenVRContext::EnsureOverlay()
	{
		if (!IsValid()) {
			return false;
		}

		if (!overlay) {
			overlay = RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext);
		}
		return overlay != nullptr;
	}

	vr::TrackedDeviceIndex_t GetControllerIndexForDevice(InputDeviceType device, bool isLeftHanded)
	{
		OpenVRContext ctx;
		if (!ctx.IsValid())
			return vr::k_unTrackedDeviceIndexInvalid;

		// Determine the OpenVR role based on handedness and our device enum
		vr::ETrackedControllerRole targetRole;

		if (device == InputDeviceType::Primary) {
			// Primary controller = dominant hand
			targetRole = isLeftHanded ? vr::ETrackedControllerRole::TrackedControllerRole_LeftHand : vr::ETrackedControllerRole::TrackedControllerRole_RightHand;
		} else {
			// Secondary controller = non-dominant hand
			targetRole = isLeftHanded ? vr::ETrackedControllerRole::TrackedControllerRole_RightHand : vr::ETrackedControllerRole::TrackedControllerRole_LeftHand;
		}

		// Find controller with the target role
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
			if (ctx.system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
				if (ctx.system->GetControllerRoleForTrackedDeviceIndex(i) == targetRole) {
					return i;
				}
			}
		}
		return vr::k_unTrackedDeviceIndexInvalid;
	}

	bool GetControllerWorldMatrix(vr::TrackedDeviceIndex_t index, float out[3][4])
	{
		OpenVRContext ctx;
		if (!ctx.IsValid())
			return false;

		vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
		if (!GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, poses, vr::k_unMaxTrackedDeviceCount))
			return false;

		if (!poses[index].bPoseIsValid)
			return false;

		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 4; ++j)
				out[i][j] = poses[index].mDeviceToAbsoluteTracking.m[i][j];
		return true;
	}

	bool GetDeviceToAbsoluteTrackingPoseCompatible(vr::ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow, vr::TrackedDevicePose_t* pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount)
	{
		(void)fPredictedSecondsToPhotonsFromNow;
		(void)eOrigin;
		OpenVRContext ctx;
		if (!ctx.IsValid())
			return false;

		// For single device requests (common with HMD pose requests),
		// use a full pose array to ensure OpenComposite compatibility
		if (unTrackedDevicePoseArrayCount == 1) {
			vr::TrackedDevicePose_t allPoses[vr::k_unMaxTrackedDeviceCount];

			// Try to use compositor interface first for better OpenComposite compatibility
			// Use BSOpenVR's method to avoid static linking issues
			auto* compositor = RE::BSOpenVR::GetIVRCompositor();
			if (!compositor && ctx.openvr) {
				// Fallback to compositor from the context
				compositor = ctx.openvr->vrContext.vrCompositor;
			}

			if (compositor) {
				// For OpenComposite compatibility, try to use GetLastPoses which is more stable
				auto error = compositor->GetLastPoses(allPoses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
				if (error == vr::VRCompositorError_None) {
					// Copy HMD pose (index 0) to output
					pTrackedDevicePoseArray[0] = allPoses[0];
					return true;
				}
				// Fallback to WaitGetPoses if GetLastPoses fails
				error = compositor->WaitGetPoses(allPoses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
				if (error == vr::VRCompositorError_None) {
					// Copy HMD pose (index 0) to output
					pTrackedDevicePoseArray[0] = allPoses[0];
					return true;
				}
			}

			// If compositor methods failed, return false rather than using the problematic direct call
			return false;
		}

		// For full device array requests, try compositor first
		// Use BSOpenVR's method to avoid static linking issues
		auto* compositor = RE::BSOpenVR::GetIVRCompositor();
		if (!compositor && ctx.openvr) {
			// Fallback to compositor from the context
			compositor = ctx.openvr->vrContext.vrCompositor;
		}

		if (compositor) {
			// For OpenComposite compatibility, try to use GetLastPoses which is more stable
			auto error = compositor->GetLastPoses(pTrackedDevicePoseArray, unTrackedDevicePoseArrayCount, nullptr, 0);
			if (error == vr::VRCompositorError_None) {
				return true;
			}
			// Fallback to WaitGetPoses if GetLastPoses fails
			error = compositor->WaitGetPoses(pTrackedDevicePoseArray, unTrackedDevicePoseArrayCount, nullptr, 0);
			if (error == vr::VRCompositorError_None) {
				return true;
			}
		}

		// If compositor methods failed, return false rather than using the problematic direct call
		return false;
	}

}
