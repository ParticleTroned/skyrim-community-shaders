#include "MenuDevBenchBridge.h"
#include "Features/Upscaling/FoveatedBlendPolicy.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/AdaptiveBalanceGodraySettings.h"
#	include "Features/AdaptiveBrightness.h"
#	include "Features/DynamicCubemaps.h"
#	include "Features/ExtendedMaterials.h"
#	include "Features/FoliageLighting.h"
#	include "Features/GrassOptimizations.h"
#	include "Features/LinearLighting.h"
#	include "Features/ScreenSpaceGI.h"
#	include "Features/ScreenSpaceShadows.h"
#	include "Features/ScreenshotFeature.h"
#	include "Features/SkySync.h"
#	include "Features/TerrainVariation.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Features/VRDepthCullingTemporal.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "Menu/SettingsPage.h"
#	include "Menu/FeatureListRenderer.h"
#	include "Menu/ExternalSettingsPage.h"
#	include "Menu/PerformanceTuningRenderer.h"
#	include "MenuDevBenchPreflightPolicy.h"
#	include "MenuDepthCullingSettingsPolicy.h"
#	include "MenuDepthOfFieldSettingsPolicy.h"
#	include "State.h"
#	include "TruePBR.h"
#	include "Features/GrassLighting.h"

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

	json AdaptiveBalanceDepthOfFieldStatus()
	{
		const auto& balance = globals::features::adaptiveBrightness;
		const auto scene = AdaptiveBalanceDepthOfField::GetLiveSceneSettings();
		const auto underwater = AdaptiveBalanceDepthOfField::GetLiveUnderwaterSettings();
		return {
			{ "settings", balance.settings.depthOfField },
			{ "overridesActive", AdaptiveBalanceDepthOfField::IsRuntimeEnabled() },
			{ "correctionActive", AdaptiveBalanceDepthOfField::IsCorrectionEnabled() },
			{ "sceneDataAvailable", scene.has_value() },
			{ "underwaterDataAvailable", underwater.has_value() },
			{ "scene", scene ? json(*scene) : json(nullptr) },
			{ "underwater", underwater ? json(*underwater) : json(nullptr) }
		};
	}

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
				.neuralRenderingEnabled = settings.neuralRenderingEnabled,
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
			{ "neuralRenderingEnabled", state.neuralRenderingEnabled },
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
		if (a_snapshot.state.neuralRenderingEnabled)
			return "nr_must_be_disabled_for_taa_fixture";
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
				{ "ok", false },
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
			{ "ok", ready },
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

	double AdaptiveBalanceAppearanceBound(float value)
	{
		// Expose decimal slider endpoints without promoting float representation noise.
		return std::stod(std::to_string(value));
	}

	json BuildAdaptiveBalanceAppearanceSchema()
	{
		json properties = json::object();
		for (const auto& field : AdaptiveBalanceAppearance::kScalars) {
			properties[field.name] = {
				{ "type", "number" }, { "minimum", AdaptiveBalanceAppearanceBound(field.minimum) },
				{ "maximum", AdaptiveBalanceAppearanceBound(field.maximum) }, { "default", field.neutral },
				{ "description", field.label }
			};
		}
		for (const auto& field : AdaptiveBalanceAppearance::kTints) {
			properties[field.name] = {
				{ "type", "array" }, { "minItems", 3 }, { "maxItems", 3 }, { "default", { 1.0f, 1.0f, 1.0f } },
				{ "items", { { "type", "number" }, { "minimum", 0 }, { "maximum", AdaptiveBalanceAppearanceBound(field.maximum) } } },
				{ "description", field.label }
			};
		}
		return {
			{ "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false },
			{ "description", "Partial Global appearance update. Requires Global lightingAdvanced; omitted fields retain their saved values. RGB tints are three finite numeric channels." },
			{ "properties", std::move(properties) }
		};
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
		static constexpr std::array<Field, 24> fields{ { { "skySaturation", 0.0, 2.0 },
			{ "godrayFinalBrightness", AdaptiveBalanceGodray::kMin, AdaptiveBalanceGodray::kMax },
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
			{ "pointLightSaturation", 0.0, 2.0 },
			{ "pointLightCurve", 0.1, 4.0 },
			{ "fireIntensity", 0.0, 5.0 },
			{ "fireSaturation", 0.0, 2.0 },
			{ "fireCurve", 0.25, 4.0 },
			{ "ambient", 0.0, 5.0 },
			{ "causticsStrength", 0.0, 2.0 },
			{ "causticsTiling", 0.25, 4.0 },
			{ "causticsSpeed", 0.0, 3.0 },
			{ "causticsDispersion", 0.0, 2.0 },
			{ "parallaxStrength", 0.0, 2.0 },
			{ "parallaxQuality", 4.0, 64.0 } } };
		for (const auto& [name, value] : a_visuals.items()) {
			if (name == "appearance") {
				if (!value.is_object() || value.empty())
					return "appearance must be a nonempty object";
				for (const auto& [appearanceName, appearanceValue] : value.items()) {
					const auto scalar = std::find_if(AdaptiveBalanceAppearance::kScalars.begin(), AdaptiveBalanceAppearance::kScalars.end(),
						[&](const auto& entry) { return appearanceName == entry.name; });
					if (scalar != AdaptiveBalanceAppearance::kScalars.end()) {
						if (!appearanceValue.is_number())
							return "Non-numeric appearance field: " + appearanceName;
						const double number = appearanceValue.get<double>();
						if (!std::isfinite(number) || number < AdaptiveBalanceAppearanceBound(scalar->minimum) || number > AdaptiveBalanceAppearanceBound(scalar->maximum))
							return "Out-of-range or invalid appearance field: " + appearanceName;
						continue;
					}
					const auto tint = std::find_if(AdaptiveBalanceAppearance::kTints.begin(), AdaptiveBalanceAppearance::kTints.end(),
						[&](const auto& entry) { return appearanceName == entry.name; });
					if (tint == AdaptiveBalanceAppearance::kTints.end() || !appearanceValue.is_array() || appearanceValue.size() != 3)
						return "Unknown or invalid appearance tint: " + appearanceName;
					for (const auto& channel : appearanceValue) {
						if (!channel.is_number())
							return "Non-numeric appearance tint channel: " + appearanceName;
						const double number = channel.get<double>();
						if (!std::isfinite(number) || number < 0.0 || number > AdaptiveBalanceAppearanceBound(tint->maximum))
							return "Out-of-range or invalid appearance tint channel: " + appearanceName;
					}
				}
				continue;
			}
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
		configured["pointLightSaturation"] = profile.pointLightSaturation;
		configured["pointLightCurve"] = profile.pointLightCurve;
		configured["fireIntensity"] = profile.fireIntensity;
		configured["fireSaturation"] = profile.fireSaturation;
		configured["fireCurve"] = profile.fireCurve;
		configured["ambient"] = profile.ambientMult;
		configured["appearance"] = profile.appearance;
		configured["lightingAdvanced"] = profile.advanced;
		configured["useAmbientEffectLighting"] = balance.settings.useAmbientEffectLighting;
		configured["godrayFinalBrightness"] = balance.settings.godrayFinalBrightness;
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
		effective["pointLightSaturation"] = lighting.pointLightSaturation;
		effective["pointLightCurve"] = lighting.pointLightCurve;
		effective["fireIntensity"] = lighting.fireIntensity;
		effective["fireSaturation"] = lighting.fireSaturation;
		effective["fireCurve"] = lighting.fireCurve;
		effective["ambient"] = lighting.ambientMult;
		effective["appearance"] = lighting.appearance;
		effective["useAmbientEffectLighting"] = balance.IsRuntimeEnabled() && balance.settings.useAmbientEffectLighting;
		effective["godrayFinalBrightness"] = balance.GetEffectiveGodrayFinalBrightness();
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

	std::string ValidateWandSettings(const json& a_update)
	{
		if (!a_update.is_object() || a_update.empty())
			return "wand must be a nonempty object";
		for (const auto& [name, value] : a_update.items()) {
			if (name == "enableWandPointing") {
				if (!value.is_boolean())
					return "enableWandPointing must be boolean";
			} else if (name == "aimPitchTrimDegrees") {
				if (!value.is_number())
					return "aimPitchTrimDegrees must be a finite number in [-90,90]";
				const double pitch = value.get<double>();
				if (!std::isfinite(pitch) || pitch < VR::Config::kMinWandAimPitchTrimDegrees ||
					pitch > VR::Config::kMaxWandAimPitchTrimDegrees)
					return "aimPitchTrimDegrees must be a finite number in [-90,90]";
			} else {
				return "Unknown wand field: " + name;
			}
		}
		return {};
	}

	json WandSettingsStatus()
	{
		const auto& vr = globals::features::vr;
		json hands = json::array();
		for (std::size_t index = 0; index < vr.wandHandStates.size(); ++index) {
			const auto& hand = vr.wandHandStates[index];
			hands.push_back({
				{ "controller", index == 0 ? "primary" : "secondary" },
				{ "physicalHand", (index == 0 ? vr.lastKnownLeftHandedMode : !vr.lastKnownLeftHandedMode) ? "left" : "right" },
				{ "controllerIndex", hand.controllerIndex },
				{ "poseValid", hand.poseValid },
				{ "poseSource", hand.poseValid ? json(hand.usingOCUPointer ? "ocu_shared_pointer" : hand.usingAimComponent ? "render_model_tip" :
																															 "raw_controller") :
												 json(nullptr) },
				{ "intersecting", hand.isIntersecting },
				{ "moved", hand.moved },
				{ "usingOCUPointer", hand.usingOCUPointer },
				{ "ocuOwnsInput", hand.ocuOwnsInput },
				{ "ocuTriggerDown", hand.ocuTriggerDown },
				{ "ocuTriggerEligible", hand.ocuTriggerEligible },
				{ "ocuTriggerSequence", hand.ocuTriggerSequence },
				{ "ocuOwnershipGeneration", hand.ocuOwnershipGeneration },
				{ "usingPresentedSurface", hand.usingPresentedSurface },
				{ "hasScreenPosition", hand.hasScreenPosition },
				{ "screenPosition", hand.hasScreenPosition ? json::array({ hand.screenPosition.x, hand.screenPosition.y }) : json(nullptr) },
				{ "uv", hand.isIntersecting ? json::array({ hand.uvCoordinates.x, hand.uvCoordinates.y }) : json(nullptr) },
				{ "rayOrigin", hand.poseValid ? json::array({ hand.rayOrigin.x, hand.rayOrigin.y, hand.rayOrigin.z }) : json(nullptr) },
				{ "rayDirection", hand.poseValid ? json::array({ hand.rayDirection.x, hand.rayDirection.y, hand.rayDirection.z }) : json(nullptr) },
			});
		}
		const auto controllerName = [](ControllerDevice a_controller) {
			return a_controller == ControllerDevice::Primary   ? "primary" :
			       a_controller == ControllerDevice::Secondary ? "secondary" :
			                                                     "none";
		};
		const auto beamColor = vr.GetWandPointerColor();
		const auto dotColor = vr.GetWandPointerDotColor();
		return {
			{ "available", globals::game::isVR && vr.IsOpenVRCompatible() },
			{ "enableWandPointing", vr.settings.EnableWandPointing },
			{ "useRuntimeDefaultMenuNavigation", vr.settings.UseRuntimeDefaultMenuNavigation },
			{ "effectiveWandPointing", globals::game::isVR && vr.CanUseWandPointing() },
			{ "aimPitchTrimDegrees", vr.settings.WandAimPitchTrimDegrees },
			{ "ocuPointerAvailable", vr.ocuPointerAvailable },
			{ "ocuPointerValid", vr.ocuPointerValid },
			{ "ocuPointerGeneration", vr.ocuPointerGeneration },
			{ "ocuPointerFrameId", vr.ocuPointerFrameId },
			{ "ocuPointerOwnedHands", vr.ocuPointerOwnedHands },
			{ "pointerColorSpace", "srgb_straight_alpha" },
			{ "beamColor", json::array({ beamColor.x, beamColor.y, beamColor.z, beamColor.w }) },
			{ "dotColor", json::array({ dotColor.x, dotColor.y, dotColor.z, dotColor.w }) },
			{ "activeController", controllerName(vr.activeWandController) },
			{ "capturedController", controllerName(vr.capturedWandController) },
			{ "intersecting", vr.wandState.isIntersecting },
			{ "activelyDrivingCursor", vr.wandState.isActivelyDrivingCursor },
			{ "hands", std::move(hands) },
		};
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
			{ "fov", FovSettingsStatus() },
			{ "wand", WandSettingsStatus() },
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
			{ "adaptiveBalanceDepthOfField", AdaptiveBalanceDepthOfFieldStatus() },
			{ "skySyncSunlight", { { "loaded", globals::features::skySync.loaded }, { "skySyncEnabled", globals::features::skySync.settings.Enabled }, { "dimmingEnabled", globals::features::skySync.settings.DimSunlightUnderHorizon }, { "fadeHours", globals::features::skySync.settings.HorizonFadeHours }, { "appliedFactor", globals::features::skySync.GetSunlightDimmingFactor() } } },
			{ "foliageLightingEnabled", globals::features::foliageLighting.IsEnabled() },
			{ "grassOptimizations", globals::features::grassOptimizations.GetDiagnostics() },
			{ "motionAdaptiveSharpening", MotionSharpeningStatus() },
			{ "foliageLightingActive", globals::features::foliageLighting.IsRuntimeEnabled() },
			{ "terrainVariationMeshEnabled", globals::features::terrainVariation.settings.enableMeshSupport != 0 },
			{ "terrainVariationMeshActive", globals::features::terrainVariation.IsMeshSupportEnabled() },
			{ "parallaxStrength", globals::features::extendedMaterials.settings.ParallaxStrength },
			{ "extendedMaterialsLoaded", globals::features::extendedMaterials.loaded },
			{ "pbrGrass", { { "requested", globals::features::truePBR.settings.GrassEnabled != 0 }, { "lightingAvailable", globals::features::truePBR.IsPBRGrassEnabled() }, { "truePbrLoaded", globals::features::truePBR.loaded }, { "truePbrEnabled", globals::features::truePBR.settings.Enabled != 0 }, { "grassLightingLoaded", globals::features::grassLighting.loaded }, { "grassLightingEnabled", globals::features::grassLighting.settings.Enabled != 0 }, { "diagnostics", globals::features::truePBR.GetGrassDiagnostics() } } },
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
		if (action == "set_adaptive_balance_dof") {
			const auto patch = a_args.value("depthOfField", json{});
			std::string error;
			if (!MenuDepthOfFieldSettingsPolicy::Validate(patch, error))
				return { { "error", error }, { "action", action } };
			return RunOnMainThread([patch, action]() -> json {
				if (PerformanceTuningRenderer::HasActiveMeasurements())
					return { { "error", "Finish or cancel the performance measurement before changing depth of field" }, { "action", action } };
				auto& balance = globals::features::adaptiveBrightness;
				if (!balance.loaded)
					return { { "error", "Adaptive Balance is not loaded" }, { "action", action } };
				if (!globals::menu)
					return { { "error", "CSX menu unavailable" }, { "action", action } };
				std::string error;
				if (!MenuDepthOfFieldSettingsPolicy::Apply(patch, balance.settings.depthOfField, error))
					return { { "error", error }, { "action", action } };
				globals::menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "depthOfField", AdaptiveBalanceDepthOfFieldStatus() } };
			});
		}
		if (action == "restore_defaults") {
			return RunOnMainThread([]() -> json {
				std::string error;
				if (!globals::state || !globals::state->RestoreDefaultSettings(error))
					return { { "error", error.empty() ? "Settings are unavailable" : error } };
				return { { "action", "restore_defaults" }, { "saved", false } };
			});
		}
		if (action == "set_sidebar_visible") {
			if (!a_args.contains("visible") || !a_args["visible"].is_boolean())
				return { { "error", "visible must be a boolean" } };
			return RunOnMainThread([visible = a_args["visible"].get<bool>()]() -> json {
				FeatureListRenderer::SetSidebarVisible(visible);
				return { { "action", "set_sidebar_visible" }, { "sidebarVisible", FeatureListRenderer::IsSidebarVisible() } };
			});
		}
		if (action == "ui_status" || action == "open_feature" || action == "select_tab" || action == "scroll") {
			if ((a_args.contains("feature") && !a_args["feature"].is_string()) || (action != "set_sidebar_visible" && action != "ui_status" && !a_args.contains("feature")))
				return { { "ok", false }, { "error", "feature must be a feature short name or observed page ID" } };
			if ((a_args.contains("tab") && !a_args["tab"].is_string()) || ((action == "select_tab" || action == "scroll") && !a_args.contains("tab")))
				return { { "ok", false }, { "error", "tab must be an available tab ID from ui_status" } };
			if (action == "scroll" && (!a_args.contains("scrollRatio") || !a_args["scrollRatio"].is_number()))
				return { { "ok", false }, { "error", "scroll requires numeric scrollRatio in [0,1]" } };
			const auto ratio = action == "scroll" ? a_args["scrollRatio"].get<double>() : 0.0;
			if (!std::isfinite(ratio) || ratio < 0 || ratio > 1)
				return { { "ok", false }, { "error", "scrollRatio must be finite and in [0,1]" } };
			const auto featureName = a_args.value("feature", std::string{});
			const auto tab = a_args.value("tab", std::string("overview"));
			return RunOnMainThread([action, featureName, tab, ratio]() -> json {
				if (action != "ui_status" && PerformanceTuningRenderer::HasActiveMeasurements())
					return { { "ok", false }, { "error", "Finish or cancel the performance comparison before navigating." } };
				if (action != "ui_status" && ImGui::GetCurrentContext() &&
					ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
					return { { "ok", false }, { "error", "Finish or cancel the open dialog before navigating." } };
				if (!globals::menu)
					return { { "ok", false }, { "error", "CSX menu unavailable" } };
				if (action == "scroll" && (!globals::menu->IsEnabled || !MenuUI::DevBenchViewport::Scroll(featureName, tab, ratio)))
					return { { "ok", false }, { "error", "Open the target page and tab, then query ui_status for a fresh viewport with no pending scroll." } };
				if (action == "open_feature") {
					const auto features = FeatureListRenderer::GetMenuFeatures();
					const auto found = std::ranges::find_if(features, [&](Feature* feature) { return feature->GetShortName() == featureName; });
					const auto builtIn = FeatureListRenderer::GetBuiltInPageMenuEntryId(featureName);
					if (builtIn.empty() && (found == features.end() || !(*found)->IsInMenu() || (*found)->IsHiddenFromUserView() ||
											   (!dynamic_cast<MenuUI::ExternalSettingsPage*>(*found) && (!(*found)->loaded || globals::state->IsFeatureDisabled(featureName)))))
						return { { "ok", false }, { "error", "Unknown or unavailable feature" } };
					if (!MenuUI::SettingsPage::Select(featureName.c_str(), "overview"))
						return { { "ok", false }, { "error", "Save or discard the unsaved INI draft before opening this overview." } };
					globals::menu->SelectFeatureMenu(builtIn.empty() ? featureName : builtIn);
					globals::menu->OpenMenu();
				} else if (action == "select_tab" && !MenuUI::SettingsPage::Navigate(featureName.c_str(), tab.c_str())) {
					return { { "ok", false }, { "error", "Unavailable or disabled tab, or unsaved INI draft. Enable profiling on the main Profiling page for feature timing views; save or discard INI drafts. Query ui_status for tab availability." } };
				}
				return { { "ok", true }, { "action", action }, { "menuOpen", globals::menu->IsEnabled }, { "viewport", MenuUI::DevBenchViewport::Describe(globals::menu->IsEnabled) }, { "pages", MenuUI::SettingsPage::Describe() }, { "unsaved", globals::menu->HasUnsavedSettings() }, { "sidebarVisible", FeatureListRenderer::IsSidebarVisible() } };
			});
		}

		if (action != "status" && action != "open" && action != "open_vr_fov" && action != "set_fov_enabled" && action != "set_fov_blend_curve" && action != "close" && action != "screenshot" && action != "set_path" && action != "set_layout_unlocked" && action != "set_wand_settings" && action != "texture_stats" && action != "set_depth_culling_settings" && action != "set_depth_culling_legacy_mode" && action != "set_depth_culling_telemetry_enabled" && action != "reset_depth_culling_telemetry" && action != "set_grass_optimizations_enabled" && action != "set_grass_optimizations_settings" && action != "set_grass_hiz_enabled" && action != "set_grass_optimizations_diagnostics_enabled" && action != "set_adaptive_balance_enabled" && action != "set_adaptive_balance_visuals" && action != "set_adaptive_balance_dof" && action != "set_foliage_lighting_enabled" && action != "set_terrain_variation_mesh_enabled" && action != "set_pbr_grass_enabled" && action != "set_pbr_grass_diagnostics_enabled" && action != "set_truepbr_verbose_json_logging" && action != "set_dynamic_cubemap_resolution" && action != "prepare_coc" && action != "prepare_tuning" && action != "set_motion_adaptive_sharpening" && action != "set_parallax_strength" && action != "set_sky_sync_sunlight" && action != "ui_status" && action != "open_feature" && action != "select_tab" && action != "scroll" && action != "restore_defaults") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "open", "open_vr_fov", "set_fov_enabled", "set_fov_blend_curve", "close", "screenshot", "set_path", "set_layout_unlocked", "set_wand_settings", "texture_stats", "set_depth_culling_settings", "set_depth_culling_legacy_mode", "set_depth_culling_telemetry_enabled", "reset_depth_culling_telemetry", "set_adaptive_balance_enabled", "set_adaptive_balance_visuals", "set_adaptive_balance_dof", "set_foliage_lighting_enabled", "set_terrain_variation_mesh_enabled", "set_pbr_grass_enabled", "set_pbr_grass_diagnostics_enabled", "set_truepbr_verbose_json_logging", "set_dynamic_cubemap_resolution", "prepare_coc", "prepare_tuning", "set_motion_adaptive_sharpening", "set_parallax_strength", "set_sky_sync_sunlight", "set_grass_optimizations_enabled", "set_grass_optimizations_settings", "set_grass_hiz_enabled", "set_grass_optimizations_diagnostics_enabled", "ui_status", "open_feature", "select_tab", "set_sidebar_visible", "scroll", "restore_defaults" }) },
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
		if ((action == "set_fov_enabled" || action == "set_layout_unlocked" || action == "set_depth_culling_legacy_mode" || action == "set_depth_culling_telemetry_enabled" || action == "set_adaptive_balance_enabled" || action == "set_grass_optimizations_enabled" || action == "set_grass_hiz_enabled" || action == "set_grass_optimizations_diagnostics_enabled" || action == "set_foliage_lighting_enabled" || action == "set_terrain_variation_mesh_enabled" || action == "set_pbr_grass_enabled" || action == "set_pbr_grass_diagnostics_enabled" || action == "set_truepbr_verbose_json_logging" || action == "set_sky_sync_sunlight") &&
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
		json wandUpdate = json::object();
		if (action == "set_wand_settings") {
			wandUpdate = a_args.value("wand", json::object());
			if (const auto error = ValidateWandSettings(wandUpdate); !error.empty())
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
		const json grassUpdate = a_args.value("grassOptimizations", json{});
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

		return RunOnMainThread([action, path, enabled, resolution, motionSharpening, visuals, depthCulling, wandUpdate, grassUpdate, hasFalloff, falloff, parallaxStrength, hasHorizonFadeHours, horizonFadeHours]() -> json {
			if (action == "set_wand_settings" && !globals::game::isVR)
				return { { "error", "Wand settings require Skyrim VR" }, { "errorCode", "skyrim_vr_required" }, { "action", action } };
			if ((action == "set_depth_culling_settings" || action == "set_depth_culling_legacy_mode") && !globals::game::isVR)
				return { { "error", "Depth-culling settings require Skyrim VR" }, { "errorCode", "skyrim_vr_required" }, { "action", action } };
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
			} else if (action == "set_wand_settings") {
				auto& vr = globals::features::vr;
				const bool selectNavigation = wandUpdate.contains("enableWandPointing");
				const bool wandEnabled = wandUpdate.value("enableWandPointing", vr.settings.EnableWandPointing);
				const float pitch = wandUpdate.value("aimPitchTrimDegrees", vr.settings.WandAimPitchTrimDegrees);
				const bool changed = wandEnabled != vr.settings.EnableWandPointing ||
				                     pitch != vr.settings.WandAimPitchTrimDegrees ||
				                     (selectNavigation && vr.settings.UseRuntimeDefaultMenuNavigation);
				if (changed) {
					vr.settings.EnableWandPointing = wandEnabled;
					vr.settings.WandAimPitchTrimDegrees = pitch;
					if (selectNavigation)
						vr.settings.UseRuntimeDefaultMenuNavigation = false;
					vr.ResetMenuInputRuntimeState();
					menu->RequestSettingsDirtyCheck();
				}
				return { { "action", action }, { "changed", changed }, { "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_depth_culling_settings") {
				MenuDepthCullingSettingsPolicy::Apply(depthCulling, globals::features::vr.settings);
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
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
				profile.pointLightSaturation = visuals.value("pointLightSaturation", profile.pointLightSaturation);
				profile.pointLightCurve = visuals.value("pointLightCurve", profile.pointLightCurve);
				profile.fireIntensity = visuals.value("fireIntensity", profile.fireIntensity);
				profile.fireSaturation = visuals.value("fireSaturation", profile.fireSaturation);
				profile.fireCurve = visuals.value("fireCurve", profile.fireCurve);
				profile.ambientMult = visuals.value("ambient", profile.ambientMult);
				profile.advanced = visuals.value("lightingAdvanced", profile.advanced);
				profile.water.CausticsStrength = visuals.value("causticsStrength", profile.water.CausticsStrength);
				profile.water.CausticsTiling = visuals.value("causticsTiling", profile.water.CausticsTiling);
				profile.water.CausticsSpeed = visuals.value("causticsSpeed", profile.water.CausticsSpeed);
				profile.water.CausticsDispersion = visuals.value("causticsDispersion", profile.water.CausticsDispersion);
				profile.water.ParallaxStrength = visuals.value("parallaxStrength", profile.water.ParallaxStrength);
				profile.water.ParallaxQuality = visuals.value("parallaxQuality", profile.water.ParallaxQuality);
				if (visuals.contains("appearance")) {
					json appearance = profile.appearance;
					appearance.merge_patch(visuals.at("appearance"));
					profile.appearance = appearance.get<AdaptiveBalanceAppearanceSettings>();
				}
				balance.settings.globalProfile = profile;
				balance.settings.useAmbientEffectLighting = visuals.value("useAmbientEffectLighting", balance.settings.useAmbientEffectLighting);
				balance.settings.godrayFinalBrightness = visuals.value("godrayFinalBrightness", balance.settings.godrayFinalBrightness);
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_grass_optimizations_settings" || action == "set_grass_hiz_enabled") {
				auto& grass = globals::features::grassOptimizations;
				if (!grass.loaded || !grass.IsHookInstalled())
					return { { "error", "Grass optimizations are unavailable" }, { "action", action } };
				std::string error;
				const json update = action == "set_grass_hiz_enabled" ? json{ { "EnableOcclusionCulling", enabled } } : grassUpdate;
				if (!grass.UpdateSettings(update, error))
					return { { "error", error }, { "action", action } };
				menu->RequestSettingsDirtyCheck();
				return { { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			} else if (action == "set_grass_optimizations_enabled" || action == "set_grass_optimizations_diagnostics_enabled") {
				auto& grass = globals::features::grassOptimizations;
				if (enabled && (!grass.loaded || !grass.IsHookInstalled()))
					return { { "error", "Grass draw batching is unavailable" }, { "action", action } };
				if (action == "set_grass_optimizations_enabled") {
					grass.SetEnabled(enabled);
					menu->RequestSettingsDirtyCheck();
				} else {
					grass.SetDiagnosticsEnabled(enabled);
				}
			} else if (action == "set_foliage_lighting_enabled") {
				globals::features::foliageLighting.SetEnabled(enabled);
				menu->RequestSettingsDirtyCheck();
			} else if (action == "set_terrain_variation_mesh_enabled") {
				if (enabled && !globals::features::terrainVariation.loaded)
					return { { "error", "Terrain Variation is not loaded" }, { "action", action } };
				globals::features::terrainVariation.SetMeshSupportEnabled(enabled);
				menu->RequestSettingsDirtyCheck();
			} else if (action == "set_pbr_grass_enabled") {
				auto& pbr = globals::features::truePBR;
				if (enabled && (!pbr.loaded || !globals::features::grassLighting.loaded))
					return { { "error", "PBR grass requires loaded True PBR and Grass Lighting" }, { "action", action } };
				pbr.settings.GrassEnabled = enabled ? 1u : 0u;
				menu->RequestSettingsDirtyCheck();
			} else if (action == "set_pbr_grass_diagnostics_enabled") {
				globals::features::truePBR.SetGrassDiagnosticsEnabled(enabled);
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

		static constexpr const char* baseDescriptor =
			R"({"description":"set_path selects auto, overlay, or in_scene. auto uses IVROverlay on SteamVR from startup, and in-scene for OpenComposite or when IVROverlay is unavailable. set_wand_settings requires a nonempty wand object with optional enableWandPointing and aimPitchTrimDegrees (-90 to 90 degrees). Explicit enableWandPointing selects manual navigation; pitch alone preserves navigation mode. This VR-only action stages settings without saving, validates every field before mutation and releases held menu input only when settings change. status.wand reports saved and effective navigation, pitch, controller ownership and each hand pose/intersection. OCU sharing is selected automatically when its pointer API is available. ocuPointerAvailable identifies the API; ocuPointerValid identifies a fresh accepted snapshot. ocuPointerGeneration, ocuPointerFrameId and ocuPointerOwnedHands identify the snapshot and physical hand lease mask (bit 0 left, bit 1 right). Each hand reports physicalHand, usingOCUPointer, ocuOwnsInput, ownership-gated ocuTriggerDown, release-gated ocuTriggerEligible and physical ocuTriggerSequence and per-hand ocuOwnershipGeneration. beamColor and dotColor report the current idle/pressed straight-alpha sRGB palette shared with OCU; standalone pointers use the matching warm-idle and blue-pressed defaults. poseSource ocu_shared_pointer applies aimPitchTrimDegrees once in OCU after its calibration, then shares the smoothed ray without a second CSX trim or cursor filter. Invalid shared samples suppress wand interaction until recovery. poseSource render_model_tip is the runtime render-model component; raw_controller is its fallback when no shared API is available. set_sidebar_visible requires boolean visible and shows or hides the shared feature sidebar for this menu session without changing feature selection, column widths or saved settings. It overrides hover auto-hide until its preference is changed. ui_status reports sidebarVisible. Inspect and control the CSX VR menu, desktop/headset layout lock, independent exterior/interior depth-culling settings, Advanced/Legacy policy and recovery telemetry, Adaptive Balance and Foliage Lighting runtime state, TruePBR verbose JSON logging, dynamic cubemap resolution, and optional DLSS/DLAA RCAS and Luma Unsharp motion-adaptive sharpening. set_pbr_grass_enabled requires boolean enabled and stages authored PBR grass shading without saving settings or changing grass density, culling or deformation. Default false; rendering also requires enabled True PBR and Grass Lighting and a compiled grass shader pair. Uses the existing shader permutations, with no compilation triggered by this toggle. Basic grass remains available when disabled; authored PBR textures always use their full alpha texture. set_pbr_grass_diagnostics_enabled requires boolean enabled and toggles cumulative material-setup counters only. They count material binds, not instances or completed GPU draws; enabling does not reset them. Diagnostics also enable profiler scopes Grass::PBRRequested and Grass::Legacy; the former measures requested shading and may include a shader fallback. Use communityshaders.profiler with these scopes for a separate diagnostic capture and whole-frame timings with diagnostics off for performance windows. status.pbrGrass reports configuration, lighting availability and counters; availability is not proof of a completed PBR draw. These controls support SE, AE and VR. set_parallax_strength requires finite numeric strength in [0,2] and loaded Extended Materials. It stages live mesh and terrain parallax depth and self-shadow sampling, including TruePBR; 1 preserves authored depth, 0 disables depth and self-shadows while retaining terrain height blending. Water is independent. It does not enable material features or save settings. status reports parallaxStrength and extendedMaterialsLoaded. set_terrain_variation_mesh_enabled requires boolean enabled and stages mesh anti-tiling until settings are saved; enabling requires Terrain Variation to be loaded. status reports terrainVariationMeshEnabled and terrainVariationMeshActive. This control supports SE, AE and VR. set_adaptive_balance_dof stages a partial depthOfField object without saving or enabling Adaptive Balance. enabled gates manual overrides only; fixUnderwaterFogDofBlur is independent. The Adaptive Balance master gates both. sceneDof and underwaterDof accept locked, values and baseline objects; mode is 0-3, blurRadius 0-7, depth and range 0-50000, autofocus depth and range 0-10000, and blur strengths 0-1. Unknown fields, nulls, wrong types, non-finite values and out-of-range values reject the entire patch. Active performance measurements block mutation. status.adaptiveBalanceDepthOfField reports saved settings, active gates, live scene/underwater values and separate data availability. Underwater rendering uses Back mode with autofocus off; other stored values remain available for compatibility. These global settings do not inherit from profiles or locations. set_adaptive_balance_enabled requires boolean enabled and stages the Adaptive Balance master toggle until settings are saved. Off bypasses all its Global, profile, and location lighting, color, Bloom, water, wind response, and global depth-of-field adjustments while preserving engine wind and independent renderer features. status reports adaptiveBalanceEnabled and adaptiveBalanceActive. set_motion_adaptive_sharpening applies signed strength adjustment above a threshold in output pixels per frame, capped on the 0-1 sharpness scale; it requires all four settings and stages them until settings are saved. The selected fixed sharpener is used without valid current motion; status reports the selected sharpener, last dispatch result and current applicability. FSR sharpening is unchanged. The screenshot action is obsolete and retained temporarily for migration; use communityshaders.screenshot contractMajor 1 instead. set_layout_unlocked enables desktop move, resize, and docking plus headset custom placement and grip dragging. Resolution changes are staged in memory; save settings and restart to apply them. set_depth_culling_settings requires a nonempty depthCulling object with exteriorEnabled, interiorEnabled, exteriorMinExtent and/or interiorMinExtent; extents must be finite numbers in [0,1000]. Unknown fields reject the whole update. Exterior and interior controls are independent. set_depth_culling_legacy_mode selects Legacy when enabled and Advanced when disabled; this choice persists after leaving Debug mode when settings are saved. status.depthCullingConfiguredPolicy and depthCullingTemporal.policy retain the historical machine value balanced for Advanced. Both setters require Skyrim VR and stage changes until settings are saved. The Legacy setter response retains enabled, method (balanced or legacy), persisted=false and status. Depth-culling telemetry controls affect measurements only, never culling policy. prepare_coc is a one-shot pre-assay gate: it requires in-game Skyrim VR and startup-active VR FPS Stabilizer profile sync, then enables runtime-only developer mode and the fixed FOV plus TAA 0.3/0.7 fixture without saving settings. prepare_tuning applies the same runtime-only fixture in-game without requiring VR FPS Stabilizer profile sync. prepare_coc and prepare_tuning reject enabled NR with nr_must_be_disabled_for_taa_fixture before any mutation because their fixed FOV + TAA fixture is incompatible. Both preparation actions return explicit ok: true only when their postconditions are ready, otherwise false; applied and changed separately report mutation. Neither preparation action changes cells. open_vr_fov opens the menu and queues navigation to the VR FOV tab, reveals that feature's Advanced settings, clears the feature search and expands its category without changing graphics settings. It requires loaded, enabled Skyrim VR settings. set_fov_enabled requires boolean enabled and uses the same VR FOV switch as the menu. Turning FOV on selects SSGI FOV and Screen Space Shadows FOV without enabling their parent effects; repeated enabled=true preserves individual choices. Turning FOV off keeps the child selections. Requires loaded VR Upscaling and DLSS/FSR when enabling. Does not save settings. status.fov and the setter response report selections and control availability. Every response identifies the exact producing DLL. expectedBuildId makes requests fail closed when the loaded binary is not the intended build. set_adaptive_balance_visuals also accepts global-only visuals.godrayFinalBrightness, a finite final godray brightness multiplier in [0,5], default 1. It scales the completed godray colour after the existing gamma conversion, independently of Global lightingAdvanced, profile godrayIntensity and Linear Lighting. The Adaptive Balance master/runtime gate bypasses it to 1. It does not enable the godray renderer. status.adaptiveBalanceVisuals.global.godrayFinalBrightness reports the saved value and effective.godrayFinalBrightness reports the gate-adjusted value. Legacy vlIntensity maps to this setting. set_adaptive_balance_visuals accepts an optional nested visuals.appearance object for directional saturation/colour curve/tint, ambient saturation, fog colour brightness/tint, cloud opacity/tint, sky gradient intensity/curve/tint, stars grading, sky-static mist colour, non-fire particle and additive glow-sprite appearance, particle lighting influence, and godray/cloud-shadow profile adjustments. Appearance fields require Global lightingAdvanced and compose with active day/night and location layers. Neutral multipliers/curves are 1 and tint multipliers are white. godrayTint is an RGB replacement colour with godrayTintAmount default 0; it overlays existing standalone godray custom colour without changing saved Volumetric Lighting settings. Godray intensity/opacity/saturation and cloudShadowStrength multiply standalone settings within their renderer bounds. Disabled/unloaded parent features remain disabled; godray tuning requires ImageSpace pixel replacement. Particle/glow-sprite classification follows shader material flags, can include similarly authored effects, and does not change emitted point lights or radius. Existing fire controls remain separate. status.adaptiveBalanceVisuals.global.appearance and effective.appearance report saved Global and composed Adaptive Balance adjustments, before standalone godray/cloud-shadow composition. All values must be finite, RGB arrays exactly three numeric channels, and unknown nested fields reject the whole update. set_adaptive_balance_visuals stages a partial update to Global Adaptive Balance controls using a nonempty visuals object; values must be finite and within schema bounds, and unknown fields reject the whole update. It does not enable Adaptive Balance or save settings. ambient, skySaturation, pointLightSaturation, pointLightCurve, fireIntensity, fireSaturation and fireCurve are under Lighting and require that layer's detailed lighting controls; ambient scales the final vanilla or IBL ambient contribution once, independently of DALC matching; optional lightingAdvanced controls that existing switch. pointLightCurve defaults to 1, accepts [0.1,4], and adjusts general point-light colour with pow(max(inputRGB,0),curve) per component before light-space conversion, intensity and saturation; 1 bypasses the curve exactly. It composes across active Adaptive Balance layers and changes colour response, not distance falloff or light radius. pointLightSaturation defaults to 1, accepts [0,2], and adjusts general point-light color, including linear, spot, omnidirectional and particle lights, independently of Linear Lighting; 0 makes their illumination monochrome, and 1 preserves it. It composes across active Adaptive Balance layers and does not change flame textures or light radius. fireIntensity [0,5], fireSaturation [0,2] and fireCurve [0.25,4] all default to 1 and compose across active layers. They adjust visible flame intensity, saturation and color response for fire classified from additive effect-shader flags; they do not require ENB profiles or change emitted point lights. Coverage depends on material flags: differently authored fire can be missed, and non-fire effects sharing those flags can also match. contrast and saturation belong to Color, are independent of lightingAdvanced and Linear Lighting, and grade the composed scene including bloom before fades. Both default to 1; contrast uses a hue-preserving luminance curve around middle gray, and saturation 0 makes the scene monochrome. The six caustics/parallax fields belong to Water. status.adaptiveBalanceVisuals reports configured Global values and effective composed values. Water Effects and water-parallax shader features must be loaded for their respective controls to affect rendering. Cloud brightness, saturation and gamma are independent of sky controls. fogIntensity scales vanilla distance-fog opacity. useAmbientEffectLighting (boolean, default false) is a global Adaptive Balance switch independent of lightingAdvanced; it replaces weather lighting on in-world effect meshes and sky statics with ambient/IBL plus shadowed directional light. effectBrightness and skyStaticBrightness scale the corresponding weather or ambient/directional lighting; 1 preserves brightness, and the existing Effects lighting multiplier is independent. The master/runtime and performance-measurement gates bypass the ambient switch while preserving its saved value. skyStaticTransparency fades sky-static effect meshes, and sunGlareIntensity scales glare already drawn; it does not restore missing glare or weather lens flares. These eight atmosphere fields require lightingAdvanced for the Global layer, support SE/AE/VR and default to neutral except migrated cloud values inherit saved sky adjustments. status.adaptiveBalanceWeatherColorsAvailable reports whether the weather-color hook installed; on failure weather-driven effect/sky-static brightness and sky-gradient grades leave native colors unchanged. status.adaptiveBalanceVisuals.effective.cloudGamma reports the composed gamma, including the active Linear Lighting baseline and profile layers. set_fov_blend_curve requires boolean enabled and optional finite falloff in [0.5,2]. It stages the VR FOV Blend Curve checkbox and remembered exponent without enabling FOV or saving settings; omitted falloff preserves the saved value. Off or exponent 1 restores legacy feathering. Applies to both FOV-only and FOV + TAA, retaining mask geometry and transition width. Changes reset affected history. Requires loaded VR Upscaling; status.fov.blendCurve reports enabled, saved falloff, effectiveFalloff and applicability. set_sky_sync_sunlight requires boolean enabled for direct-light horizon dimming, with optional finite fadeHours in [0,1.5] game hours; omission preserves the current duration. Stages settings without saving and applies on the next sky update in SE/AE/VR. Requires loaded Sky Sync; its master Enable switch still gates the effect. status.skySyncSunlight reports availability, settings and the last applied brightness factor. Moon selection follows climate night boundaries regardless of dimming or visual sun-path offsets. set_pbr_grass_enabled requires boolean enabled and stages PBR grass shading without saving or shader recompilation; enabling requires loaded True PBR and Grass Lighting. Their parent enabled settings also gate rendering. set_pbr_grass_diagnostics_enabled requires boolean enabled and toggles DevBench-only cumulative material and draw diagnostics. set_grass_optimizations_enabled stages the master grass batching switch. Grass diagnostics expose expiredResidents for visibility-age retirement and pressureEvictions for visible-source admission; nativeVisibilityBypassed is retained as zero because native visibility is preserved for fallback. Grass status distinguishes hookInstalled from renderingAvailable; a latched rendering failure keeps native draws and makes the enabled-state performance measurement unavailable. set_grass_optimizations_settings stages a nonempty partial grassOptimizations object with schema-bounded booleans and finite numbers; unknown fields and invalid threshold ordering reject the whole transaction. Changes apply next grass frame without shader recompilation or saving. Defaults enable batching, frustum tests, density reduction and grass Hi-Z; mesh LOD stays off. Density off disables projected-size thinning. CollisionDistance defaults to 2048 units, accepts 0 through 20480 and fades optimized grass collision to zero at that radius; zero disables collision, and the Grass Collision local area still applies. Distance, fade and mesh cost controls remain independent; missing or incompatible middle/far LOD meshes retain full meshes. set_grass_hiz_enabled requires boolean enabled and changes only grass occlusion for same-build A/B. It works with Advanced and Legacy scene culling. The master grass switch is independent. status.grassOptimizations reports settings and Hi-Z availability. set_grass_optimizations_diagnostics_enabled toggles DevBench-only capture, admission, draw rejection, persistent CPU bucket and source reuse, native visibility bypass, coarse group/instance rejection, incremental-record upload/reuse, grass Hi-Z build-failure/state and same-model compatibility counters plus asynchronous GPU eye-instance counts for frustum, density, distance, fade, Hi-Z failure reasons and each surviving LOD tier; CPU coarse counts are logical instances before per-eye GPU counts. Counters are cumulative and GPU counts may lag several frames. Use profiler pass timings alongside on/off A/B; rejected instance counts alone do not establish a performance benefit. ui_status lists observed setup pages and their visible tab IDs. open_feature opens a visible, loaded feature overview or the VR-only VRFpsStabilizer companion page, including its inactive warning when the installed main INI or plugin is unavailable; select_tab navigates an available tab without editing settings. Feature profiling sections require Enabled on the main Profiling page; disabled Profiling cards stay visible and cannot be opened. ui_status page tabs report enabled and disabledReason. Navigation rejects open dialogs, active performance measurements and switching between Stabilizer INI editors with unsaved drafts. Stabilizer uses its own INI save actions, independently of CSX settings. open_feature also accepts the existing Home, General, Advanced, Profiling and PerformanceTuning pages. scroll requires feature, tab and scrollRatio (0=top, 1=bottom); it queues a bounded request for the fresh observed viewport only, while the menu is open and no modal or measurement is active. ui_status.viewport reports fresh, page, tab (legacy for untabbed content), scrollY, scrollMaxY, height, pending and requested/applied generations. Acceptance is not observed scrolling; verify appliedGeneration and scrollY on a later ui_status. Menu closure or queued navigation immediately invalidates observations and cancels unapplied requests; requests belong to the observed ImGui context and window and expire after two UI frames or 500ms. Fresh observations must be from the current or preceding UI frame and at most 500ms old. Viewport observation and scroll controls are compiled only in DevBench builds. restore_defaults applies installed SettingsDefault.json in memory for menu, shader and loaded-feature settings without saving; it preserves the dirty baseline and refuses during save/load or performance measurements.","outputSchema":{"type":"object","properties":{"ok":{"type":"boolean","description":"For UI actions, true means accepted, not completed scrolling. For prepare_coc and prepare_tuning, true only when runtime preparation postconditions are ready; applied and changed describe mutation separately."},"menuOpen":{"type":"boolean"},"viewport":{"type":"object","description":"Latest rendered settings viewport; fresh is false after navigation, closure, a missing context or stale UI frames. appliedGeneration acknowledges issuing a scroll target; verify scrollY in a later observation.","properties":{"fresh":{"type":"boolean"},"page":{"type":"string"},"tab":{"type":"string"},"frame":{"type":"integer"},"scrollY":{"type":"number"},"scrollMaxY":{"type":"number"},"height":{"type":"number"},"pending":{"type":"boolean"},"requestedGeneration":{"type":"integer"},"appliedGeneration":{"type":"integer"}}},"wand":{"$ref":"#/$defs/wandStatus"},"status":{"type":"object","properties":{"wand":{"$ref":"#/$defs/wandStatus"}}}},"$defs":{"wandStatus":{"type":"object","description":"Live wand diagnostics, returned directly by status and under status by setting actions. OCU hand mask uses bit 0 for physical left and bit 1 for physical right.","properties":{"ocuPointerAvailable":{"type":"boolean"},"ocuPointerValid":{"type":"boolean"},"ocuPointerGeneration":{"type":"integer","minimum":0},"ocuPointerFrameId":{"type":"integer","minimum":0},"ocuPointerOwnedHands":{"type":"integer","minimum":0,"maximum":3},"pointerColorSpace":{"type":"string","enum":["srgb_straight_alpha"]},"beamColor":{"type":"array","minItems":4,"maxItems":4,"items":{"type":"number","minimum":0,"maximum":1}},"dotColor":{"type":"array","minItems":4,"maxItems":4,"items":{"type":"number","minimum":0,"maximum":1}},"hands":{"type":"array","items":{"type":"object","properties":{"controller":{"type":"string","enum":["primary","secondary"]},"physicalHand":{"type":"string","enum":["left","right"]},"poseSource":{"type":["string","null"],"enum":["ocu_shared_pointer","render_model_tip","raw_controller",null]},"usingOCUPointer":{"type":"boolean"},"ocuOwnsInput":{"type":"boolean"},"ocuTriggerDown":{"type":"boolean"},"ocuTriggerEligible":{"type":"boolean"},"ocuTriggerSequence":{"type":"integer","minimum":0},"ocuOwnershipGeneration":{"type":"integer","minimum":0}}}}}}}},"inputSchema":{"type":"object","properties":{"visible":{"type":"boolean","description":"Show or hide the feature sidebar for set_sidebar_visible."},"action":{"type":"string","description":"screenshot is obsolete; use communityshaders.screenshot contractMajor 1 action capture","enum":["status","open","open_vr_fov","set_fov_enabled","set_fov_blend_curve","close","screenshot","set_path","set_layout_unlocked","set_wand_settings","texture_stats","set_depth_culling_settings","set_depth_culling_legacy_mode","set_depth_culling_telemetry_enabled","reset_depth_culling_telemetry","set_adaptive_balance_enabled","set_adaptive_balance_visuals","set_adaptive_balance_dof","set_foliage_lighting_enabled","set_terrain_variation_mesh_enabled","set_pbr_grass_enabled","set_pbr_grass_diagnostics_enabled","set_truepbr_verbose_json_logging","set_dynamic_cubemap_resolution","prepare_coc","prepare_tuning","set_motion_adaptive_sharpening","set_parallax_strength","set_sky_sync_sunlight","set_grass_optimizations_enabled","set_grass_optimizations_settings","set_grass_hiz_enabled","set_grass_optimizations_diagnostics_enabled","ui_status","open_feature","select_tab","set_sidebar_visible","scroll","restore_defaults"],"default":"status"},"path":{"type":"string","enum":["auto","overlay","in_scene"],"description":"auto uses IVROverlay on SteamVR from startup; OpenComposite and runtimes without IVROverlay use in_scene."},"enabled":{"type":"boolean","description":"Boolean state required by a setter action."},"adjustment":{"type":"number","minimum":-1,"maximum":1,"description":"Signed RCAS or Luma Unsharp sharpness adjustment in motion."},"thresholdPixels":{"type":"number","minimum":0,"maximum":64,"description":"Motion threshold in output pixels per frame."},"strengthCap":{"type":"number","minimum":0,"maximum":1,"description":"Maximum adjusted strength on the DLSS sharpness slider scale."},"resolution":{"type":"integer","enum":[128,256],"description":"Dynamic cubemap resolution staged for the next game restart."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."},"wand":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"enableWandPointing":{"type":"boolean"},"aimPitchTrimDegrees":{"type":"number","minimum":-90,"maximum":90,"description":"Controller pitch in degrees; standalone CSX applies it locally and shared OCU pointing applies it once in OCU before smoothing."}}},"depthCulling":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"exteriorEnabled":{"type":"boolean"},"interiorEnabled":{"type":"boolean"},"exteriorMinExtent":{"type":"number","minimum":0,"maximum":1000},"interiorMinExtent":{"type":"number","minimum":0,"maximum":1000}}},"visuals":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"useAmbientEffectLighting":{"type":"boolean","description":"Global ambient/IBL and directional effect lighting, default false; independent of lightingAdvanced."},"lightingAdvanced":{"type":"boolean"},"skySaturation":{"type":"number","minimum":0,"maximum":2},"contrast":{"type":"number","minimum":0.5,"maximum":2},"saturation":{"type":"number","minimum":0,"maximum":2},"pointLightSaturation":{"type":"number","minimum":0,"maximum":2,"default":1,"description":"General point-light saturation; 0 is monochrome and 1 preserves color. Requires Global lightingAdvanced; omission preserves the saved value."},"pointLightCurve":{"type":"number","minimum":0.1,"maximum":4,"default":1,"description":"General point-light colour curve applied per component to nonnegative input RGB before light-space conversion, intensity and saturation; 1 bypasses exactly. Requires Global lightingAdvanced; omission preserves the saved value. Does not change radius or distance falloff."},"fireIntensity":{"type":"number","minimum":0,"maximum":5,"default":1,"description":"Visible flame intensity for classified fire effects; 1 is neutral. Requires Global lightingAdvanced; omission preserves the saved value."},"fireSaturation":{"type":"number","minimum":0,"maximum":2,"default":1,"description":"Visible flame saturation for classified fire effects; 0 is monochrome and 1 is neutral. Requires Global lightingAdvanced; omission preserves the saved value."},"fireCurve":{"type":"number","minimum":0.25,"maximum":4,"default":1,"description":"Visible flame color response curve for classified fire effects; 1 is neutral. Requires Global lightingAdvanced; omission preserves the saved value."},"ambient":{"type":"number","minimum":0,"maximum":5},"causticsStrength":{"type":"number","minimum":0,"maximum":2},"causticsTiling":{"type":"number","minimum":0.25,"maximum":4},"causticsSpeed":{"type":"number","minimum":0,"maximum":3},"causticsDispersion":{"type":"number","minimum":0,"maximum":2},"parallaxStrength":{"type":"number","minimum":0,"maximum":2},"parallaxQuality":{"type":"integer","minimum":4,"maximum":64},"cloudBrightness":{"type":"number","minimum":0,"maximum":2},"cloudSaturation":{"type":"number","minimum":0,"maximum":2},"cloudGammaOffset":{"type":"number","minimum":-1,"maximum":1},"fogIntensity":{"type":"number","minimum":0,"maximum":5},"sunGlareIntensity":{"type":"number","minimum":0,"maximum":5},"effectBrightness":{"type":"number","minimum":0,"maximum":2},"skyStaticBrightness":{"type":"number","minimum":0,"maximum":2},"skyStaticTransparency":{"type":"number","minimum":0,"maximum":1}}},"falloff":{"type":"number","minimum":0.5,"maximum":2,"description":"Optional FOV blend exponent; 1 is neutral. Omission preserves the saved value."},"strength":{"type":"number","minimum":0,"maximum":2,"description":"Global Extended Materials parallax depth; 1 is neutral, 0 disables depth and self-shadows. Water is separate."},"fadeHours":{"type":"number","minimum":0,"maximum":1.5,"description":"Optional Sky Sync direct-light recovery/fade duration on the night side of sunset/sunrise, in game hours; omission preserves the current value."},"grassOptimizations":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"Enabled":{"type":"boolean"},"CrossCellBatching":{"type":"boolean"},"FrustumCulling":{"type":"boolean"},"DensityReduction":{"type":"boolean"},"EnableMeshLOD":{"type":"boolean"},"EnableMidLOD":{"type":"boolean"},"EnableFarLOD":{"type":"boolean"},"EnableOcclusionCulling":{"type":"boolean"},"MinPixelSize":{"type":"number","minimum":0,"maximum":64},"FullDetailPixelSize":{"type":"number","minimum":0.01,"maximum":256},"MinDensity":{"type":"number","minimum":0,"maximum":1},"MidLODPixelSize":{"type":"number","minimum":0.01,"maximum":128},"FarLODPixelSize":{"type":"number","minimum":0.01,"maximum":128},"MeshLODBandPixels":{"type":"number","minimum":0.01,"maximum":32},"OcclusionBias":{"type":"number","minimum":0,"maximum":0.05},"MeshCostBias":{"type":"number","minimum":0,"maximum":1},"CostBiasStartDistance":{"type":"number","minimum":0,"maximum":20000},"InvisibleFadeCull":{"type":"number","minimum":0,"maximum":1},"RenderDistanceOverride":{"type":"number","minimum":0,"maximum":100000},"EdgeFadeStart":{"type":"number","minimum":0,"maximum":1},"SimpleShadingPixelSize":{"type":"number","minimum":0,"maximum":32},"CollisionDistance":{"type":"number","minimum":0,"maximum":20480,"default":2048}}},"feature":{"type":"string","description":"Feature short name or VRFpsStabilizer companion page; ui_status reports observed page IDs."},"tab":{"type":"string","description":"Available section ID reported in ui_status tabs; showTab indicates top-tab visibility. Feature performance and profiling tools open from the bottom overview cards. Feature performance sections combine measurements and both profiling views. All connected features expose an independent profiling section. Its card remains visible while profiling is off, but opening it requires Enabled on the main Profiling page. ui_status tabs report enabled and disabledReason. NeuralRendering GPU mode includes separate D3D12 inference timings; CPU mode does not request GPU timestamps. The PerformanceTuning page exposes quick_scan and compare. quick_scan captures instrumented feature timings together without changing settings; compare retains the individual on/off measurements."},"scrollRatio":{"type":"number","minimum":0,"maximum":1,"description":"Normalized vertical viewport offset for scroll."}}}})";
		static const std::string descriptor = [] {
			auto document = json::parse(baseDescriptor);
			document["inputSchema"]["properties"]["visuals"]["properties"]["appearance"] = BuildAdaptiveBalanceAppearanceSchema();
			document["inputSchema"]["properties"]["depthOfField"] = MenuDepthOfFieldSettingsPolicy::Schema();
			const auto dofValues = document["inputSchema"]["properties"]["depthOfField"]["properties"]["sceneDof"]["properties"]["values"];
			const json liveDof = { { "oneOf", json::array({ dofValues, { { "type", "null" } } }) } };
			const json dofStatus = { { "type", "object" }, { "properties", { { "settings", MenuDepthOfFieldSettingsPolicy::Schema() },
																			   { "overridesActive", { { "type", "boolean" } } }, { "correctionActive", { { "type", "boolean" } } },
																			   { "sceneDataAvailable", { { "type", "boolean" } } }, { "underwaterDataAvailable", { { "type", "boolean" } } },
																			   { "scene", liveDof }, { "underwater", liveDof } } } };
			document["outputSchema"]["properties"]["depthOfField"] = dofStatus;
			document["outputSchema"]["properties"]["status"]["properties"]["adaptiveBalanceDepthOfField"] = dofStatus;
			const json finalBrightnessSchema = {
				{ "type", "number" }, { "minimum", AdaptiveBalanceGodray::kMin }, { "maximum", AdaptiveBalanceGodray::kMax },
				{ "default", AdaptiveBalanceGodray::kDefault },
				{ "description", "Global final godray brightness after gamma conversion; independent of profile intensity and lightingAdvanced. Adaptive Balance off makes the effective multiplier 1. Does not enable godrays." }
			};
			document["inputSchema"]["properties"]["visuals"]["properties"]["godrayFinalBrightness"] = finalBrightnessSchema;
			const json finalBrightnessStatus = {
				{ "type", "object" },
				{ "properties", { { "global", { { "type", "object" }, { "properties", { { "godrayFinalBrightness", finalBrightnessSchema } } } } },
									{ "effective", { { "type", "object" }, { "properties", { { "godrayFinalBrightness", finalBrightnessSchema } } } } } } }
			};
			document["outputSchema"]["properties"]["adaptiveBalanceVisuals"] = finalBrightnessStatus;
			document["outputSchema"]["properties"]["status"]["properties"]["adaptiveBalanceVisuals"] = finalBrightnessStatus;
			return document.dump();
		}();
		devBench->RegisterTool("communityshaders.menu", descriptor.c_str(), &ToolHandler, nullptr);
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
