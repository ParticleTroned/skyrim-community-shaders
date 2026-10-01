#include "ImGuiVRHelperDisplayCamera.h"

#include "Utils/VRUtils.h"

#include <array>
#include <chrono>
#include <cmath>

#ifdef _MSC_VER
#	pragma float_control(precise, on, push)
#endif

namespace ImGuiVRHelperDisplayCamera
{
	namespace
	{
		namespace Scene = ImGuiVRHelperScenePacket;

		Scene::Matrix4x4 ToSceneMatrix(const Matrix& a_matrix) noexcept
		{
			Scene::Matrix4x4 result{};
			for (std::size_t row = 0; row < 4; ++row)
				for (std::size_t column = 0; column < 4; ++column)
					result[row * 4 + column] = a_matrix.m[row][column];
			return result;
		}

		struct DisplayProperties
		{
			vr::IVRSystem* system = nullptr;
			std::array<Matrix, 2> eyeToHead{};
			std::array<Matrix, 2> projection{};
			std::chrono::steady_clock::time_point retryAfter{};
			bool valid = false;
		};

		const DisplayProperties& GetDisplayProperties(vr::IVRSystem* a_system)
		{
			thread_local DisplayProperties cached;
			if (cached.system == a_system && (cached.valid || std::chrono::steady_clock::now() < cached.retryAfter))
				return cached;
			cached = {};
			cached.system = a_system;
			cached.retryAfter = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				const auto vrEye = static_cast<vr::EVREye>(eye);
				const auto eyeToHead = Util::HmdMatrix34ToMatrix(a_system->GetEyeToHeadTransform(vrEye));
				if (!Scene::IsInvertible(ToSceneMatrix(eyeToHead)))
					return cached;
				float left = 0, right = 0, bottom = 0, top = 0;
				// Match the existing overlay's tangent ordering and DirectX right-handed depth convention.
				a_system->GetProjectionRaw(vrEye, &left, &right, &bottom, &top);
				if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(top) ||
					right <= left || top <= bottom)
					return cached;
				constexpr float nearPlane = 0.1f;
				constexpr float farPlane = 1000.0f;
				const Matrix projection = DirectX::XMMatrixPerspectiveOffCenterRH(
					left * nearPlane, right * nearPlane, bottom * nearPlane, top * nearPlane, nearPlane, farPlane);
				if (!Scene::IsInvertible(ToSceneMatrix(projection)))
					return cached;
				cached.eyeToHead[eye] = eyeToHead;
				cached.projection[eye] = projection;
			}
			cached.valid = true;
			return cached;
		}
	}

	std::optional<ImGuiVRHelperSceneCapture::Snapshot> CaptureDisplayCamera() noexcept
	{
		try {
			if (!REL::Module::IsVR())
				return std::nullopt;
			auto* openvr = RE::BSOpenVR::GetSingleton();
			if (!openvr || !openvr->vrSystem)
				return std::nullopt;
			const auto& properties = GetDisplayProperties(openvr->vrSystem);
			if (!properties.valid)
				return std::nullopt;
			auto* compositor = RE::BSOpenVR::GetIVRCompositor();
			if (!compositor)
				compositor = openvr->vrContext.vrCompositor;
			if (!compositor)
				return std::nullopt;
			std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
			if (compositor->GetLastPoses(poses.data(), static_cast<std::uint32_t>(poses.size()), nullptr, 0) != vr::VRCompositorError_None)
				return std::nullopt;
			const auto& pose = poses[vr::k_unTrackedDeviceIndex_Hmd];
			if (!pose.bPoseIsValid || !pose.bDeviceIsConnected)
				return std::nullopt;
			const Matrix head = Util::HmdMatrix34ToMatrix(pose.mDeviceToAbsoluteTracking);
			ImGuiVRHelperSceneCapture::Snapshot snapshot;
			snapshot.headToTracking = ToSceneMatrix(head);
			if (!Scene::IsInvertible(snapshot.headToTracking))
				return std::nullopt;
			snapshot.worldToTracking = ToSceneMatrix(Matrix::Identity);
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				const Matrix eyeToTracking = properties.eyeToHead[eye] * head;
				if (!Scene::IsInvertible(ToSceneMatrix(eyeToTracking)))
					return std::nullopt;
				const Matrix trackingToClip = eyeToTracking.Invert() * properties.projection[eye];
				const auto colorClip = ToSceneMatrix(trackingToClip);
				if (!Scene::IsInvertible(colorClip))
					return std::nullopt;
				snapshot.eyes[eye].trackingToNativeColorClip = colorClip;
				snapshot.eyes[eye].trackingToReconstructedColorClip = colorClip;
			}
			return snapshot;
		} catch (...) {
			return std::nullopt;
		}
	}
}

#ifdef _MSC_VER
#	pragma float_control(pop)
#endif
