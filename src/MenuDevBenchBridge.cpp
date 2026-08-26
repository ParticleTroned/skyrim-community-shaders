#include "MenuDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "BuildProvenance.h"
#	include "Features/ScreenshotFeature.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Features/VRDepthCullingTemporal.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <atomic>
#	include <chrono>
#	include <cstdint>
#	include <functional>
#	include <future>
#	include <limits>
#	include <memory>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json InspectMenuTexture()
	{
		auto& vr = globals::features::vr;
		auto* source = vr.menuTexture.get();
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!source || !device || !context)
			return { { "available", false }, { "error", "menu texture or D3D11 device/context unavailable" } };

		D3D11_TEXTURE2D_DESC sourceDesc{};
		source->GetDesc(&sourceDesc);
		json output = {
			{ "available", true },
			{ "width", sourceDesc.Width },
			{ "height", sourceDesc.Height },
			{ "format", static_cast<std::uint32_t>(sourceDesc.Format) },
			{ "mipLevels", sourceDesc.MipLevels },
		};
		if (sourceDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && sourceDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
			output["error"] = "unsupported menu texture format for RGBA inspection";
			return output;
		}

		D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
		stagingDesc.Width = sourceDesc.Width;
		stagingDesc.Height = sourceDesc.Height;
		stagingDesc.MipLevels = 1;
		stagingDesc.ArraySize = 1;
		stagingDesc.SampleDesc = { 1, 0 };
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;

		ID3D11Texture2D* staging = nullptr;
		HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
		if (FAILED(hr) || !staging) {
			output["error"] = "CreateTexture2D staging failed";
			output["hresult"] = static_cast<std::uint32_t>(hr);
			return output;
		}

		context->CopySubresourceRegion(staging, 0, 0, 0, 0, source, 0, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr)) {
			staging->Release();
			output["error"] = "Map staging texture failed";
			output["hresult"] = static_cast<std::uint32_t>(hr);
			return output;
		}

		std::uint64_t alphaNonZero = 0;
		std::uint64_t alphaOpaque = 0;
		std::uint64_t rgbNonZero = 0;
		std::uint64_t alphaSum = 0;
		std::uint32_t minX = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t minY = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t maxX = 0;
		std::uint32_t maxY = 0;
		for (std::uint32_t y = 0; y < sourceDesc.Height; ++y) {
			const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
			for (std::uint32_t x = 0; x < sourceDesc.Width; ++x) {
				const auto* pixel = row + static_cast<std::size_t>(x) * 4;
				const auto alpha = pixel[3];
				alphaSum += alpha;
				rgbNonZero += (pixel[0] | pixel[1] | pixel[2]) != 0;
				if (alpha != 0) {
					++alphaNonZero;
					alphaOpaque += alpha == 255;
					minX = (std::min)(minX, x);
					minY = (std::min)(minY, y);
					maxX = (std::max)(maxX, x);
					maxY = (std::max)(maxY, y);
				}
			}
		}
		context->Unmap(staging, 0);
		staging->Release();

		const auto pixelCount = static_cast<std::uint64_t>(sourceDesc.Width) * sourceDesc.Height;
		output["rowPitch"] = mapped.RowPitch;
		output["pixelCount"] = pixelCount;
		output["alphaNonZero"] = alphaNonZero;
		output["alphaOpaque"] = alphaOpaque;
		output["rgbNonZero"] = rgbNonZero;
		output["meanAlpha"] = pixelCount ? static_cast<double>(alphaSum) / (255.0 * static_cast<double>(pixelCount)) : 0.0;
		if (alphaNonZero != 0)
			output["alphaBounds"] = { { "minX", minX }, { "minY", minY }, { "maxX", maxX }, { "maxY", maxY } };
		return output;
	}

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		tasks->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
		});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json BuildStatus()
	{
		auto* menu = globals::menu;
		auto& vr = globals::features::vr;
		auto& screenshot = globals::features::screenshotFeature;
		const auto inSceneSubmitSuppressionReasons =
			globals::features::upscaling.GetVRInSceneOverlaySubmitSuppressionReasons();
		const auto depthCullingTemporal = VRDepthCullingTemporal::GetStatus();
		auto* drawData = ImGui::GetCurrentContext() ? ImGui::GetDrawData() : nullptr;
		const auto fixedWorldPosition = vr.fixedWorldOverlayPosition.m.Translation();
		const json depthCullingTemporalStatus = {
			{ "installed", depthCullingTemporal.installed },
			{ "policy", depthCullingTemporal.performanceMode ? "performance" : "balanced" },
			{ "envelopeMisses", depthCullingTemporal.envelopeMisses },
			{ "totalPromoted", depthCullingTemporal.totalPromoted },
			{ "lastObjectCount", depthCullingTemporal.lastObjectCount },
			{ "lastEligibleCount", depthCullingTemporal.lastEligibleCount },
			{ "lastPromotedCount", depthCullingTemporal.lastPromotedCount },
		};
		return {
			{ "menuEnabled", menu && menu->IsEnabled },
			{ "menuSessionOpen", menu && menu->IsMenuSessionOpen() },
			{ "performanceOverlayVisible", menu && menu->overlayVisible },
			{ "mainMenuOpen", globals::state && globals::state->isMainMenuOpen },
			{ "loadingMenuOpen", globals::state && globals::state->isLoadingMenuOpen },
			{ "openVRCompatible", vr.IsOpenVRCompatible() },
			{ "runtimeType", static_cast<int>(vr.openVRInfo.runtimeType) },
			{ "hasOverlayInterface", vr.openVRInfo.hasOverlayInterface },
			{ "shouldUseInSceneOverlay", vr.ShouldUseInSceneOverlay() },
			{ "inSceneSubmitSuppressed",
				inSceneSubmitSuppressionReasons !=
					VRInSceneOverlaySubmitPolicy::SuppressionReason::None },
			{ "inSceneSubmitSuppressionReasons", static_cast<std::uint32_t>(inSceneSubmitSuppressionReasons) },
			{ "shouldPresentOverlayInHeadset", vr.ShouldPresentOverlayInHeadset() },
			{ "attachMode", static_cast<int>(vr.settings.attachMode) },
			{ "menuOverlayPath", static_cast<int>(vr.settings.menuOverlayPath) },
			{ "menuPositioningMethod", vr.settings.VRMenuPositioningMethod },
			{ "effectiveFixedWorldPositioning", vr.UseFixedWorldMenuPositioning() },
			{ "fixedWorldPositionInitialized", vr.fixedWorldOverlayPosition.initialized },
			{ "fixedWorldReanchorRequested", vr.fixedWorldOverlayReanchorRequested },
			{ "fixedWorldPosition", {
				{ "x", fixedWorldPosition.x },
				{ "y", fixedWorldPosition.y },
				{ "z", fixedWorldPosition.z },
			} },
			{ "menuScale", vr.settings.VRMenuScale },
			{ "depthCullingExteriorEnabled", vr.settings.EnableDepthBufferCullingExterior },
			{ "depthCullingInteriorEnabled", vr.settings.EnableDepthBufferCullingInterior },
			{ "depthCullingPerformanceMode", vr.settings.DepthCullingPerformanceMode },
			{ "depthCullingTemporal", depthCullingTemporalStatus },
			{ "menuOffsetX", vr.settings.VRMenuOffsetX },
			{ "menuOffsetY", vr.settings.VRMenuOffsetY },
			{ "menuOffsetZ", vr.settings.VRMenuOffsetZ },
			{ "menuTexture", vr.menuTexture != nullptr },
			{ "menuRenderTarget", vr.menuRTV != nullptr },
			{ "hmdOverlayHandle", vr.menuOverlayHandle != vr::k_ulOverlayHandleInvalid },
			{ "controllerOverlayHandle", vr.menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid },
			{ "submitHookInstalled", vr.inSceneResources.submitHookInstalled },
			{ "inSceneResourcesInitialized", vr.inSceneResources.initialized },
			{ "drawDataValid", drawData && drawData->Valid },
			{ "drawCommandLists", drawData ? drawData->CmdListsCount : 0 },
			{ "drawTotalVertices", drawData ? drawData->TotalVtxCount : 0 },
			{ "drawTotalIndices", drawData ? drawData->TotalIdxCount : 0 },
			{ "screenshotEnabled", screenshot.IsRuntimeEnabled() },
			{ "screenshotPending", screenshot.HasPendingCapture() },
		};
	}

	json BuildResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action != "status" && action != "open" && action != "close" && action != "screenshot" && action != "set_path" && action != "texture_stats" && action != "set_depth_culling_performance_mode") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "open", "close", "screenshot", "set_path", "texture_stats", "set_depth_culling_performance_mode" }) },
			};
		}
		const std::string path = a_args.value("path", std::string());
		if (action == "set_path" && path != "auto" && path != "overlay" && path != "in_scene") {
			return {
				{ "error", "set_path requires path auto, overlay, or in_scene" },
				{ "action", action },
				{ "path", path },
			};
		}
		if (action == "set_depth_culling_performance_mode" &&
			(!a_args.contains("enabled") || !a_args.at("enabled").is_boolean())) {
			return {
				{ "error", "set_depth_culling_performance_mode requires boolean enabled" },
				{ "action", action },
			};
		}
		const bool enabled = a_args.value("enabled", false);

		return RunOnMainThread([action, path, enabled]() -> json {
			auto* menu = globals::menu;
			if (!menu)
				return { { "error", "CSX menu unavailable" } };
			json delegatedRequest = nullptr;
			if (action == "open") {
				menu->OpenMenu();
			} else if (action == "close") {
				menu->CloseMenu();
			} else if (action == "screenshot") {
				delegatedRequest = globals::features::screenshotFeature.RequestLegacyCapture("communityshaders.menu");
			} else if (action == "set_path") {
				auto& vr = globals::features::vr;
				vr.HideOverlaysIfPresent();
				if (path == "overlay")
					vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::IVROverlay;
				else if (path == "in_scene")
					vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::InScene;
				else
					vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::Auto;
				vr.InvalidatePresentedMenuSurfaces();
				vr.ResetMenuInputRuntimeState();
			} else if (action == "set_depth_culling_performance_mode") {
				globals::features::vr.SetDepthCullingPerformanceMode(enabled);
			}
			if (action == "texture_stats")
				return { { "action", action }, { "texture", InspectMenuTexture() }, { "status", BuildStatus() } };
			return { { "action", action }, { "path", path }, { "delegatedRequest", std::move(delegatedRequest) }, { "status", BuildStatus() } };
		});
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = BuildResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		BuildProvenance::AttachProducer(output);
		try {
			const std::string serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}
}

namespace MenuDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("MenuDevBenchBridge: devbench host not present; menu tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"({"description":"Inspect and control the CSX VR menu and depth-culling A/B policy. Every response identifies the exact producing DLL. expectedBuildId makes requests fail closed when the loaded binary is not the intended build.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","open","close","screenshot","set_path","texture_stats","set_depth_culling_performance_mode"],"default":"status"},"path":{"type":"string","enum":["auto","overlay","in_scene"]},"enabled":{"type":"boolean","description":"Performance Mode state required by set_depth_culling_performance_mode."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."}}}})";
		devBench->RegisterTool("communityshaders.menu", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("MenuDevBenchBridge: registered communityshaders.menu with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace MenuDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
