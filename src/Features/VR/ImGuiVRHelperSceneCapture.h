#pragma once

#include "ImGuiVRHelperScenePacket.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <memory>

namespace ImGuiVRHelperSceneCapture
{
	namespace Scene = ImGuiVRHelperScenePacket;

	/// An owned copy whose pixels remain unchanged while a published snapshot retains it.
	struct DepthSurface
	{
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		Scene::TextureView view{};
	};

	struct EyeCamera
	{
		Scene::PixelRect depthRect{};
		Scene::Matrix4x4 trackingToNativeColorClip{};
		Scene::Matrix4x4 trackingToReconstructedColorClip{};
		Scene::Matrix4x4 trackingToDepthClip{};
		std::array<double, 4> trackingToDepthViewMetres{};
		double nearPlane = 0.0;
		double farPlane = 0.0;
		double sourceUnitsToMetres = 0.0;
	};

	/// Completed opaque depth and the exact source camera, retained together for both eyes.
	struct Snapshot
	{
		std::uint32_t frame = 0;
		std::uint64_t resourceGeneration = 0;
		std::uintptr_t deviceIdentity = 0;
		std::uint32_t thread = 0;
		std::uint64_t contentSerial = 0;
		std::shared_ptr<const DepthSurface> depth;
		std::array<EyeCamera, 2> eyes{};
		std::array<double, 3> worldOrigin{};
		/// Applies after subtracting worldOrigin, preserving large exterior coordinates in double.
		Scene::Matrix4x4 worldToTracking{};
		Scene::Matrix4x4 headToTracking{};
		bool nativeTemporalResolved = false;
	};

	enum class Status : std::uint8_t
	{
		Inactive,
		AwaitingOpaque,
		Ready,
		UnavailableSource,
		StaleCamera,
		InvalidCamera,
		InvalidDepthView,
		InvalidPublication,
		RetentionBusy,
		ResourceFailure,
	};

	/// Enables producer work only while an explicitly negotiated host requests content.
	void SetRequested(bool a_requested) noexcept;
	/// Stamps the engine's existing per-frame constant-buffer cache at its upload boundary.
	void RecordFramebufferUpdate() noexcept;
	/// Invalidates a previous opaque pass without disturbing retained submit snapshots.
	void InvalidateOpaque() noexcept;
	/// Copies the completed full-eye native depth; requires the render thread inside world rendering.
	void CaptureCompletedOpaqueDepth(ID3D11Texture2D* a_texture, ID3D11ShaderResourceView* a_srv) noexcept;
	/// Returns only the current frame, device, publication generation, and calling render thread.
	[[nodiscard]] std::shared_ptr<const Snapshot> Acquire(std::uint32_t a_frame,
		std::uint64_t a_generation, std::uintptr_t a_device) noexcept;
	[[nodiscard]] Status GetStatus() noexcept;
	/// Drops capture ownership; outstanding submit snapshots keep their pixels alive.
	void Reset() noexcept;
}
