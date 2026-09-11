#include "MenuDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/DynamicCubemaps.h"
#	include "Features/FoliageLighting.h"
#	include "Features/ScreenshotFeature.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Features/VRDepthCullingTemporal.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "MenuDevBenchPreflightPolicy.h"
#	include "State.h"
#	include "TruePBR.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <atomic>
#	include <cstdint>
#	include <functional>
#	include <limits>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json MotionSharpeningStatus()
	{
		const auto& upscaling = globals::features::upscaling;
		const auto& settings = upscaling.settings;
		const auto sharpener = upscaling.GetDLSSSharpenerMode();
		const bool luma = sharpener == Upscaling::DLSSSharpenerMode::LumaUnsharp;
		return {
			{ "enabled", settings.motionAdaptiveRCAS },
			{ "adjustment", settings.motionSharpnessAdjustment },
			{ "thresholdPixels", settings.motionSharpnessThreshold },
			{ "strengthCap", settings.motionSharpnessCap },
			{ "applicable", upscaling.GetRuntimeUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS &&
								sharpener != Upscaling::DLSSSharpenerMode::Off && settings.sharpnessDLSS > 0.0f },
			{ "sharpener", sharpener == Upscaling::DLSSSharpenerMode::Off ? "off" : luma ? "luma_unsharp" :
																						   "rcas" },
			{ "lastDispatch", sharpener == Upscaling::DLSSSharpenerMode::Off ? "not_applicable" :
							  luma                                           ? Upscaling::lumaSharpen.GetMotionAdaptiveStatus() :
																			   Upscaling::rcas.GetMotionAdaptiveStatus() },
			{ "lastRCASDispatch", Upscaling::rcas.GetMotionAdaptiveStatus() },
			{ "lastLumaDispatch", Upscaling::lumaSharpen.GetMotionAdaptiveStatus() },
		};
	}

	struct CocPreflightSnapshot
	{
		MenuDevBenchPreflightPolicy::State state;
		bool stabilizerFileExists = false;
		bool stabilizerFileReadable = false;
		bool stabilizerSwitchingEnabled = false;
		bool stabilizerHasUpscalingProfile = false;
		std::string stabilizerIniName;
		std::string logLevel;
		int logLevelValue = 0;
	};

	CocPreflightSnapshot CaptureCocPreflightSnapshot()
	{
		auto* state = globals::state;
		auto& upscaling = globals::features::upscaling;
		const auto& settings = upscaling.settings;
		const auto& stabilizer = upscaling.GetVRFpsStabilizerSessionConfig();
		const auto logLevel = state ? state->GetLogLevel() : spdlog::level::off;

		return {
			.state = {
				.vr = globals::game::isVR,
				.inGame = state &&
			              !state->isMainMenuOpen &&
			              !state->isLoadingMenuOpen &&
			              RE::PlayerCharacter::GetSingleton() != nullptr,
				.stabilizerActiveForSession = upscaling.IsVRFpsStabilizerSyncActive(),
				.developerMode = state && state->IsDeveloperMode(),
				.foveatedVendorDispatch = settings.foveatedVendorDispatch,
				.foveatedCenterArea = settings.foveatedCenterArea,
				.peripheryTAAEnabled = settings.periphery_taa_enable,
				.peripheryTAACenterArea = settings.periphery_taa_center_area,
				.peripheryTAAOuterScale = settings.periphery_taa_outer_scale,
			},
			.stabilizerFileExists = stabilizer.fileExists,
			.stabilizerFileReadable = stabilizer.fileReadable,
			.stabilizerSwitchingEnabled = stabilizer.upscalingSwitchingEnabled,
			.stabilizerHasUpscalingProfile = stabilizer.HasAnyUpscalingProfile(),
			.stabilizerIniName = stabilizer.path.filename().string(),
			.logLevel = std::string(magic_enum::enum_name(logLevel)),
			.logLevelValue = static_cast<int>(logLevel),
		};
	}

	json CocPreflightSnapshotJson(const CocPreflightSnapshot& a_snapshot, MenuDevBenchPreflightPolicy::Preparation a_preparation)
	{
		const auto& state = a_snapshot.state;
		return {
			{ "ready", MenuDevBenchPreflightPolicy::IsReady(state, a_preparation) },
			{ "vr", state.vr },
			{ "inGame", state.inGame },
			{ "developerMode", {
								   { "active", state.developerMode },
								   { "logLevel", a_snapshot.logLevel },
								   { "logLevelValue", a_snapshot.logLevelValue },
							   } },
			{ "foveation", {
							   { "ready", MenuDevBenchPreflightPolicy::HasRequiredFoveation(state) },
							   { "foveatedVendorDispatch", state.foveatedVendorDispatch },
							   { "foveatedCenterArea", state.foveatedCenterArea },
							   { "peripheryTAAEnable", state.peripheryTAAEnabled },
							   { "peripheryTAACenterArea", state.peripheryTAACenterArea },
							   { "peripheryTAAOuterScale", state.peripheryTAAOuterScale },
						   } },
			{ "vrFpsStabilizer", {
									 { "activeForSession", state.stabilizerActiveForSession },
									 { "fileExistsAtStartup", a_snapshot.stabilizerFileExists },
									 { "fileReadableAtStartup", a_snapshot.stabilizerFileReadable },
									 { "switchingEnabledAtStartup", a_snapshot.stabilizerSwitchingEnabled },
									 { "hasUpscalingProfileAtStartup", a_snapshot.stabilizerHasUpscalingProfile },
									 { "iniName", a_snapshot.stabilizerIniName },
								 } },
		};
	}

	std::string CocPreflightBlockCode(const CocPreflightSnapshot& a_snapshot, MenuDevBenchPreflightPolicy::Preparation a_preparation)
	{
		if (!a_snapshot.state.vr)
			return "skyrim_vr_required";
		if (!a_snapshot.state.inGame)
			return "in_game_state_required";
		if (a_preparation == MenuDevBenchPreflightPolicy::Preparation::Coc && !a_snapshot.state.stabilizerActiveForSession)
			return "vr_fps_stabilizer_required";
		return "preflight_not_ready";
	}

	json PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation a_preparation)
	{
		const auto action = a_preparation == MenuDevBenchPreflightPolicy::Preparation::Coc ? "prepare_coc" : "prepare_tuning";
		const auto before = CaptureCocPreflightSnapshot();
		if (!MenuDevBenchPreflightPolicy::CanApplyRuntimeSettings(before.state, a_preparation)) {
			return {
				{ "action", action },
				{ "applied", false },
				{ "changed", false },
				{ "persisted", false },
				{ "ready", false },
				{ "promptRequired", true },
				{ "errorCode", CocPreflightBlockCode(before, a_preparation) },
				{ "before", CocPreflightSnapshotJson(before, a_preparation) },
				{ "after", CocPreflightSnapshotJson(before, a_preparation) },
			};
		}

		json changes = json::array();
		if (!before.state.developerMode) {
			globals::state->SetLogLevel(spdlog::level::debug);
			changes.push_back("developer_mode");
		}

		auto& settings = globals::features::upscaling.settings;
		if (!settings.foveatedVendorDispatch) {
			settings.foveatedVendorDispatch = true;
			changes.push_back("foveated_vendor_dispatch");
		}
		if (!MenuDevBenchPreflightPolicy::NearlyEqual(
				settings.foveatedCenterArea,
				MenuDevBenchPreflightPolicy::kFoveatedCenterArea)) {
			settings.foveatedCenterArea =
				static_cast<float>(MenuDevBenchPreflightPolicy::kFoveatedCenterArea);
			changes.push_back("foveated_center_area");
		}
		if (!settings.periphery_taa_enable) {
			settings.periphery_taa_enable = true;
			changes.push_back("periphery_taa");
		}
		if (!MenuDevBenchPreflightPolicy::NearlyEqual(
				settings.periphery_taa_center_area,
				MenuDevBenchPreflightPolicy::kPeripheryTAACenterArea)) {
			settings.periphery_taa_center_area =
				static_cast<float>(MenuDevBenchPreflightPolicy::kPeripheryTAACenterArea);
			changes.push_back("periphery_taa_center_area");
		}
		if (!MenuDevBenchPreflightPolicy::NearlyEqual(
				settings.periphery_taa_outer_scale,
				MenuDevBenchPreflightPolicy::kPeripheryTAAOuterScale)) {
			settings.periphery_taa_outer_scale =
				static_cast<float>(MenuDevBenchPreflightPolicy::kPeripheryTAAOuterScale);
			changes.push_back("periphery_taa_outer_scale");
		}

		const auto after = CaptureCocPreflightSnapshot();
		const bool ready = MenuDevBenchPreflightPolicy::IsReady(after.state, a_preparation);
		json result = {
			{ "action", action },
			{ "applied", true },
			{ "changed", !changes.empty() },
			{ "persisted", false },
			{ "ready", ready },
			{ "promptRequired", !ready },
			{ "changes", std::move(changes) },
			{ "before", CocPreflightSnapshotJson(before, a_preparation) },
			{ "after", CocPreflightSnapshotJson(after, a_preparation) },
		};
		if (!ready)
			result["errorCode"] = CocPreflightBlockCode(after, a_preparation);
		return result;
	}

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
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run));
	}

	json BuildStatus()
	{
		auto* menu = globals::menu;
		auto& vr = globals::features::vr;
		auto& screenshot = globals::features::screenshotFeature;
		auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		const auto inSceneSubmitSuppressionReasons =
			globals::features::upscaling.GetVRInSceneOverlaySubmitSuppressionReasons();
		const auto depthCullingTemporal = VRDepthCullingTemporal::GetStatus();
		auto* drawData = ImGui::GetCurrentContext() ? ImGui::GetDrawData() : nullptr;
		const auto fixedWorldPosition = vr.fixedWorldOverlayPosition.m.Translation();
		const auto effectiveAttachMode = vr.GetEffectiveMenuAttachMode();
		const auto effectiveHMDOffset = vr.GetEffectiveHMDMenuOffset();
		const json depthCullingTemporalStatus = {
			{ "installed", depthCullingTemporal.installed },
			{ "cullingEnabled", depthCullingTemporal.cullingEnabled },
			{ "telemetryEnabled", depthCullingTemporal.telemetryEnabled },
			{ "policy", VRDepthCullingTemporal::GetModeName(depthCullingTemporal.mode) },
			{ "envelopeMisses", depthCullingTemporal.envelopeMisses },
			{ "recoveryAttempts", depthCullingTemporal.recoveryAttempts },
			{ "objectsInspected", depthCullingTemporal.objectsInspected },
			{ "invalidTransforms", depthCullingTemporal.invalidTransforms },
			{ "invalidMotionEnvelopes", depthCullingTemporal.invalidMotionEnvelopes },
			{ "frustumTests", depthCullingTemporal.frustumTests },
			{ "totalEligible", depthCullingTemporal.totalEligible },
			{ "totalPromoted", depthCullingTemporal.totalPromoted },
			{ "totalDurationNanoseconds", depthCullingTemporal.totalDurationNanoseconds },
			{ "maximumDurationNanoseconds", depthCullingTemporal.maximumDurationNanoseconds },
			{ "durationHistogramNanoseconds", {
												  { "upperBounds", [] {
													   json bounds = VRDepthCullingTelemetryPolicy::DurationUpperBoundsNanoseconds;
													   bounds.push_back(nullptr);
													   return bounds;
												   }() },
												  { "counts", depthCullingTemporal.durationHistogram },
											  } },
			{ "lastObjectCount", depthCullingTemporal.lastObjectCount },
			{ "lastEligibleCount", depthCullingTemporal.lastEligibleCount },
			{ "lastPromotedCount", depthCullingTemporal.lastPromotedCount },
		};
		return {
			{ "menuEnabled", menu && menu->IsEnabled },
			{ "menuSessionOpen", menu && menu->IsMenuSessionOpen() },
			{ "menuLayoutUnlocked", vr.settings.UnlockMenuPositionAndSize },
			{ "desktopMenuCanvasLocked", globals::game::isVR && !vr.settings.UnlockMenuPositionAndSize },
			{ "controllerGripDragEnabled", vr.settings.UnlockMenuPositionAndSize && vr.settings.EnableDragToReposition },
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
			{ "attachMode", static_cast<int>(effectiveAttachMode) },
			{ "savedAttachMode", static_cast<int>(vr.settings.attachMode) },
			{ "menuOverlayPath", static_cast<int>(vr.settings.menuOverlayPath) },
			{ "menuPositioningMethod", vr.UseFixedWorldMenuPositioning() ? 1 : 0 },
			{ "savedMenuPositioningMethod", vr.settings.VRMenuPositioningMethod },
			{ "effectiveFixedWorldPositioning", vr.UseFixedWorldMenuPositioning() },
			{ "fixedWorldPositionInitialized", vr.fixedWorldOverlayPosition.initialized },
			{ "savedUnlockedFixedWorldPositionInitialized", vr.savedUnlockedFixedWorldOverlayPosition.initialized },
			{ "fixedWorldReanchorRequested", vr.fixedWorldOverlayReanchorRequested },
			{ "fixedWorldPosition", {
										{ "x", fixedWorldPosition.x },
										{ "y", fixedWorldPosition.y },
										{ "z", fixedWorldPosition.z },
									} },
			{ "menuScale", vr.GetEffectiveMenuScale() },
			{ "savedMenuScale", vr.settings.VRMenuScale },
			{ "depthCullingExteriorEnabled", vr.settings.EnableDepthBufferCullingExterior },
			{ "depthCullingInteriorEnabled", vr.settings.EnableDepthBufferCullingInterior },
			{ "depthCullingPerformanceMode", vr.settings.DepthCullingPerformanceMode },
			{ "depthCullingLegacyMode", vr.settings.DepthCullingLegacyMode },
			{ "foliageLightingEnabled", globals::features::foliageLighting.IsEnabled() },
			{ "motionAdaptiveSharpening", MotionSharpeningStatus() },
			{ "foliageLightingActive", globals::features::foliageLighting.IsRuntimeEnabled() },
			{ "truePbrVerboseJsonLogging", globals::features::truePBR.enableVerboseJsonLogging },
			{ "dynamicCubemaps", {
									 { "configuredResolution", dynamicCubemaps.settings.CubemapResolution },
									 { "activeResolution", dynamicCubemaps.GetActiveCubemapResolution() },
									 { "activeMipLevels", dynamicCubemaps.GetActiveCubemapMipLevels() },
									 { "restartRequired", dynamicCubemaps.IsCubemapResolutionRestartRequired() },
								 } },
			{ "depthCullingTemporal", depthCullingTemporalStatus },
			{ "menuOffsetX", effectiveHMDOffset.x },
			{ "menuOffsetY", effectiveHMDOffset.y },
			{ "menuOffsetZ", effectiveHMDOffset.z },
			{ "savedMenuOffsetX", vr.settings.VRMenuOffsetX },
			{ "savedMenuOffsetY", vr.settings.VRMenuOffsetY },
			{ "savedMenuOffsetZ", vr.settings.VRMenuOffsetZ },
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
		if (action != "status" && action != "open" && action != "close" && action != "screenshot" && action != "set_path" && action != "set_layout_unlocked" && action != "texture_stats" && action != "set_depth_culling_performance_mode" && action != "set_depth_culling_legacy_mode" && action != "set_depth_culling_telemetry_enabled" && action != "reset_depth_culling_telemetry" && action != "set_foliage_lighting_enabled" && action != "set_truepbr_verbose_json_logging" && action != "set_dynamic_cubemap_resolution" && action != "prepare_coc" && action != "prepare_tuning" && action != "set_motion_adaptive_sharpening") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "open", "close", "screenshot", "set_path", "set_layout_unlocked", "texture_stats", "set_depth_culling_performance_mode", "set_depth_culling_legacy_mode", "set_depth_culling_telemetry_enabled", "reset_depth_culling_telemetry", "set_foliage_lighting_enabled", "set_truepbr_verbose_json_logging", "set_dynamic_cubemap_resolution", "prepare_coc", "prepare_tuning", "set_motion_adaptive_sharpening" }) },
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
		if ((action == "set_layout_unlocked" || action == "set_depth_culling_performance_mode" || action == "set_depth_culling_legacy_mode" || action == "set_depth_culling_telemetry_enabled" || action == "set_foliage_lighting_enabled" || action == "set_truepbr_verbose_json_logging") &&
			(!a_args.contains("enabled") || !a_args.at("enabled").is_boolean())) {
			return {
				{ "error", action + " requires boolean enabled" },
				{ "action", action },
			};
		}
		if (action == "set_dynamic_cubemap_resolution" &&
			(!a_args.contains("resolution") || !a_args.at("resolution").is_number_integer())) {
			return {
				{ "error", "set_dynamic_cubemap_resolution requires integer resolution" },
				{ "action", action },
			};
		}
		MotionSharpening::Settings motionSharpening{};
		if (action == "set_motion_adaptive_sharpening") {
			if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean() ||
				!a_args.contains("adjustment") || !a_args["adjustment"].is_number() ||
				!a_args.contains("thresholdPixels") || !a_args["thresholdPixels"].is_number() ||
				!a_args.contains("strengthCap") || !a_args["strengthCap"].is_number()) {
				return { { "error", "requires enabled, adjustment, thresholdPixels, and strengthCap" }, { "action", action } };
			}
			if (!MotionSharpening::TryCreateSettings(a_args["enabled"].get<bool>(), a_args["adjustment"].get<double>(),
					a_args["thresholdPixels"].get<double>(), a_args["strengthCap"].get<double>(), motionSharpening)) {
				return { { "error", "adjustment must be [-1,1], thresholdPixels [0,64], and strengthCap [0,1]; all must be finite" }, { "action", action } };
			}
		}
		const bool enabled = a_args.value("enabled", false);
		const uint32_t resolution = a_args.value("resolution", 0u);
		if (action == "set_dynamic_cubemap_resolution" &&
			resolution != DynamicCubemaps::kPerformanceCubemapResolution &&
			resolution != DynamicCubemaps::kQualityCubemapResolution) {
			return {
				{ "error", "set_dynamic_cubemap_resolution requires resolution 128 or 256" },
				{ "action", action },
				{ "resolution", resolution },
			};
		}

		return RunOnMainThread([action, path, enabled, resolution, motionSharpening]() -> json {
			if (action == "prepare_coc")
				return PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation::Coc);
			if (action == "prepare_tuning")
				return PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation::Tuning);
			if (action == "set_depth_culling_telemetry_enabled") {
				VRDepthCullingTemporal::SetTelemetryEnabled(enabled);
				return { { "action", action }, { "enabled", enabled }, { "status", BuildStatus() } };
			}
			if (action == "reset_depth_culling_telemetry") {
				if (!VRDepthCullingTemporal::TryResetStatus()) {
					return {
						{ "error", "depth-culling telemetry is currently being updated" },
						{ "errorCode", "depth_culling_telemetry_busy" },
						{ "retrySafe", true },
					};
				}
				return { { "action", action }, { "reset", true }, { "status", BuildStatus() } };
			}
			auto* menu = globals::menu;
			if (!menu)
				return { { "error", "CSX menu unavailable" } };
			if (action == "set_motion_adaptive_sharpening") {
				auto& settings = globals::features::upscaling.settings;
				settings.motionAdaptiveRCAS = motionSharpening.enabled;
				settings.motionSharpnessAdjustment = motionSharpening.adjustment;
				settings.motionSharpnessThreshold = motionSharpening.thresholdPixels;
				settings.motionSharpnessCap = motionSharpening.strengthCap;
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "motionAdaptiveSharpening", MotionSharpeningStatus() } };
			}
			json delegatedRequest = nullptr;
			json deprecation = nullptr;
			if (action == "open") {
				menu->OpenMenu();
			} else if (action == "close") {
				menu->CloseMenu();
			} else if (action == "screenshot") {
				delegatedRequest = globals::features::screenshotFeature.RequestApiCapture("communityshaders.menu");
				deprecation = {
					{ "obsolete", true },
					{ "message", "communityshaders.menu screenshot is obsolete; migrate to communityshaders.screenshot contractMajor 1" },
					{ "replacement", {
										 { "tool", "communityshaders.screenshot" },
										 { "contractMajor", 1 },
										 { "action", "capture" },
									 } },
				};
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
			} else if (action == "set_layout_unlocked") {
				globals::features::vr.SetMenuLayoutUnlocked(enabled);
			} else if (action == "set_depth_culling_performance_mode") {
				globals::features::vr.SetDepthCullingPerformanceMode(enabled);
			} else if (action == "set_depth_culling_legacy_mode") {
				globals::features::vr.SetDepthCullingLegacyMode(enabled);
			} else if (action == "set_foliage_lighting_enabled") {
				globals::features::foliageLighting.SetEnabled(enabled);
				menu->RequestSettingsDirtyCheck();
			} else if (action == "set_truepbr_verbose_json_logging") {
				globals::features::truePBR.enableVerboseJsonLogging = enabled;
			} else if (action == "set_dynamic_cubemap_resolution") {
				globals::features::dynamicCubemaps.SetCubemapResolution(resolution);
				menu->RequestSettingsDirtyCheck();
			}
			if (action == "texture_stats")
				return { { "action", action }, { "texture", InspectMenuTexture() }, { "status", BuildStatus() } };
			if (action == "set_dynamic_cubemap_resolution")
				return { { "action", action }, { "resolution", resolution }, { "persisted", false }, { "status", BuildStatus() } };
			json result = {
				{ "action", action },
				{ "path", path },
				{ "delegatedRequest", std::move(delegatedRequest) },
				{ "status", BuildStatus() },
			};
			if (!deprecation.is_null())
				result["deprecation"] = std::move(deprecation);
			return result;
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
			R"({"description":"Inspect and control the CSX VR menu, desktop/headset layout lock, depth-culling A/B policy and recovery telemetry, Foliage Lighting runtime state, TruePBR verbose JSON logging, dynamic cubemap resolution, and optional DLSS/DLAA RCAS and Luma Unsharp motion-adaptive sharpening. set_motion_adaptive_sharpening applies signed strength adjustment above a threshold in output pixels per frame, capped on the 0-1 sharpness scale; it requires all four settings and stages them until settings are saved. The selected fixed sharpener is used without valid current motion; status reports the selected sharpener, last dispatch result and current applicability. FSR sharpening is unchanged. The screenshot action is obsolete and retained temporarily for migration; use communityshaders.screenshot contractMajor 1 instead. set_layout_unlocked enables desktop move, resize, and docking plus headset custom placement and grip dragging. Resolution changes are staged in memory; save settings and restart to apply them. Depth-culling telemetry controls affect measurements only, never culling policy. prepare_coc is a one-shot pre-assay gate: it requires in-game Skyrim VR and startup-active VR FPS Stabilizer profile sync, then enables runtime-only developer mode and the fixed FOV plus TAA 0.3/0.7 fixture without saving settings. prepare_tuning applies the same runtime-only fixture in-game without requiring VR FPS Stabilizer profile sync. Neither preparation action changes cells. Every response identifies the exact producing DLL. expectedBuildId makes requests fail closed when the loaded binary is not the intended build.","inputSchema":{"type":"object","properties":{"action":{"type":"string","description":"screenshot is obsolete; use communityshaders.screenshot contractMajor 1 action capture","enum":["status","open","close","screenshot","set_path","set_layout_unlocked","texture_stats","set_depth_culling_performance_mode","set_depth_culling_legacy_mode","set_depth_culling_telemetry_enabled","reset_depth_culling_telemetry","set_foliage_lighting_enabled","set_truepbr_verbose_json_logging","set_dynamic_cubemap_resolution","prepare_coc","prepare_tuning","set_motion_adaptive_sharpening"],"default":"status"},"path":{"type":"string","enum":["auto","overlay","in_scene"]},"enabled":{"type":"boolean","description":"Boolean state required by a setter action."},"adjustment":{"type":"number","minimum":-1,"maximum":1,"description":"Signed RCAS or Luma Unsharp sharpness adjustment in motion."},"thresholdPixels":{"type":"number","minimum":0,"maximum":64,"description":"Motion threshold in output pixels per frame."},"strengthCap":{"type":"number","minimum":0,"maximum":1,"description":"Maximum adjusted strength on the DLSS sharpness slider scale."},"resolution":{"type":"integer","enum":[128,256],"description":"Dynamic cubemap resolution staged for the next game restart."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."}}}})";
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
