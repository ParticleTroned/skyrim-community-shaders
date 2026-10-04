#include "MenuDevBenchBridge.h"
#include "Features/Upscaling/FoveatedBlendPolicy.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/AdaptiveBrightness.h"
#	include "Features/DynamicCubemaps.h"
#	include "Features/ExtendedMaterials.h"
#	include "Features/FoliageLighting.h"
#	include "Features/LinearLighting.h"
#	include "Features/ScreenSpaceGI.h"
#	include "Features/ScreenSpaceShadows.h"
#	include "Features/ScreenshotFeature.h"
#	include "Features/SkySync.h"
#	include "Features/TerrainVariation.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Features/VRDepthCullingTemporal.h"
#	include "Features/VRHybridCulling.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "MenuDevBenchPreflightPolicy.h"
#	include "MenuDepthCullingDiagnostics.h"
#	include "MenuDepthCullingSettingsPolicy.h"
#	include "State.h"
#	include "TruePBR.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cmath>
#	include <cstdint>
#	include <functional>
#	include <limits>
#	include <stdexcept>
#	include <string>
#	include <string_view>

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

	std::string ValidateAdaptiveBalanceVisuals(const json& a_visuals)
	{
		if (!a_visuals.is_object() || a_visuals.empty())
			return "visuals must be a nonempty object";
		struct Field
		{
			std::string_view name;
			double minimum;
			double maximum;
		};
		static constexpr std::array<Field, 18> fields{ { { "skySaturation", 0.0, 2.0 },
			{ "cloudBrightness", 0.0, 2.0 },
			{ "cloudSaturation", 0.0, 2.0 },
			{ "cloudGammaOffset", -1.0, 1.0 },
			{ "fogIntensity", 0.0, 5.0 },
			{ "sunGlareIntensity", 0.0, 5.0 },
			{ "effectBrightness", 0.0, 2.0 },
			{ "skyStaticBrightness", 0.0, 2.0 },
			{ "skyStaticTransparency", 0.0, 1.0 },
			{ "contrast", 0.5, 2.0 },
			{ "saturation", 0.0, 2.0 },
			{ "ambient", 0.0, 5.0 },
			{ "causticsStrength", 0.0, 2.0 },
			{ "causticsTiling", 0.25, 4.0 },
			{ "causticsSpeed", 0.0, 3.0 },
			{ "causticsDispersion", 0.0, 2.0 },
			{ "parallaxStrength", 0.0, 2.0 },
			{ "parallaxQuality", 4.0, 64.0 } } };
		for (const auto& [name, value] : a_visuals.items()) {
			if (name == "lightingAdvanced" || name == "useAmbientEffectLighting") {
				if (!value.is_boolean())
					return name + " must be boolean";
				continue;
			}
			const auto field = std::find_if(fields.begin(), fields.end(), [&](const Field& entry) { return entry.name == name; });
			if (field == fields.end() || !value.is_number())
				return "Unknown or non-numeric visuals field: " + name;
			const double number = value.get<double>();
			if (!std::isfinite(number) || number < field->minimum || number > field->maximum ||
				(name == "parallaxQuality" && !value.is_number_integer()))
				return "Out-of-range or invalid visuals field: " + name;
		}
		return {};
	}

	json AdaptiveBalanceVisualsStatus()
	{
		const auto& balance = globals::features::adaptiveBrightness;
		const auto& profile = balance.settings.globalProfile;
		const auto waterFields = [](const auto& water) -> json {
			return {
				{ "causticsStrength", water.CausticsStrength },
				{ "causticsTiling", water.CausticsTiling },
				{ "causticsSpeed", water.CausticsSpeed },
				{ "causticsDispersion", water.CausticsDispersion },
				{ "parallaxStrength", water.ParallaxStrength },
				{ "parallaxQuality", water.ParallaxQuality }
			};
		};
		auto configured = waterFields(profile.water);
		configured["skySaturation"] = profile.skySaturation;
		configured["cloudBrightness"] = profile.cloudBrightnessMult;
		configured["cloudSaturation"] = profile.cloudSaturation;
		configured["cloudGammaOffset"] = profile.cloudGammaOffset;
		configured["fogIntensity"] = profile.fogIntensity;
		configured["sunGlareIntensity"] = profile.sunGlareIntensity;
		configured["effectBrightness"] = profile.effectBrightness;
		configured["skyStaticBrightness"] = profile.skyStaticBrightness;
		configured["skyStaticTransparency"] = profile.skyStaticTransparency;
		configured["contrast"] = profile.contrast;
		configured["saturation"] = profile.saturation;
		configured["ambient"] = profile.ambientMult;
		configured["lightingAdvanced"] = profile.advanced;
		configured["useAmbientEffectLighting"] = balance.settings.useAmbientEffectLighting;
		auto effective = waterFields(balance.GetEffectiveWaterAppearanceSettings());
		const auto lighting = balance.GetEffectiveSharedLightingSettings();
		effective["skySaturation"] = lighting.skySaturation;
		effective["cloudBrightness"] = lighting.cloudBrightness;
		effective["cloudSaturation"] = lighting.cloudSaturation;
		const auto& linear = globals::features::linearLighting;
		effective["cloudGamma"] = balance.GetEffectiveLinearLightingSettings(linear.settings, linear.IsRuntimeEnabled()).settings.cloudGamma;
		effective["fogIntensity"] = lighting.fogIntensity;
		effective["sunGlareIntensity"] = lighting.sunGlareIntensity;
		effective["effectBrightness"] = lighting.effectBrightness;
		effective["skyStaticBrightness"] = lighting.skyStaticBrightness;
		effective["skyStaticTransparency"] = lighting.skyStaticTransparency;
		effective["contrast"] = lighting.contrast;
		effective["saturation"] = lighting.saturation;
		effective["ambient"] = lighting.ambientMult;
		effective["useAmbientEffectLighting"] = balance.IsRuntimeEnabled() && balance.settings.useAmbientEffectLighting;
		return { { "global", std::move(configured) }, { "effective", std::move(effective) } };
	}

	std::string ValidateFovBlendCurve(const json& a_args)
	{
		if (!a_args.contains("enabled") || !a_args.at("enabled").is_boolean())
			return "set_fov_blend_curve requires boolean enabled";
		if (a_args.contains("falloff")) {
			const auto& value = a_args.at("falloff");
			if (!value.is_number())
				return "falloff must be a finite number in [0.5,2]";
			const double falloff = value.get<double>();
			if (!std::isfinite(falloff) || falloff < FoveatedBlendPolicy::MinFalloff || falloff > FoveatedBlendPolicy::MaxFalloff)
				return "falloff must be a finite number in [0.5,2]";
		}
		return {};
	}

	json FovSettingsStatus()
	{
		auto& upscaling = globals::features::upscaling;
		auto& ssgi = globals::features::screenSpaceGI;
		auto& shadows = globals::features::screenSpaceShadows;
		const bool maskActive = upscaling.IsSharedFoveatedMaskActive();
		return {
			{ "enabled", upscaling.settings.foveatedVendorDispatch },
			{ "maskActive", maskActive },
			{ "blendCurve", {
								{ "enabled", upscaling.settings.foveatedBlendCurveEnabled },
								{ "falloff", upscaling.settings.foveatedBlendFalloff },
								{ "effectiveFalloff", upscaling.GetFoveatedBlendFalloff() },
								{ "applicable", maskActive },
							} },
			{ "ssgiSelected", ssgi.settings.EnableFoveated },
			{ "ssgiAvailable", maskActive && ssgi.IsRuntimeEnabled() && !ssgi.settings.ExperimentalOCUEffectFoveation },
			{ "screenSpaceShadowsSelected", shadows.bendSettings.EnableFoveated != 0 },
			{ "screenSpaceShadowsAvailable", maskActive && shadows.IsRuntimeEnabled() },
		};
	}

	json BuildDepthCullingStatus()
	{
		const auto depthCullingTemporal = VRDepthCullingTemporal::GetStatus();
		const auto hybridCulling = VRHybridCulling::GetStatus(depthCullingTemporal.cullingEpoch,
			depthCullingTemporal.mode == VRDepthCullingTemporal::Mode::Hybrid,
			depthCullingTemporal.cullingEnabled, depthCullingTemporal.hybridInstalled);
		return MenuDepthCullingDiagnostics::BuildStatus(depthCullingTemporal, hybridCulling);
	}

	json BuildStatus()
	{
		auto* menu = globals::menu;
		auto& vr = globals::features::vr;
		auto& screenshot = globals::features::screenshotFeature;
		auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		const auto inSceneSubmitSuppressionReasons =
			globals::features::upscaling.GetVRInSceneOverlaySubmitSuppressionReasons();
		auto* drawData = ImGui::GetCurrentContext() ? ImGui::GetDrawData() : nullptr;
		const auto fixedWorldPosition = vr.fixedWorldOverlayPosition.m.Translation();
		const auto effectiveAttachMode = vr.GetEffectiveMenuAttachMode();
		const auto effectiveHMDOffset = vr.GetEffectiveHMDMenuOffset();

		return {
			{ "menuEnabled", menu && menu->IsEnabled },
			{ "fov", FovSettingsStatus() },
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
			{ "depthCullingExteriorMinExtent", vr.settings.MinOccludeeBoxExtentExterior },
			{ "depthCullingInteriorMinExtent", vr.settings.MinOccludeeBoxExtentInterior },
			{ "depthCullingConfiguredPolicy", VRDepthCullingTemporal::GetModeName(vr.GetDepthCullingMode()) },
			{ "depthCullingLegacyMode", vr.settings.DepthCullingLegacyMode },
			{ "adaptiveBalanceEnabled", globals::features::adaptiveBrightness.settings.enabled },
			{ "adaptiveBalanceActive", globals::features::adaptiveBrightness.IsRuntimeEnabled() },
			{ "adaptiveBalanceWeatherColorsAvailable", globals::features::adaptiveBrightness.weatherColorHookInstalled },
			{ "adaptiveBalanceVisuals", AdaptiveBalanceVisualsStatus() },
			{ "skySyncSunlight", { { "loaded", globals::features::skySync.loaded }, { "skySyncEnabled", globals::features::skySync.settings.Enabled }, { "dimmingEnabled", globals::features::skySync.settings.DimSunlightUnderHorizon }, { "fadeHours", globals::features::skySync.settings.HorizonFadeHours }, { "appliedFactor", globals::features::skySync.GetSunlightDimmingFactor() } } },
			{ "foliageLightingEnabled", globals::features::foliageLighting.IsEnabled() },
			{ "motionAdaptiveSharpening", MotionSharpeningStatus() },
			{ "foliageLightingActive", globals::features::foliageLighting.IsRuntimeEnabled() },
			{ "terrainVariationMeshEnabled", globals::features::terrainVariation.settings.enableMeshSupport != 0 },
			{ "terrainVariationMeshActive", globals::features::terrainVariation.IsMeshSupportEnabled() },
			{ "parallaxStrength", globals::features::extendedMaterials.settings.ParallaxStrength },
			{ "extendedMaterialsLoaded", globals::features::extendedMaterials.loaded },
			{ "truePbrVerboseJsonLogging", globals::features::truePBR.enableVerboseJsonLogging },
			{ "dynamicCubemaps", {
									 { "configuredResolution", dynamicCubemaps.settings.CubemapResolution },
									 { "activeResolution", dynamicCubemaps.GetActiveCubemapResolution() },
									 { "activeMipLevels", dynamicCubemaps.GetActiveCubemapMipLevels() },
									 { "restartRequired", dynamicCubemaps.IsCubemapResolutionRestartRequired() },
								 } },
			{ "depthCullingTemporal", BuildDepthCullingStatus() },
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
		if (action != "status" && action != "depth_culling_snapshot" && action != "open" && action != "open_vr_fov" && action != "set_fov_enabled" && action != "set_fov_blend_curve" && action != "close" && action != "screenshot" && action != "set_path" && action != "set_layout_unlocked" && action != "texture_stats" && action != "set_depth_culling_settings" && action != "set_depth_culling_method" && action != "set_depth_culling_legacy_mode" && action != "set_depth_culling_telemetry_enabled" && action != "set_depth_culling_traversal_diagnostics_enabled" && action != "reset_depth_culling_telemetry" && action != "set_adaptive_balance_enabled" && action != "set_adaptive_balance_visuals" && action != "set_foliage_lighting_enabled" && action != "set_terrain_variation_mesh_enabled" && action != "set_truepbr_verbose_json_logging" && action != "set_dynamic_cubemap_resolution" && action != "prepare_coc" && action != "prepare_tuning" && action != "set_motion_adaptive_sharpening" && action != "set_parallax_strength" && action != "set_sky_sync_sunlight") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "depth_culling_snapshot", "open", "open_vr_fov", "set_fov_enabled", "set_fov_blend_curve", "close", "screenshot", "set_path", "set_layout_unlocked", "texture_stats", "set_depth_culling_settings", "set_depth_culling_method", "set_depth_culling_legacy_mode", "set_depth_culling_traversal_diagnostics_enabled", "set_depth_culling_telemetry_enabled", "reset_depth_culling_telemetry", "set_adaptive_balance_enabled", "set_adaptive_balance_visuals", "set_foliage_lighting_enabled", "set_terrain_variation_mesh_enabled", "set_truepbr_verbose_json_logging", "set_dynamic_cubemap_resolution", "prepare_coc", "prepare_tuning", "set_motion_adaptive_sharpening", "set_parallax_strength", "set_sky_sync_sunlight" }) },
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
		if ((action == "set_fov_enabled" || action == "set_layout_unlocked" || action == "set_depth_culling_legacy_mode" || action == "set_depth_culling_traversal_diagnostics_enabled" || action == "set_depth_culling_telemetry_enabled" || action == "set_adaptive_balance_enabled" || action == "set_foliage_lighting_enabled" || action == "set_terrain_variation_mesh_enabled" || action == "set_truepbr_verbose_json_logging" || action == "set_sky_sync_sunlight") &&
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
		json visuals = json::object();
		float parallaxStrength = ExtendedMaterials::Settings{}.ParallaxStrength;
		if (action == "set_parallax_strength") {
			const auto value = a_args.find("strength");
			if (value == a_args.end() || !value->is_number())
				return { { "error", "set_parallax_strength requires numeric strength in [0,2]" }, { "action", action } };
			const double strength = value->get<double>();
			if (!std::isfinite(strength) || strength < ExtendedMaterials::kMinParallaxStrength || strength > ExtendedMaterials::kMaxParallaxStrength)
				return { { "error", "strength must be finite and in [0,2]" }, { "action", action } };
			parallaxStrength = static_cast<float>(strength);
		}
		if (action == "set_adaptive_balance_visuals") {
			visuals = a_args.value("visuals", json::object());
			if (const auto error = ValidateAdaptiveBalanceVisuals(visuals); !error.empty())
				return { { "error", error }, { "action", action } };
		}
		auto depthCullingMethod = VRDepthCullingTemporal::Mode::Balanced;
		if (action == "set_depth_culling_method") {
			std::string error;
			if (!MenuDepthCullingSettingsPolicy::TryParseMethod(a_args.value("method", json()), depthCullingMethod, error))
				return { { "error", error }, { "action", action } };
		}
		MenuDepthCullingSettingsPolicy::Update depthCulling;
		if (action == "set_depth_culling_settings") {
			std::string error;
			if (!MenuDepthCullingSettingsPolicy::TryParse(a_args.value("depthCulling", json::object()), depthCulling, error))
				return { { "error", error }, { "action", action } };
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
		if (action == "set_fov_blend_curve") {
			const auto error = ValidateFovBlendCurve(a_args);
			if (!error.empty())
				return { { "error", error }, { "action", action } };
		}
		const bool hasFalloff = action == "set_fov_blend_curve" && a_args.contains("falloff");
		const bool hasHorizonFadeHours = action == "set_sky_sync_sunlight" && a_args.contains("fadeHours");
		if (hasHorizonFadeHours && (!a_args.at("fadeHours").is_number() ||
									   !std::isfinite(a_args.at("fadeHours").get<double>()) ||
									   a_args.at("fadeHours").get<double>() < 0.0 ||
									   a_args.at("fadeHours").get<double>() > SkySync::MaxHorizonFadeHours))
			return { { "error", "fadeHours must be finite and between 0 and 1.5 game hours" }, { "action", action } };
		const float horizonFadeHours = hasHorizonFadeHours ? a_args.at("fadeHours").get<float>() : SkySync::DefaultHorizonFadeHours;
		const float falloff = hasFalloff ? a_args.at("falloff").get<float>() : FoveatedBlendPolicy::NeutralFalloff;
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

		return RunOnMainThread([action, path, enabled, resolution, motionSharpening, visuals, depthCulling, depthCullingMethod, hasFalloff, falloff, parallaxStrength, hasHorizonFadeHours, horizonFadeHours]() -> json {
			if ((action == "set_depth_culling_settings" || action == "set_depth_culling_method" || action == "set_depth_culling_legacy_mode" || action == "set_depth_culling_traversal_diagnostics_enabled") && !globals::game::isVR)
				return { { "error", "Depth-culling settings require Skyrim VR" }, { "errorCode", "skyrim_vr_required" }, { "action", action } };
			if (action == "depth_culling_snapshot")
				return { { "action", action }, { "frame", globals::state ? globals::state->frameCount : 0u }, { "depthCullingTemporal", BuildDepthCullingStatus() } };
			if (action == "prepare_coc")
				return PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation::Coc);
			if (action == "prepare_tuning")
				return PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation::Tuning);
			if (action == "set_depth_culling_traversal_diagnostics_enabled") {
				VRHybridCulling::SetTraversalDiagnosticsEnabled(enabled);
				return { { "action", action }, { "enabled", enabled }, { "status", BuildStatus() } };
			}
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
			if (action == "set_fov_enabled") {
				if (!globals::features::upscaling.SetFoveatedUpscalingEnabled(enabled))
					return { { "error", "FOV requires loaded VR Upscaling; enabling also requires DLSS or FSR" }, { "errorCode", "vr_fov_unavailable" }, { "action", action } };
				return { { "action", action }, { "persisted", false }, { "fov", FovSettingsStatus() } };
			}
			if (action == "set_fov_blend_curve") {
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.SetFoveatedBlendCurve(enabled, hasFalloff ? falloff : upscaling.settings.foveatedBlendFalloff))
					return { { "error", "FOV blend curve requires loaded VR Upscaling" }, { "errorCode", "vr_fov_unavailable" }, { "action", action } };
				return { { "action", action }, { "persisted", false }, { "fov", FovSettingsStatus() } };
			}
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
			if (action == "set_sky_sync_sunlight") {
				auto& skySync = globals::features::skySync;
				if (!skySync.loaded)
					return { { "error", "Sky Sync is not loaded" }, { "action", action } };
				if (!skySync.SetSunlightDimming(enabled, hasHorizonFadeHours ? horizonFadeHours : skySync.settings.HorizonFadeHours))
					return { { "error", "Invalid sunlight dimming settings" }, { "action", action } };
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			}
			if (action == "set_parallax_strength") {
				auto& materials = globals::features::extendedMaterials;
				if (!materials.loaded)
					return { { "error", "Extended Materials is not loaded" }, { "action", action } };
				materials.settings.ParallaxStrength = parallaxStrength;
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			}
			json deprecation = nullptr;
			if (action == "open_vr_fov") {
				if (!globals::features::vr.OpenFovSettings())
					return { { "error", "VR FOV settings are unavailable" }, { "errorCode", "vr_fov_unavailable" }, { "action", action } };
				menu->OpenMenu();
				return { { "action", action }, { "navigationQueued", true }, { "feature", "VR" }, { "tab", "FOV" }, { "status", BuildStatus() } };
			}
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
			} else if (action == "set_depth_culling_settings") {
				MenuDepthCullingSettingsPolicy::Apply(depthCulling, globals::features::vr.settings);
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_depth_culling_method") {
				globals::features::vr.SetDepthCullingMode(depthCullingMethod);
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "method", VRDepthCullingTemporal::GetModeName(depthCullingMethod) },
					{ "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_depth_culling_legacy_mode") {
				globals::features::vr.SetDepthCullingLegacyMode(enabled);
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "enabled", enabled },
					{ "method", VRDepthCullingTemporal::GetModeName(globals::features::vr.GetDepthCullingMode()) },
					{ "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_adaptive_balance_enabled") {
				globals::features::adaptiveBrightness.SetEnabled(enabled);
				menu->RequestSettingsDirtyCheck();
			} else if (action == "set_adaptive_balance_visuals") {
				auto& balance = globals::features::adaptiveBrightness;
				if (!balance.loaded)
					return { { "error", "Adaptive Balance is not loaded" }, { "action", action } };
				auto profile = balance.settings.globalProfile;
				profile.skySaturation = visuals.value("skySaturation", profile.skySaturation);
				profile.cloudBrightnessMult = visuals.value("cloudBrightness", profile.cloudBrightnessMult);
				profile.cloudSaturation = visuals.value("cloudSaturation", profile.cloudSaturation);
				profile.cloudGammaOffset = visuals.value("cloudGammaOffset", profile.cloudGammaOffset);
				profile.fogIntensity = visuals.value("fogIntensity", profile.fogIntensity);
				profile.sunGlareIntensity = visuals.value("sunGlareIntensity", profile.sunGlareIntensity);
				profile.effectBrightness = visuals.value("effectBrightness", profile.effectBrightness);
				profile.skyStaticBrightness = visuals.value("skyStaticBrightness", profile.skyStaticBrightness);
				profile.skyStaticTransparency = visuals.value("skyStaticTransparency", profile.skyStaticTransparency);
				profile.contrast = visuals.value("contrast", profile.contrast);
				profile.saturation = visuals.value("saturation", profile.saturation);
				profile.ambientMult = visuals.value("ambient", profile.ambientMult);
				profile.advanced = visuals.value("lightingAdvanced", profile.advanced);
				profile.water.CausticsStrength = visuals.value("causticsStrength", profile.water.CausticsStrength);
				profile.water.CausticsTiling = visuals.value("causticsTiling", profile.water.CausticsTiling);
				profile.water.CausticsSpeed = visuals.value("causticsSpeed", profile.water.CausticsSpeed);
				profile.water.CausticsDispersion = visuals.value("causticsDispersion", profile.water.CausticsDispersion);
				profile.water.ParallaxStrength = visuals.value("parallaxStrength", profile.water.ParallaxStrength);
				profile.water.ParallaxQuality = visuals.value("parallaxQuality", profile.water.ParallaxQuality);
				balance.settings.globalProfile = profile;
				balance.settings.useAmbientEffectLighting = visuals.value("useAmbientEffectLighting", balance.settings.useAmbientEffectLighting);
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_foliage_lighting_enabled") {
				globals::features::foliageLighting.SetEnabled(enabled);
				menu->RequestSettingsDirtyCheck();
			} else if (action == "set_terrain_variation_mesh_enabled") {
				if (enabled && !globals::features::terrainVariation.loaded)
					return { { "error", "Terrain Variation is not loaded" }, { "action", action } };
				globals::features::terrainVariation.SetMeshSupportEnabled(enabled);
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
			R"({"description":"set_depth_culling_traversal_diagnostics_enabled requires boolean enabled and selects extra Hybrid shader reason/work counters and nonblocking readback. Default false; also requires depth-culling telemetry enabled. Disable it for GPU profiler and frame-time comparisons. Reset telemetry after changing it; pending records across resets or toggles are excluded. depth_culling_snapshot and status expose hybrid.traversalDiagnostics with accepted-history eye reasons, object/work totals, submitted and missing/discarded readback batches, and setup availability. Diagnostic setup failure keeps normal Hybrid culling active and reports unavailable batches. Requires Skyrim VR. This instrumentation is compiled out without DEVBENCH_BRIDGE_ENABLED. Inspect and control the CSX VR menu, desktop/headset layout lock, independent exterior/interior depth-culling settings, Advanced/Legacy/Hybrid Hi-Z methods and recovery telemetry, Adaptive Balance and Foliage Lighting runtime state, TruePBR verbose JSON logging, dynamic cubemap resolution, and optional DLSS/DLAA RCAS and Luma Unsharp motion-adaptive sharpening. set_parallax_strength requires finite numeric strength in [0,2] and loaded Extended Materials. It stages live mesh and terrain parallax depth and self-shadow sampling, including TruePBR; 1 preserves authored depth, 0 disables depth and self-shadows while retaining terrain height blending. Water is independent. It does not enable material features or save settings. status reports parallaxStrength and extendedMaterialsLoaded. set_terrain_variation_mesh_enabled requires boolean enabled and stages mesh anti-tiling until settings are saved; enabling requires Terrain Variation to be loaded. status reports terrainVariationMeshEnabled and terrainVariationMeshActive. This control supports SE, AE and VR. set_adaptive_balance_enabled requires boolean enabled and stages the Adaptive Balance master toggle until settings are saved. Off bypasses all its Global, profile, and location lighting, color, Bloom, water, and wind response adjustments while preserving engine wind and independent renderer features. status reports adaptiveBalanceEnabled and adaptiveBalanceActive. set_motion_adaptive_sharpening applies signed strength adjustment above a threshold in output pixels per frame, capped on the 0-1 sharpness scale; it requires all four settings and stages them until settings are saved. The selected fixed sharpener is used without valid current motion; status reports the selected sharpener, last dispatch result and current applicability. FSR sharpening is unchanged. The screenshot action is obsolete and retained temporarily for migration; use communityshaders.screenshot contractMajor 1 instead. set_layout_unlocked enables desktop move, resize, and docking plus headset custom placement and grip dragging. Resolution changes are staged in memory; save settings and restart to apply them. set_depth_culling_settings requires a nonempty depthCulling object with exteriorEnabled, interiorEnabled, exteriorMinExtent and/or interiorMinExtent; extents must be finite numbers in [0,1000]. Unknown fields reject the whole update. Exterior and interior controls are independent. set_depth_culling_method requires method balanced (Advanced), legacy, or hybrid (experimental stereo Hi-Z). Method switches invalidate visibility history. set_depth_culling_legacy_mode selects Legacy when enabled and Advanced when disabled; the VR method selector is available at Info logging and the choice persists when settings are saved. status.depthCullingConfiguredPolicy and depthCullingTemporal.policy retain the historical machine value balanced for Advanced. All depth-culling settings setters require Skyrim VR and stage changes until settings are saved. Depth-culling telemetry controls jointly enable and reset Advanced and Hybrid counters and CPU timings without changing culling policy. A busy reset leaves both methods untouched. depth_culling_snapshot returns the same depthCullingTemporal diagnostics and observed frame without building unrelated menu status, enabling capture, or changing settings. Measurement windows identify their reset and starting culling epoch; reset after selecting a method, and reject mixed-epoch windows for A/B comparison. status.depthCullingTemporal.hybrid reports backend admission, cumulative fallback/history reason counts, source/resource observations, dispatch/allocation counters and accepted visibility results. submittedObjects counts queued GPU candidates; testedObjects counts CPU readback records examined, not an extra GPU query. status.depthCullingTemporal.nativeVisibility reports native CPU result counts before and after recovery, including empty and unreadable batches, separately from Hybrid readbacks. These gated scans use the existing CPU result array and add no GPU readback. Both observations share one telemetry writer so disabling or resetting cannot split a pair. Source phases and matrices are observations, not depth-content freshness proof. Missing source data and unmeasured timing means are null. Logical pyramid bytes describe uncompressed texels, not driver VRAM. CPU stage durations use nanoseconds with histograms; Hybrid cpuTimings.readback measures only post-native history validation, while nativeReadback measures the intercepted native call including any blocking Map. Prepare/dispatch CPU times include submission work, not completed GPU execution. CPU stage timings are inclusive and must not be summed across nested calls. Effective backend and last failure reasons remain available with telemetry disabled; detailed metrics stop accumulating after admitted writers finish. Counter totals are individually atomic live observations; for stable final totals, disable telemetry and wait for telemetryFrozen=true. Capture source observations before disabling. All depth-culling developer diagnostics and timing scopes are compiled out without DEVBENCH_BRIDGE_ENABLED. GPU timings use communityshaders.profiler captures and report self time; inspect activeGpu/hasGpu and sample counts rather than interpreting missing samples as zero cost. Actual method changes are logged at Info, and both method setters return method and persisted=false. prepare_coc is a one-shot pre-assay gate: it requires in-game Skyrim VR and startup-active VR FPS Stabilizer profile sync, then enables runtime-only developer mode and the fixed FOV plus TAA 0.3/0.7 fixture without saving settings. prepare_tuning applies the same runtime-only fixture in-game without requiring VR FPS Stabilizer profile sync. Neither preparation action changes cells. open_vr_fov opens the menu and queues navigation to the VR FOV tab, reveals that feature's Advanced settings, clears the feature search and expands its category without changing graphics settings. It requires loaded, enabled Skyrim VR settings. set_fov_enabled requires boolean enabled and uses the same VR FOV switch as the menu. Turning FOV on selects SSGI FOV and Screen Space Shadows FOV without enabling their parent effects; repeated enabled=true preserves individual choices. Turning FOV off keeps the child selections. Requires loaded VR Upscaling and DLSS/FSR when enabling. Does not save settings. status.fov and the setter response report selections and control availability. Every response identifies the exact producing DLL. expectedBuildId makes requests fail closed when the loaded binary is not the intended build. set_adaptive_balance_visuals stages a partial update to the Global Adaptive Balance profile using a nonempty visuals object; values must be finite and within schema bounds, and unknown fields reject the whole update. It does not enable Adaptive Balance or save settings. ambient and skySaturation are under Lighting and require that layer's detailed lighting controls; ambient scales the final vanilla or IBL ambient contribution once, independently of DALC matching; optional lightingAdvanced controls that existing switch. contrast and saturation belong to Color, are independent of lightingAdvanced and Linear Lighting, and grade the composed scene including bloom before fades. Both default to 1; contrast uses a hue-preserving luminance curve around middle gray, and saturation 0 makes the scene monochrome. The six caustics/parallax fields belong to Water. status.adaptiveBalanceVisuals reports configured Global values and effective composed values. Water Effects and water-parallax shader features must be loaded for their respective controls to affect rendering. Cloud brightness, saturation and gamma are independent of sky controls. fogIntensity scales vanilla distance-fog opacity. useAmbientEffectLighting (boolean, default false) is a global Adaptive Balance switch independent of lightingAdvanced; it replaces weather lighting on in-world effect meshes and sky statics with ambient/IBL plus shadowed directional light. effectBrightness and skyStaticBrightness scale the corresponding weather or ambient/directional lighting; 1 preserves brightness, and the existing Effects lighting multiplier is independent. The master/runtime and performance-measurement gates bypass the ambient switch while preserving its saved value. skyStaticTransparency fades sky-static effect meshes, and sunGlareIntensity scales glare already drawn; it does not restore missing glare or weather lens flares. These eight atmosphere fields require lightingAdvanced for the Global layer, support SE/AE/VR and default to neutral except migrated cloud values inherit saved sky adjustments. status.adaptiveBalanceWeatherColorsAvailable reports whether the weather-color hook installed; on failure those two weather brightness controls leave native colors unchanged. status.adaptiveBalanceVisuals.effective.cloudGamma reports the composed gamma, including the active Linear Lighting baseline and profile layers. set_fov_blend_curve requires boolean enabled and optional finite falloff in [0.5,2]. It stages the VR FOV Blend Curve checkbox and remembered exponent without enabling FOV or saving settings; omitted falloff preserves the saved value. Off or exponent 1 restores legacy feathering. Applies to both FOV-only and FOV + TAA, retaining mask geometry and transition width. Changes reset affected history. Requires loaded VR Upscaling; status.fov.blendCurve reports enabled, saved falloff, effectiveFalloff and applicability. set_sky_sync_sunlight requires boolean enabled for direct-light horizon dimming, with optional finite fadeHours in [0,1.5] game hours; omission preserves the current duration. Stages settings without saving and applies on the next sky update in SE/AE/VR. Requires loaded Sky Sync; its master Enable switch still gates the effect. status.skySyncSunlight reports availability, settings and the last applied brightness factor. Moon selection follows climate night boundaries regardless of dimming or visual sun-path offsets.","inputSchema":{"type":"object","properties":{"action":{"type":"string","description":"screenshot is obsolete; use communityshaders.screenshot contractMajor 1 action capture","enum":["status","depth_culling_snapshot","open","open_vr_fov","set_fov_enabled","set_fov_blend_curve","close","screenshot","set_path","set_layout_unlocked","texture_stats","set_depth_culling_settings","set_depth_culling_method","set_depth_culling_legacy_mode","set_depth_culling_traversal_diagnostics_enabled", "set_depth_culling_telemetry_enabled","reset_depth_culling_telemetry","set_adaptive_balance_enabled","set_adaptive_balance_visuals","set_foliage_lighting_enabled","set_terrain_variation_mesh_enabled","set_truepbr_verbose_json_logging","set_dynamic_cubemap_resolution","prepare_coc","prepare_tuning","set_motion_adaptive_sharpening","set_parallax_strength","set_sky_sync_sunlight"],"default":"status"},"method":{"type":"string","enum":["balanced","legacy","hybrid"],"description":"Depth-culling method required by set_depth_culling_method; balanced selects Advanced and hybrid selects experimental stereo Hi-Z."},"path":{"type":"string","enum":["auto","overlay","in_scene"]},"enabled":{"type":"boolean","description":"Boolean state required by a setter action."},"adjustment":{"type":"number","minimum":-1,"maximum":1,"description":"Signed RCAS or Luma Unsharp sharpness adjustment in motion."},"thresholdPixels":{"type":"number","minimum":0,"maximum":64,"description":"Motion threshold in output pixels per frame."},"strengthCap":{"type":"number","minimum":0,"maximum":1,"description":"Maximum adjusted strength on the DLSS sharpness slider scale."},"resolution":{"type":"integer","enum":[128,256],"description":"Dynamic cubemap resolution staged for the next game restart."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."},"depthCulling":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"exteriorEnabled":{"type":"boolean"},"interiorEnabled":{"type":"boolean"},"exteriorMinExtent":{"type":"number","minimum":0,"maximum":1000},"interiorMinExtent":{"type":"number","minimum":0,"maximum":1000}}},"visuals":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"useAmbientEffectLighting":{"type":"boolean","description":"Global ambient/IBL and directional effect lighting, default false; independent of lightingAdvanced."},"lightingAdvanced":{"type":"boolean"},"skySaturation":{"type":"number","minimum":0,"maximum":2},"contrast":{"type":"number","minimum":0.5,"maximum":2},"saturation":{"type":"number","minimum":0,"maximum":2},"ambient":{"type":"number","minimum":0,"maximum":5},"causticsStrength":{"type":"number","minimum":0,"maximum":2},"causticsTiling":{"type":"number","minimum":0.25,"maximum":4},"causticsSpeed":{"type":"number","minimum":0,"maximum":3},"causticsDispersion":{"type":"number","minimum":0,"maximum":2},"parallaxStrength":{"type":"number","minimum":0,"maximum":2},"parallaxQuality":{"type":"integer","minimum":4,"maximum":64},"cloudBrightness":{"type":"number","minimum":0,"maximum":2},"cloudSaturation":{"type":"number","minimum":0,"maximum":2},"cloudGammaOffset":{"type":"number","minimum":-1,"maximum":1},"fogIntensity":{"type":"number","minimum":0,"maximum":5},"sunGlareIntensity":{"type":"number","minimum":0,"maximum":5},"effectBrightness":{"type":"number","minimum":0,"maximum":2},"skyStaticBrightness":{"type":"number","minimum":0,"maximum":2},"skyStaticTransparency":{"type":"number","minimum":0,"maximum":1}}},"falloff":{"type":"number","minimum":0.5,"maximum":2,"description":"Optional FOV blend exponent; 1 is neutral. Omission preserves the saved value."},"strength":{"type":"number","minimum":0,"maximum":2,"description":"Global Extended Materials parallax depth; 1 is neutral, 0 disables depth and self-shadows. Water is separate."},"fadeHours":{"type":"number","minimum":0,"maximum":1.5,"description":"Optional Sky Sync direct-light recovery/fade duration on the night side of sunset/sunrise, in game hours; omission preserves the current value."}}}})";
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
