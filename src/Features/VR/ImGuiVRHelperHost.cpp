#include "ImGuiVRHelperHost.h"

#include "Features/Upscaling.h"
#include "Globals.h"
#include "GpuPass.h"
#include "ImGuiVRHelperDisplayCamera.h"
#include "ImGuiVRHelperHostPolicy.h"
#include "ImGuiVRHelperSceneCapture.h"
#include "State.h"
#include "Util.h"
#include <ImGuiVRHelperAPI.h>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <DevBenchAPI.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <magic_enum/magic_enum.hpp>
#include <mutex>
#include <nlohmann/json.hpp>

namespace ImGuiVRHelperHost
{
	namespace API = ImGuiVRHelperPluginAPI;
	namespace Policy = ImGuiVRHelperHostPolicy;
	namespace Scene = ImGuiVRHelperScenePacket;
	namespace Capture = ImGuiVRHelperSceneCapture;
	namespace
	{
		std::atomic<API::IImGuiVRHelperInterface006*> g_interface{ nullptr };
		std::atomic_bool g_requestedEnabled{ true };
		std::atomic_bool g_requestedDepthComparison{ true };
		std::atomic_bool g_active{ false };
		std::atomic_uint32_t g_layers{ 0 };
		std::uint64_t g_token = 0;
		std::mutex g_statusMutex;
		nlohmann::json g_status = { { "connected", false }, { "active", false }, { "reason", "not_negotiated" } };
		std::uint64_t g_composedPairs = 0;
		std::uint64_t g_rejectedPairs = 0;

		struct Surface
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11RenderTargetView> rtv;
			D3D11_TEXTURE2D_DESC description{};
			std::uintptr_t deviceIdentity = 0;
		};
		std::array<Surface, 2> g_surfaces;
		struct Pair
		{
			PairIdentity identity{};
			winrt::com_ptr<ID3D11Texture2D> nativeSource;
			std::array<winrt::com_ptr<ID3D11Texture2D>, 2> sources;
			std::array<vr::VRTextureBounds_t, 2> bounds{};
			std::array<vr::EColorSpace, 2> colorSpaces{};
			std::shared_ptr<const Capture::Snapshot> scene;
			std::optional<Capture::Snapshot> displayCamera;
			std::uint64_t generation = 0;
			bool attempted = false;
			bool prepared = false;
		};
		Pair g_pair;

		void PublishStatus(const char* a_reason, std::uint32_t a_result = 0)
		{
			const std::scoped_lock lock(g_statusMutex);
			const auto captureStatus = Capture::GetStatus();
			g_status = {
				{ "connected", g_interface.load() != nullptr },
				{ "active", g_active.load() },
				{ "layers", g_layers.load() },
				{ "reason", a_reason },
				{ "result", a_result },
				{ "pairToken", g_pair.identity.token },
				{ "frame", g_pair.identity.frame },
				{ "compositorCycle", g_pair.identity.compositorCycle },
				{ "generation", g_pair.generation },
				{ "composedPairs", g_composedPairs },
				{ "rejectedPairs", g_rejectedPairs },
				{ "worldScene", g_pair.scene != nullptr },
				{ "sceneCaptureStatus", static_cast<std::uint32_t>(captureStatus) },
				{ "sceneCaptureStatusName", magic_enum::enum_name(captureStatus) },
			};
		}

		void LogStatus()
		{
			const std::scoped_lock lock(g_statusMutex);
			static nlohmann::json lastState;
			static std::chrono::steady_clock::time_point lastLog{};
			const auto now = std::chrono::steady_clock::now();
			if (!lastState.is_null() && now - lastLog < std::chrono::seconds(5))
				return;
			const nlohmann::json state = {
				g_status.value("reason", std::string()), g_status.value("result", 0u),
				g_active.load(), g_layers.load(), g_status.value("worldScene", false),
				g_status.value("sceneCaptureStatus", 0u)
			};
			if (state == lastState && (g_layers.load() == 0 || now - lastLog < std::chrono::seconds(30)))
				return;
			logger::info("[ImGuiVRHelperHost] {}", g_status.dump());
			lastState = state;
			lastLog = now;
		}

