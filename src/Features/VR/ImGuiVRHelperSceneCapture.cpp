#include "ImGuiVRHelperSceneCapture.h"

#include "Features/Upscaling.h"
#include "Globals.h"
#include "GpuPass.h"
#include "State.h"
#include "Utils/Game.h"
#include "Utils/ResourceName.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>

#ifdef _MSC_VER
#	pragma float_control(precise, on, push)
#endif

namespace ImGuiVRHelperSceneCapture
{
	namespace
	{
		constexpr double kGameUnitToMetres = 0.01428;
		std::atomic<bool> requested = false;
		std::atomic<Status> status = Status::Inactive;
		std::mutex captureMutex;
		std::shared_ptr<const Snapshot> published;
		std::array<std::shared_ptr<DepthSurface>, 3> surfaces;
		std::uint64_t serial = 0;
		thread_local std::uint32_t framebufferFrame = std::numeric_limits<std::uint32_t>::max();
		struct CameraSource
		{
			globals::FrameBufferVR buffer{};
			RE::NiTransform room{};
			std::array<double, 2> activeSize{};
			double nearPlane = 0.0;
			double farPlane = 0.0;
			bool temporalResolved = false;
			bool valid = false;
		};
		thread_local CameraSource sourceCamera;

		Scene::Matrix4x4 Identity() noexcept
		{
			return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		}

		Scene::Matrix4x4 Multiply(const Scene::Matrix4x4& a_left, const Scene::Matrix4x4& a_right) noexcept
		{
			Scene::Matrix4x4 result{};
			for (std::size_t row = 0; row < 4; ++row)
				for (std::size_t column = 0; column < 4; ++column)
					for (std::size_t inner = 0; inner < 4; ++inner)
						result[row * 4 + column] += a_left[row * 4 + inner] * a_right[inner * 4 + column];
			return result;
		}

		Scene::Matrix4x4 ShaderMatrix(const Matrix& a_matrix) noexcept
		{
			Scene::Matrix4x4 result{};
			// FrameBuffer.hlsli multiplies matrix by column vector; the hosted ABI uses row vectors.
			for (std::size_t row = 0; row < 4; ++row)
				for (std::size_t column = 0; column < 4; ++column)
					result[row * 4 + column] = a_matrix.m[column][row];
			return result;
		}