		bool Reject(const char* a_reason, std::uint32_t a_result = 0)
		{
			++g_rejectedPairs;
			g_pair.prepared = false;
			PublishStatus(a_reason, a_result);
			return false;
		}

		bool EnsureSurface(Surface& a_surface, const D3D11_TEXTURE2D_DESC& a_source, std::uint32_t a_eye)
		{
			auto* device = globals::d3d::device;
			const auto deviceIdentity = reinterpret_cast<std::uintptr_t>(device);
			if (!device || a_source.Width == 0 || a_source.Height == 0 ||
				a_source.Width > 16384 || a_source.Height > 16384 || a_source.MipLevels != 1 ||
				a_source.ArraySize != 1 || a_source.SampleDesc.Count != 1 ||
				(a_source.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
					a_source.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS && a_source.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
				return false;
			if (a_surface.texture && a_surface.deviceIdentity == deviceIdentity &&
				a_surface.description.Width == a_source.Width && a_surface.description.Height == a_source.Height)
				return true;
			UINT support = 0;
			if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &support)) ||
				(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0)
				return false;
			D3D11_TEXTURE2D_DESC description = a_source;
			description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			description.CPUAccessFlags = 0;
			description.MiscFlags = 0;
			Surface replacement;
			if (FAILED(device->CreateTexture2D(&description, nullptr, replacement.texture.put())))
				return false;
			Util::SetResourceName(replacement.texture.get(), a_eye == 0 ? "VR::HelperPresentationLeft" : "VR::HelperPresentationRight");
			D3D11_RENDER_TARGET_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			if (FAILED(device->CreateRenderTargetView(replacement.texture.get(), &view, replacement.rtv.put())))
				return false;
			Util::SetResourceName(replacement.rtv.get(), a_eye == 0 ? "VR::HelperPresentationLeft RTV" : "VR::HelperPresentationRight RTV");
			replacement.description = description;
			replacement.deviceIdentity = deviceIdentity;
			a_surface = std::move(replacement);
			return true;
		}

		VRSubmitColorContract::Contract ColorContract(vr::EColorSpace a_colorSpace)
		{
			using ColorSpace = VRSubmitColorContract::SourceColorSpace;
			const auto source = a_colorSpace == vr::ColorSpace_Gamma  ? ColorSpace::Gamma :
			                    a_colorSpace == vr::ColorSpace_Linear ? ColorSpace::Linear :
			                    a_colorSpace == vr::ColorSpace_Auto   ? ColorSpace::Automatic :
			                                                            ColorSpace::Unsupported;
			return VRSubmitColorContract::Resolve(true, source);
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		void ToolHandler(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				const auto request = nlohmann::json::parse(a_args ? a_args : "{}");
				const auto action = request.value("action", std::string("status"));
				if (!request.is_object() || (action != "status" && action != "configure"))
					throw std::invalid_argument("Expected status or configure action");
				if (action == "configure") {
					const bool enabled = request.value("enabled", g_requestedEnabled.load());
					const bool depthComparison = request.value("depthComparison", g_requestedDepthComparison.load());
					g_requestedEnabled.store(enabled);
					g_requestedDepthComparison.store(depthComparison);
				}
				nlohmann::json result;
				{
					const std::scoped_lock lock(g_statusMutex);
					result = g_status;
				}
				result["ok"] = true;
				result["requestedEnabled"] = g_requestedEnabled.load();
				result["requestedDepthComparison"] = g_requestedDepthComparison.load();
				a_write(a_sink, result.dump().c_str());
			} catch (const std::exception& error) {
				try {
					a_write(a_sink, nlohmann::json{ { "ok", false }, { "error", error.what() } }.dump().c_str());
				} catch (...) {
					a_write(a_sink, R"({"ok":false,"error":"serialization_failed"})");
				}
			} catch (...) {
				a_write(a_sink, R"({"ok":false,"error":"request_failed"})");
			}
		}
#endif
	}

	void Initialize()
	{
		if (!globals::game::isVR)
			return;
		g_interface.store(API::GetImGuiVRHelperInterface006());
		PublishStatus(g_interface.load() ? "negotiated_inactive" : "helper_interface_006_unavailable");
		LogStatus();
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (auto* devBench = DevBenchAPI::GetDevBenchInterface001()) {
			static constexpr auto descriptor = R"({"description":"CSX-only ImGui VR Helper hosting status and pair-boundary controls. configure.enabled activates or releases explicit hosting at the next stereo boundary. depthComparison=false is a session-only diagnostic that disables world-quad depth discard; restore true after diagnosis. Missing/older helpers remain on their existing rendering path.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","configure"]},"enabled":{"type":"boolean"},"depthComparison":{"type":"boolean"}},"additionalProperties":false}})";
			devBench->RegisterTool("communityshaders.imgui_vr_helper", descriptor, &ToolHandler, nullptr);
		}
#endif
	}

	bool IsAvailable() noexcept { return g_interface.load() != nullptr; }
	bool HasContent() noexcept { return g_active.load() && g_layers.load() != 0; }
	bool CanComposePair() noexcept { return HasContent() && (g_pair.scene || g_pair.displayCamera) && g_pair.identity.token != 0; }
	bool HasWorldScene() noexcept { return g_pair.scene != nullptr; }
	bool IsPairAttempted() noexcept { return g_pair.attempted; }
	void NoteSubmission() noexcept { g_pair.attempted = true; }
	void SkipPair() noexcept
	{
		g_pair.attempted = true;
		g_pair.prepared = false;
	}

	void Tick()
	{
		auto* helper = g_interface.load();
		if (!helper || !globals::d3d::context)
			return;
		// Sample the preceding pair's outcome, keeping production diagnostics off the per-eye path.
		LogStatus();
		if (g_token == 0) {
			API::RenderHostCapabilities caps;
			const auto capabilities = helper->QueryRenderHostCapabilities(&caps);
			if (capabilities != API::RenderHostResult::Success) {
				PublishStatus("capabilities_failed", static_cast<std::uint32_t>(capabilities));
				return;
			}
			API::RenderHostRegistration registration;
			const auto result = helper->RegisterRenderHost(&registration, &g_token);
			if (result != API::RenderHostResult::Success) {
				PublishStatus("registration_failed", static_cast<std::uint32_t>(result));
				return;
			}
		}
		API::RenderHostContent content;
		const auto result = helper->QueryRenderHostContent(g_token, &content);
		g_layers.store(result == API::RenderHostResult::Success ? content.layers : 0);
		Capture::SetRequested(g_requestedEnabled.load() && g_layers.load() != 0);
		if (result != API::RenderHostResult::Success && result != API::RenderHostResult::NoContent)
			PublishStatus("content_query_failed", static_cast<std::uint32_t>(result));
		else if (g_layers.load() == 0)
			PublishStatus("no_content");
	}

	void BeginPair(const PairIdentity& a_pair, ID3D11Texture2D* a_nativeSource)
	{
		EndPair();
		auto* helper = g_interface.load();
		if (!helper || g_token == 0)
			return;
		const bool enabled = g_requestedEnabled.load();
		auto& upscaling = globals::features::upscaling;
		const auto* state = globals::state;
		if (enabled && g_layers.load() != 0 && a_nativeSource && state) {
			g_pair.identity = a_pair;
			g_pair.nativeSource.copy_from(a_nativeSource);
			g_pair.generation = state->GetCompletedRenderTargetResourcePublicationGeneration();
			if (
				a_pair.frame == state->lastCompletedWorldRenderFrame && a_pair.frame == state->lastWorldRenderFrame &&
				!state->IsSaveLoadSafeModeActive() && !state->IsEngineSaveLoadActivityActive() &&
				!upscaling.IsSubmitStageDeviceLost() && !upscaling.IsVRPostLoadCompositorHoldActive() &&
				!upscaling.IsVRInitialLoadPresentationProtectionActive() && !upscaling.ShouldReuseOrdinarySaveResources() &&
				!upscaling.ShouldSuppressVRInSceneOverlaySubmit() && upscaling.GetVRNativeRestorePresentationGuardActiveEpoch() == 0) {
				g_pair.scene = Capture::Acquire(a_pair.frame, g_pair.generation, reinterpret_cast<std::uintptr_t>(globals::d3d::device));
			}
			if (!g_pair.scene && (g_layers.load() & ~API::RenderHostLayer_World) != 0) {
				g_pair.displayCamera = ImGuiVRHelperDisplayCamera::CaptureDisplayCamera();
				if (g_pair.displayCamera) {
					g_pair.displayCamera->frame = a_pair.frame;
					g_pair.displayCamera->resourceGeneration = g_pair.generation;
					g_pair.displayCamera->deviceIdentity = reinterpret_cast<std::uintptr_t>(globals::d3d::device);
					g_pair.displayCamera->thread = a_pair.thread;
				}
			}
		}
		// Ownership changes before either compositor hook runs; missing scene evidence never restores legacy world draws.
		const bool requested = enabled;
		if (requested != g_active.load()) {
			const auto result = helper->SetRenderHostActive(g_token, requested);
			if (result != API::RenderHostResult::Success) {
				PublishStatus("activation_failed", static_cast<std::uint32_t>(result));
				return;
			}
			g_active.store(requested);
			if (!enabled) {
				Capture::SetRequested(false);
				Capture::Reset();
				g_surfaces = {};
			}
			PublishStatus(requested ? "hosted" : "disabled");
		}
		if (requested && g_layers.load() != 0)
			PublishStatus(g_pair.scene || g_pair.displayCamera ? "awaiting_composition" : "awaiting_current_scene_or_display_camera");
	}

	void EndPair() noexcept { g_pair = {}; }

	bool ComposePair(const std::array<vr::Texture_t, 2>& a_sources,
		const std::array<vr::VRTextureBounds_t, 2>& a_bounds, bool a_reconstructed)
	{
		if (!HasContent() || g_pair.attempted || g_pair.identity.token == 0)
			return false;
		g_pair.attempted = true;
		if (!g_pair.scene && !g_pair.displayCamera)
			return Reject("no_current_scene_or_display_camera");
		if (!globals::state || g_pair.generation == 0 ||
			g_pair.generation != globals::state->GetCompletedRenderTargetResourcePublicationGeneration())
			return Reject("stale_resource_generation");
		CS_GPU_PASS("VR::ImGuiVRHelperCompose");
		auto* helper = g_interface.load();
		API::HostedFrameHandle frameHandle;
		bool frameOpen = false;
		const SKSE::stl::scope_exit closeFrame([&]() noexcept {
			if (frameOpen)
				helper->AbortHostedFrame(g_token, frameHandle.cookie);
		});
		try {
			const bool worldScene = g_pair.scene != nullptr;
			const auto& captured = worldScene ? *g_pair.scene : *g_pair.displayCamera;
			Policy::PairInput input;
			input.isVR = true;
			input.negotiatedCSXHostToken = g_token;
			input.activeCSXHostToken = g_token;
			input.hasWorldContent = true;
			input.currentPair = g_pair.identity;
			input.completedResourceGeneration = g_pair.generation;
			input.currentDeviceIdentity = captured.deviceIdentity;
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				const auto& source = a_sources[eye];
				if (source.eType != vr::TextureType_DirectX || !source.handle)
					return Reject("invalid_color_source");
				g_pair.sources[eye].copy_from(static_cast<ID3D11Texture2D*>(source.handle));
				if (!a_reconstructed && g_pair.sources[eye] != g_pair.nativeSource)
					return Reject("native_source_mismatch");
				D3D11_TEXTURE2D_DESC description{};
				g_pair.sources[eye]->GetDesc(&description);
				winrt::com_ptr<ID3D11Device> device;
				g_pair.sources[eye]->GetDevice(device.put());
				if (device.get() != globals::d3d::device || !EnsureSurface(g_surfaces[eye], description, eye))
					return Reject("unsupported_color_target");
				auto& metadata = input.eyes[eye];
				const auto& camera = captured.eyes[eye];
				metadata.candidate = a_reconstructed ? Policy::Candidate::CurrentReconstructed : Policy::Candidate::CurrentNative;
				metadata.scenePair = input.currentPair;
				metadata.depthPair = input.currentPair;
				metadata.submitLease = { g_pair.generation, captured.deviceIdentity, true, false, false };
				metadata.colorView = { reinterpret_cast<std::uintptr_t>(g_surfaces[eye].texture.get()), captured.deviceIdentity,
					{ description.Width, description.Height } };
				metadata.colorBounds = { a_bounds[eye].uMin, a_bounds[eye].vMin, a_bounds[eye].uMax, a_bounds[eye].vMax };
				metadata.colorContract = ColorContract(source.eColorSpace);
				metadata.renderTargetCapabilityProven = true;
				metadata.depthView = worldScene ? captured.depth->view : Scene::TextureView{};
				metadata.occlusionDepthRetention = { metadata.depthView.resourceIdentity, captured.deviceIdentity, g_pair.generation, true };
				metadata.depthRect = camera.depthRect;
				metadata.completedOpaqueDepth = true;
				metadata.fullEyeDepthCoverage = true;
				metadata.trackingToColorClip = (a_reconstructed || captured.nativeTemporalResolved) ? camera.trackingToReconstructedColorClip : camera.trackingToNativeColorClip;
				metadata.trackingToDepthClip = camera.trackingToDepthClip;
				metadata.trackingToDepthViewMetres = camera.trackingToDepthViewMetres;
				metadata.nearPlane = camera.nearPlane;
				metadata.farPlane = camera.farPlane;
				metadata.sourceUnitsToMetres = camera.sourceUnitsToMetres;
				g_pair.bounds[eye] = a_bounds[eye];
				g_pair.colorSpaces[eye] = source.eColorSpace;
			}
			std::optional<Policy::StereoPacket> stereo;
			if (worldScene) {
				const auto admitted = Policy::BuildPair(input);
				if (!admitted.packet)
					return Reject("metadata_rejected", static_cast<std::uint32_t>(admitted.rejection));
				stereo = admitted.packet;
			} else {
				stereo.emplace();
				for (std::uint32_t eye = 0; eye < 2; ++eye) {
					const auto& source = input.eyes[eye];
					const auto viewport = Scene::MakeOutputViewport(source.colorView.extent, source.colorBounds);
					if (!viewport || !Scene::IsValid(source.colorView) ||
						!VRSubmitColorContract::IsPresentationSupported(source.colorContract) ||
						!Scene::IsInvertible(source.trackingToColorClip))
						return Reject("invalid_display_camera_target");
					auto& packet = stereo->eyes[eye];
					packet.colorView = source.colorView;
					packet.colorContract = source.colorContract;
					packet.viewport = *viewport;
					packet.trackingToColorClip = source.trackingToColorClip;
				}
			}
			API::HostedFrameInfo frame;
			frame.pairToken = g_pair.identity.token;
			frame.sceneFrame = g_pair.identity.frame;
			frame.resourceGeneration = g_pair.generation;
			frame.compositorCycle = g_pair.identity.compositorCycle;
			frame.context = globals::d3d::context;
			frame.worldLayerEnabled = worldScene ? 1u : 0u;
			frame.diagnostics = g_requestedDepthComparison.load() ? 0u : API::RenderHostDiagnostic_DisableWorldDepthTest;
			std::copy(captured.worldOrigin.begin(), captured.worldOrigin.end(), frame.worldOrigin);
			std::copy(captured.worldToTracking.begin(), captured.worldToTracking.end(), frame.worldToTracking);
			std::copy(captured.headToTracking.begin(), captured.headToTracking.end(), frame.headToTracking);
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				const auto& packet = stereo->eyes[eye];
				auto& target = frame.eyes[eye];
				target.outputWidth = packet.colorView.extent.width;
				target.outputHeight = packet.colorView.extent.height;
				target.outputColorSpace = packet.colorContract.transfer == VRSubmitColorContract::Transfer::Linear ?
				                              API::RenderHostColorSpace::Linear :
				                              API::RenderHostColorSpace::Gamma;
				target.viewport = { packet.viewport.x, packet.viewport.y, packet.viewport.width, packet.viewport.height };
				target.orientation = (packet.viewport.flipX ? API::RenderHostOrientation_FlipX : 0u) |
				                     (packet.viewport.flipY ? API::RenderHostOrientation_FlipY : 0u);
				target.depthSRV = worldScene ? captured.depth->srv.get() : nullptr;
				target.depthWidth = packet.depth.texture.extent.width;
				target.depthHeight = packet.depth.texture.extent.height;
				target.depthRect = { packet.depth.activeRect.x, packet.depth.activeRect.y, packet.depth.activeRect.width, packet.depth.activeRect.height };
				target.depthScale = packet.depthEncoding.scale;
				target.depthOffset = packet.depthEncoding.offset;
				std::copy(packet.trackingToColorClip.begin(), packet.trackingToColorClip.end(), target.trackingToColorClip);
				std::copy(packet.trackingToDepthClip.begin(), packet.trackingToDepthClip.end(), target.trackingToDepthClip);
				std::copy(packet.trackingToDepthViewMetres.begin(), packet.trackingToDepthViewMetres.end(), target.trackingToDepthMetres);
			}
			auto result = helper->BeginHostedFrame(g_token, &frame, &frameHandle);
			if (result != API::RenderHostResult::Success)
				return Reject("begin_failed", static_cast<std::uint32_t>(result));
			frameOpen = true;
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				globals::d3d::context->CopyResource(g_surfaces[eye].texture.get(), g_pair.sources[eye].get());
				API::HostedEyeContext target;
				target.eye = eye;
				target.pairToken = g_pair.identity.token;
				target.resourceGeneration = g_pair.generation;
				target.attemptId = eye + 1;
				target.target = g_surfaces[eye].rtv.get();
				API::HostedEyeResult eyeResult;
				result = helper->RenderHostedEye(g_token, frameHandle.cookie, &target, &eyeResult);
				if (result != API::RenderHostResult::Success)
					return Reject("composition_failed", static_cast<std::uint32_t>(result));
			}
			result = helper->EndHostedFrame(g_token, frameHandle.cookie);
			frameOpen = false;
			if (result != API::RenderHostResult::Success)
				return Reject("incomplete_pair", static_cast<std::uint32_t>(result));
			g_pair.prepared = true;
			++g_composedPairs;
			PublishStatus(worldScene ? "composed" : "composed_ui_only");
			return true;
		} catch (const std::exception& error) {
			logger::error("VR helper composition failed: {}", error.what());
			return Reject("composition_exception");
		} catch (...) {
			return Reject("composition_exception");
		}
	}

	ID3D11Texture2D* GetPreparedEye(std::uint32_t a_eye, const vr::Texture_t& a_source,
		const vr::VRTextureBounds_t* a_bounds) noexcept
	{
		if (a_eye >= 2 || !g_pair.prepared || !a_bounds || !globals::state ||
			a_source.eType != vr::TextureType_DirectX || a_source.handle != g_pair.sources[a_eye].get() ||
			a_source.eColorSpace != g_pair.colorSpaces[a_eye] ||
			g_pair.generation != globals::state->GetCompletedRenderTargetResourcePublicationGeneration())
			return nullptr;
		const auto& bounds = g_pair.bounds[a_eye];
		if (a_bounds->uMin != bounds.uMin || a_bounds->uMax != bounds.uMax ||
			a_bounds->vMin != bounds.vMin || a_bounds->vMax != bounds.vMax)
			return nullptr;
		return g_surfaces[a_eye].texture.get();
	}
}