		bool IsRotation(const Scene::Matrix4x4& a_matrix) noexcept
		{
			if (!Scene::IsValid(a_matrix))
				return false;
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t other = row; other < 3; ++other) {
					double dot = 0.0;
					for (std::size_t column = 0; column < 3; ++column)
						dot += a_matrix[row * 4 + column] * a_matrix[other * 4 + column];
					if (std::abs(dot - (row == other ? 1.0 : 0.0)) > 0.001)
						return false;
				}
			}
			return true;
		}

		bool IsNativeProjection(const Scene::Matrix4x4& a_projection, double a_near, double a_far) noexcept
		{
			if (!Scene::IsInvertible(a_projection) || !Scene::MakeNativeForwardZ(a_near, a_far, 1.0))
				return false;
			const double offset = a_far / (a_far - a_near);
			const double scale = a_near * offset;
			return std::abs(a_projection[10] - offset) < 0.0001 &&
			       std::abs(a_projection[14] + scale) < 0.0001 * scale &&
			       std::abs(a_projection[11] - 1.0) < 0.0001 && std::abs(a_projection[15]) < 0.0001 &&
			       std::abs(a_projection[2]) < 0.0001 && std::abs(a_projection[6]) < 0.0001 &&
			       std::abs(a_projection[3]) < 0.0001 && std::abs(a_projection[7]) < 0.0001;
		}

		bool MatchesMatrix(const Scene::Matrix4x4& a_left, const Scene::Matrix4x4& a_right) noexcept
		{
			if (!Scene::IsValid(a_left) || !Scene::IsValid(a_right))
				return false;
			for (std::size_t index = 0; index < a_left.size(); ++index) {
				const double magnitude = std::max({ 1.0, std::abs(a_left[index]), std::abs(a_right[index]) });
				if (std::abs(a_left[index] - a_right[index]) > magnitude * 0.001)
					return false;
			}
			return true;
		}

		bool CaptureCamera(Snapshot& a_snapshot, Scene::PixelExtent a_extent) noexcept
		{
			if (!sourceCamera.valid)
				return false;
			const auto& roomTransform = sourceCamera.room;
			const std::array<double, 4> roomValues{
				roomTransform.translate.x, roomTransform.translate.y, roomTransform.translate.z, roomTransform.scale
			};
			if (!Scene::IsValid(roomValues) || roomValues[3] <= 0.0)
				return false;
			const double unitsToMetres = kGameUnitToMetres / roomValues[3];
			const double nearPlane = sourceCamera.nearPlane;
			const double farPlane = sourceCamera.farPlane;
			if (!Scene::MakeNativeForwardZ(nearPlane, farPlane, unitsToMetres))
				return false;
			const auto& active = sourceCamera.activeSize;
			if (!Scene::IsValid(active) || active[0] < 2.0 || active[1] < 1.0 ||
				active[0] > a_extent.width || active[1] > a_extent.height)
				return false;
			const auto eyeWidth = static_cast<std::uint32_t>(active[0] * 0.5);
			const auto eyeHeight = static_cast<std::uint32_t>(active[1]);

			const auto& rotation = roomTransform.rotate.entry;
			auto roomToTracking = Identity();
			for (std::size_t row = 0; row < 3; ++row) {
				roomToTracking[row * 4] = rotation[row][0];
				roomToTracking[row * 4 + 1] = rotation[row][2];
				roomToTracking[row * 4 + 2] = -rotation[row][1];
			}
			if (!IsRotation(roomToTracking))
				return false;
			const auto roomRotation = roomToTracking;
			auto trackingToRoom = Identity();
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 3; ++column) {
					trackingToRoom[row * 4 + column] = roomRotation[column * 4 + row] / unitsToMetres;
					roomToTracking[row * 4 + column] *= unitsToMetres;
				}
			}
			a_snapshot.worldToTracking = roomToTracking;
			a_snapshot.worldOrigin = { roomValues[0], roomValues[1], roomValues[2] };
			a_snapshot.nativeTemporalResolved = sourceCamera.temporalResolved;

			const auto& camera = sourceCamera.buffer;
			std::array<Scene::Matrix4x4, 2> eyeToTracking;
			for (std::size_t eye = 0; eye < a_snapshot.eyes.size(); ++eye) {
				auto& output = a_snapshot.eyes[eye];
				const auto projection = ShaderMatrix(camera.CameraProj[eye]);
				const auto view = ShaderMatrix(camera.CameraView[eye]);
				const auto inverseView = ShaderMatrix(camera.CameraViewInverse[eye]);
				const auto viewProjection = ShaderMatrix(camera.CameraViewProj[eye]);
				if (!IsNativeProjection(projection, nearPlane, farPlane) || !IsRotation(view) || !IsRotation(inverseView) ||
					!MatchesMatrix(Multiply(view, inverseView), Identity()) || !MatchesMatrix(Multiply(view, projection), viewProjection))
					return false;
				const auto& adjust = camera.CameraPosAdjust[eye];
				const std::array<double, 3> origin{ adjust.x, adjust.y, adjust.z };
				if (!Scene::IsValid(origin))
					return false;
				auto trackingToRelativeWorld = trackingToRoom;
				auto relativeWorldToTracking = roomToTracking;
				for (std::size_t column = 0; column < 3; ++column) {
					// Subtract camera and room origins in double before projecting large exterior coordinates.
					trackingToRelativeWorld[12 + column] = roomValues[column] - origin[column];
					for (std::size_t row = 0; row < 3; ++row)
						relativeWorldToTracking[12 + column] += (origin[row] - roomValues[row]) * roomToTracking[row * 4 + column];
				}
				output.depthRect = { static_cast<std::uint32_t>(eye) * eyeWidth, 0, eyeWidth, eyeHeight };
				output.trackingToDepthClip = Multiply(trackingToRelativeWorld, viewProjection);
				output.trackingToNativeColorClip = output.trackingToDepthClip;
				output.trackingToReconstructedColorClip = Multiply(trackingToRelativeWorld, ShaderMatrix(camera.CameraViewProjUnjittered[eye]));
				const auto trackingToView = Multiply(trackingToRelativeWorld, view);
				for (std::size_t row = 0; row < 4; ++row)
					output.trackingToDepthViewMetres[row] = trackingToView[row * 4 + 2] * unitsToMetres;
				output.nearPlane = nearPlane;
				output.farPlane = farPlane;
				output.sourceUnitsToMetres = unitsToMetres;
				if (!Scene::Contains(output.depthRect, a_extent) || !Scene::IsInvertible(output.trackingToDepthClip) ||
					!Scene::IsInvertible(output.trackingToReconstructedColorClip) || !Scene::HasAxialDepthDirection(output.trackingToDepthViewMetres))
					return false;
				eyeToTracking[eye] = Multiply(inverseView, relativeWorldToTracking);
			}

			// Engine camera motion, including VRIK, is already present in the captured view matrices.
			a_snapshot.headToTracking = Identity();
			for (std::size_t row = 0; row < 3; ++row) {
				double length = 0.0;
				for (std::size_t column = 0; column < 3; ++column) {
					const double value = (eyeToTracking[0][row * 4 + column] + eyeToTracking[1][row * 4 + column]) * 0.5;
					a_snapshot.headToTracking[row * 4 + column] = value;
					length += value * value;
				}
				if (!std::isfinite(length) || length <= 0.0)
					return false;
				const double scale = (row == 2 ? -1.0 : 1.0) / std::sqrt(length);
				for (std::size_t column = 0; column < 3; ++column)
					a_snapshot.headToTracking[row * 4 + column] *= scale;
			}
			for (std::size_t column = 0; column < 3; ++column)
				a_snapshot.headToTracking[12 + column] = (eyeToTracking[0][12 + column] + eyeToTracking[1][12 + column]) * 0.5;
			return IsRotation(a_snapshot.headToTracking) && Scene::IsInvertible(a_snapshot.worldToTracking);
		}

		bool ReadDepthView(ID3D11Texture2D* a_texture, ID3D11ShaderResourceView* a_srv,
			D3D11_TEXTURE2D_DESC& a_desc, D3D11_SHADER_RESOURCE_VIEW_DESC& a_view) noexcept
		{
			a_texture->GetDesc(&a_desc);
			a_srv->GetDesc(&a_view);
			winrt::com_ptr<ID3D11Resource> source;
			a_srv->GetResource(source.put());
			winrt::com_ptr<ID3D11Device> device;
			a_texture->GetDevice(device.put());
			if (source.get() != a_texture || device.get() != globals::d3d::device ||
				a_desc.MipLevels != 1 || a_desc.ArraySize != 1 || a_desc.SampleDesc.Count != 1 ||
				a_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || a_view.Texture2D.MostDetailedMip != 0 ||
				(a_view.Texture2D.MipLevels != 1 && a_view.Texture2D.MipLevels != UINT(-1)))
				return false;
			if (a_view.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && a_view.Format != DXGI_FORMAT_R32_FLOAT &&
				a_view.Format != DXGI_FORMAT_R16_UNORM && a_view.Format != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
				return false;
			return Scene::IsValid(Scene::TextureView{
				reinterpret_cast<std::uintptr_t>(a_texture), reinterpret_cast<std::uintptr_t>(device.get()),
				{ a_desc.Width, a_desc.Height } });
		}

		std::shared_ptr<DepthSurface> RetainSurface(const D3D11_TEXTURE2D_DESC& a_source,
			const D3D11_SHADER_RESOURCE_VIEW_DESC& a_view)
		{
			for (auto& surface : surfaces) {
				if (surface && surface.use_count() != 1)
					continue;
				if (surface) {
					D3D11_TEXTURE2D_DESC previous{};
					D3D11_SHADER_RESOURCE_VIEW_DESC previousView{};
					surface->texture->GetDesc(&previous);
					surface->srv->GetDesc(&previousView);
					if (previous.Width == a_source.Width && previous.Height == a_source.Height && previous.Format == a_source.Format &&
						previousView.Format == a_view.Format && surface->view.deviceIdentity == reinterpret_cast<std::uintptr_t>(globals::d3d::device))
						return surface;
				}
				auto replacement = std::make_shared<DepthSurface>();
				auto desc = a_source;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = 0;
				if (FAILED(globals::d3d::device->CreateTexture2D(&desc, nullptr, replacement->texture.put()))) {
					status.store(Status::ResourceFailure, std::memory_order_relaxed);
					return {};
				}
				Util::SetResourceName(replacement->texture.get(), "VR::ImGuiVRHelperSceneDepth");
				auto retainedView = a_view;
				retainedView.Texture2D.MipLevels = 1;
				if (FAILED(globals::d3d::device->CreateShaderResourceView(replacement->texture.get(), &retainedView, replacement->srv.put()))) {
					status.store(Status::ResourceFailure, std::memory_order_relaxed);
					return {};
				}
				Util::SetResourceName(replacement->srv.get(), "VR::ImGuiVRHelperSceneDepth.SRV");
				replacement->view = { reinterpret_cast<std::uintptr_t>(replacement->texture.get()),
					reinterpret_cast<std::uintptr_t>(globals::d3d::device), { desc.Width, desc.Height } };
				surface = replacement;
				return replacement;
			}
			status.store(Status::RetentionBusy, std::memory_order_relaxed);
			return {};
		}
	}

	void SetRequested(bool a_requested) noexcept
	{
		const bool previous = requested.exchange(a_requested, std::memory_order_acq_rel);
		if (previous != a_requested) {
			Reset();
			status.store(a_requested ? Status::AwaitingOpaque : Status::Inactive, std::memory_order_relaxed);
		}
	}

	void RecordFramebufferUpdate() noexcept
	{
		if (requested.load(std::memory_order_relaxed) && globals::game::isVR && globals::state) {
			framebufferFrame = globals::state->frameCount;
			sourceCamera.valid = false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* nodes = player ? player->GetVRNodeData() : nullptr;
			auto* room = nodes ? nodes->RoomNode.get() : nullptr;
			if (!room || !globals::game::cameraNear || !globals::game::cameraFar || !globals::game::graphicsState)
				return;
			sourceCamera.buffer = globals::game::frameBufferCached.vr;
			sourceCamera.room = room->world;
			const auto active = Util::ConvertToDynamic(globals::state->screenSize);
			sourceCamera.activeSize = { active.x, active.y };
			sourceCamera.nearPlane = *globals::game::cameraNear;
			sourceCamera.farPlane = *globals::game::cameraFar;
			sourceCamera.temporalResolved = Util::GetTemporal() ||
			                                globals::features::upscaling.GetRuntimeUpscaleMethod() == Upscaling::UpscaleMethod::kTAA;
			sourceCamera.valid = true;
		}
	}

	void InvalidateOpaque() noexcept
	{
		if (!requested.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(captureMutex);
		published.reset();
		status.store(Status::AwaitingOpaque, std::memory_order_relaxed);
	}

	void CaptureCompletedOpaqueDepth(ID3D11Texture2D* a_texture, ID3D11ShaderResourceView* a_srv) noexcept
	{
		if (!requested.load(std::memory_order_acquire))
			return;
		try {
			std::lock_guard lock(captureMutex);
			if (!requested.load(std::memory_order_acquire))
				return;
			published.reset();
			status.store(Status::UnavailableSource, std::memory_order_relaxed);
			if (!globals::game::isVR || !globals::state || !globals::state->inWorld || !a_texture || !a_srv ||
				!globals::d3d::device || !globals::d3d::context || FAILED(globals::d3d::device->GetDeviceRemovedReason()))
				return;
			const auto frame = globals::state->frameCount;
			if (!frame || frame == std::numeric_limits<std::uint32_t>::max() || framebufferFrame != frame) {
				status.store(Status::StaleCamera, std::memory_order_relaxed);
				return;
			}
			const auto generation = globals::state->GetCompletedRenderTargetResourcePublicationGeneration();
			if (!generation) {
				status.store(Status::InvalidPublication, std::memory_order_relaxed);
				return;
			}
			D3D11_TEXTURE2D_DESC desc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			if (!ReadDepthView(a_texture, a_srv, desc, view)) {
				status.store(Status::InvalidDepthView, std::memory_order_relaxed);
				return;
			}
			auto snapshot = std::make_shared<Snapshot>();
			if (!CaptureCamera(*snapshot, { desc.Width, desc.Height })) {
				status.store(Status::InvalidCamera, std::memory_order_relaxed);
				return;
			}
			auto surface = RetainSurface(desc, view);
			if (!surface)
				return;
			CS_GPU_PASS("VR::ImGuiVRHelperSceneDepth");
			globals::d3d::context->CopyResource(surface->texture.get(), a_texture);
			snapshot->frame = frame;
			snapshot->resourceGeneration = generation;
			snapshot->deviceIdentity = reinterpret_cast<std::uintptr_t>(globals::d3d::device);
			snapshot->thread = GetCurrentThreadId();
			snapshot->contentSerial = ++serial;
			snapshot->depth = std::move(surface);
			published = std::move(snapshot);
			status.store(Status::Ready, std::memory_order_relaxed);
		} catch (...) {
			status.store(Status::ResourceFailure, std::memory_order_relaxed);
		}
	}

	std::shared_ptr<const Snapshot> Acquire(std::uint32_t a_frame, std::uint64_t a_generation, std::uintptr_t a_device) noexcept
	{
		if (!requested.load(std::memory_order_acquire))
			return {};
		std::lock_guard lock(captureMutex);
		if (!published || published->frame != a_frame || published->resourceGeneration != a_generation ||
			published->deviceIdentity != a_device || published->thread != GetCurrentThreadId())
			return {};
		return published;
	}

	Status GetStatus() noexcept
	{
		return status.load(std::memory_order_relaxed);
	}

	void Reset() noexcept
	{
		std::lock_guard lock(captureMutex);
		published.reset();
		for (auto& surface : surfaces)
			surface.reset();
		status.store(requested.load(std::memory_order_relaxed) ? Status::AwaitingOpaque : Status::Inactive, std::memory_order_relaxed);
	}
}

#ifdef _MSC_VER
#	pragma float_control(pop)
#endif
