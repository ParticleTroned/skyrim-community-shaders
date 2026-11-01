#include "MenuDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/AdaptiveBalanceGodraySettings.h"
#	include "Features/AdaptiveBrightness.h"
#	include "Features/DynamicCubemaps.h"
#	include "Features/ExtendedMaterials.h"
#	include "Features/FoliageLighting.h"
#	include "Features/GrassCollision.h"
#	include "Features/GrassOptimizations.h"
#	include "Features/GrassOptimizations/GrassBucketRenderer.h"
#	include "Features/LinearLighting.h"
#	include "Features/ScreenSpaceGI.h"
#	include "Features/ScreenSpaceShadows.h"
#	include "Features/ScreenshotFeature.h"
#	include "Features/SkySync.h"
#	include "Features/TerrainVariation.h"
#	include "Features/Upscaling.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "Menu/SettingsPage.h"
#	include "Menu/FeatureListRenderer.h"
#	include "Menu/PerformanceTuningRenderer.h"
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

	json AdaptiveBalancePresetUIStatus()
	{
		const auto& state = globals::features::adaptiveBrightness.presetUI;
		return { { "mode", state.mode == MenuUI::AdaptiveBalancePresets::Mode::Import ? "import" : "export" },
			{ "collection", MenuUI::AdaptiveBalancePresets::CollectionId(state.collection) } };
	}

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

	json RunOnMainThread(std::function<json()> a_run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run));
	}

	std::optional<json> PrepareSettingsMutation(std::string_view action, bool preserveBaseline = true)
	{
		if (!globals::menu || !globals::state)
			return json{ { "ok", false }, { "action", action }, { "error", "Settings are unavailable" } };
		if (PerformanceTuningRenderer::HasActiveMeasurements() || globals::state->IsPersistentMutationBlocked())
			return json{ { "ok", false }, { "action", action }, { "error", "Finish the current save, load or performance comparison before changing settings" } };
		if (preserveBaseline && !globals::menu->PrepareSettingsMutation())
			return json{ { "ok", false }, { "action", action }, { "error", "Could not preserve the settings baseline" } };
		return std::nullopt;
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
			{ "description", "Partial Global appearance update. Requires Global lightingAdvanced; omitted fields retain their saved values. RGB tints are three finite numeric channels. eyeSpecularBrightness scales eye highlights and reflections; eyeBrightness scales all eye lighting. Both accept 0..5, default 1, and preserve non-eye materials and previews. auroraIntensity (0..5) and auroraCurve (0.1..4) grade sky objects in the aurora slot, including replacements; both default to 1 and preserve alpha." },
			{ "properties", std::move(properties) }
		};
	}

	json BuildAdaptiveBalanceGodraySchema()
	{
		json properties = json::object();
		const VolumetricLightingTuning::Profile defaults;
		for (const auto& field : VolumetricLightingTuning::kProfileFields)
			properties[field.name] = {
				{ "type", "number" }, { "minimum", 0 }, { "maximum", field.maximum },
				{ "default", defaults.*field.member }, { "description", field.label }
			};
		const json profile = {
			{ "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false }, { "properties", std::move(properties) }
		};
		return {
			{ "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false },
			{ "properties", { { "exterior", profile }, { "interior", profile } } },
			{ "description", "Partial exterior/interior godray base update. Omitted fields retain saved values. Profile appearance adjustments compose over these bases. All godray appearance tuning is neutral while Adaptive Balance is disabled; does not enable Volumetric Lighting." }
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
		static constexpr std::array<Field, 25> fields{ { { "skySaturation", 0.0, 2.0 },
			{ "volumetricClarity", AdaptiveBalanceGodray::kClarityMin, AdaptiveBalanceGodray::kClarityMax },
			{ "moonReflectionStrength", AdaptiveBrightness::kMoonReflectionStrengthMin, AdaptiveBrightness::kMoonReflectionStrengthMax },
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
			if (name == "godrays") {
				if (!value.is_object() || value.empty())
					return "godrays must be a nonempty object";
				for (const auto& [context, profile] : value.items()) {
					if ((context != "exterior" && context != "interior") || !profile.is_object() || profile.empty())
						return "Unknown or invalid godray context: " + context;
					for (const auto& [key, input] : profile.items()) {
						const auto field = std::find_if(VolumetricLightingTuning::kProfileFields.begin(), VolumetricLightingTuning::kProfileFields.end(),
							[&](const auto& entry) { return key == entry.name; });
						if (field == VolumetricLightingTuning::kProfileFields.end() || !input.is_number())
							return "Unknown or non-numeric godray field: " + key;
						const double number = input.get<double>();
						if (!std::isfinite(number) || number < 0.0 || number > field->maximum)
							return "Out-of-range or invalid godray field: " + key;
					}
				}
				continue;
			}
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
		configured["volumetricClarity"] = balance.settings.volumetricClarity;
		configured["godrays"] = balance.settings.godrays;
		configured["moonReflectionStrength"] = balance.settings.moonReflectionStrength;
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
		effective["volumetricClarity"] = balance.GetEffectiveVolumetricClarity();
		effective["godrays"] = { { "exterior", balance.GetEffectiveGodrayProfile(false) }, { "interior", balance.GetEffectiveGodrayProfile(true) } };
		effective["moonReflectionStrength"] = balance.GetEffectiveMoonReflectionStrength();
		return { { "global", std::move(configured) }, { "effective", std::move(effective) } };
	}

	json BuildStatus()
	{
		auto* menu = globals::menu;
		auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		const auto plan = globals::features::upscaling.GetRuntimeResolutionPlan();
		return {
			{ "runtime", REL::Module::IsAE() ? "AE" : "SE" },
			{ "menuEnabled", menu && menu->IsEnabled },
			{ "menuLayoutResetPending", menu && menu->IsLayoutResetPending() },
			{ "performanceOverlayVisible", menu && menu->overlayVisible },
			{ "mainMenuOpen", globals::state && globals::state->isMainMenuOpen },
			{ "loadingMenuOpen", globals::state && globals::state->isLoadingMenuOpen },
			{ "renderSize", { plan.engineRenderSize.x, plan.engineRenderSize.y } },
			{ "outputSize", { plan.finalOutputSize.x, plan.finalOutputSize.y } },
			{ "adaptiveBalanceEnabled", globals::features::adaptiveBrightness.settings.enabled },
			{ "adaptiveBalanceActive", globals::features::adaptiveBrightness.IsRuntimeEnabled() },
			{ "adaptiveBalanceWeatherColorsAvailable", globals::features::adaptiveBrightness.weatherColorHookInstalled },
			{ "adaptiveBalanceVisuals", AdaptiveBalanceVisualsStatus() },
			{ "adaptiveBalanceDepthOfField", AdaptiveBalanceDepthOfFieldStatus() },
			{ "skySyncWorldspaces", globals::features::skySync.GetWorldspaceStatus() },
			{ "skySyncSunlight", { { "loaded", globals::features::skySync.loaded }, { "skySyncEnabled", globals::features::skySync.settings.Enabled }, { "dimmingEnabled", globals::features::skySync.settings.DimSunlightUnderHorizon }, { "fadeHours", globals::features::skySync.settings.HorizonFadeHours }, { "appliedFactor", globals::features::skySync.GetSunlightDimmingFactor() } } },
			{ "foliageLightingEnabled", globals::features::foliageLighting.IsEnabled() },
			{ "grassOptimizations", globals::features::grassOptimizations.GetDiagnostics() },
			{ "grassCollision", { { "loaded", globals::features::grassCollision.loaded }, { "enabled", globals::features::grassCollision.settings.EnableGrassCollision }, { "collisionStrength", globals::features::grassCollision.GetCollisionStrength() } } },
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
		};
	}

	json BuildResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "request_dynamic_cubemap_refresh") {
			return RunOnMainThread([]() -> json {
				if (auto error = PrepareSettingsMutation("request_dynamic_cubemap_refresh", false))
					return *error;
				auto& cubemaps = globals::features::dynamicCubemaps;
				if (!cubemaps.loaded)
					return { { "error", "Dynamic Cubemaps is not loaded" }, { "errorCode", "dynamic_cubemaps_unavailable" } };
				cubemaps.RequestCaptureRefresh();
				return { { "ok", true }, { "action", "request_dynamic_cubemap_refresh" }, { "queued", true }, { "persisted", false } };
			});
		}
		if (action == "set_grass_collision_strength") {
			const auto value = a_args.find("collisionStrength");
			if (value == a_args.end() || !value->is_number())
				return { { "ok", false }, { "error", "collisionStrength must be a finite number in [0,5]" }, { "action", action } };
			const double strength = value->get<double>();
			if (!std::isfinite(strength) || strength < GrassCollision::kMinCollisionStrength || strength > GrassCollision::kMaxCollisionStrength)
				return { { "ok", false }, { "error", "collisionStrength must be a finite number in [0,5]" }, { "action", action } };
			return RunOnMainThread([strength, action]() -> json {
				if (auto error = PrepareSettingsMutation(action))
					return *error;
				if (PerformanceTuningRenderer::HasActiveMeasurements())
					return { { "ok", false }, { "error", "Finish or cancel the performance measurement before changing grass collision strength" }, { "action", action } };
				auto& collision = globals::features::grassCollision;
				if (!collision.loaded)
					return { { "ok", false }, { "error", "Grass Collision is not loaded" }, { "action", action } };
				if (!globals::menu)
					return { { "ok", false }, { "error", "CSX menu unavailable" }, { "action", action } };
				if (!collision.SetCollisionStrength(static_cast<float>(strength)))
					return { { "ok", false }, { "error", "Grass collision strength rejected" }, { "action", action } };
				globals::menu->RequestSettingsDirtyCheck();
				return { { "ok", true }, { "action", action }, { "collisionStrength", collision.GetCollisionStrength() }, { "persisted", false }, { "status", BuildStatus() } };
			});
		}
		if (action == "set_grass_background_preparation_enabled") {
			if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean())
				return { { "ok", false }, { "error", "enabled must be a boolean" }, { "action", action } };
			return RunOnMainThread([enabled = a_args["enabled"].get<bool>(), action]() -> json {
				if (auto error = PrepareSettingsMutation(action, false))
					return *error;
				if (PerformanceTuningRenderer::HasActiveMeasurements())
					return { { "ok", false }, { "error", "Finish or cancel the performance measurement before changing grass preparation" }, { "action", action } };
				auto& grass = globals::features::grassOptimizations;
				if (enabled && (!grass.loaded || !grass.GetRenderer().IsRenderingAvailable()))
					return { { "ok", false }, { "error", "Grass Optimizations rendering is unavailable" }, { "action", action } };
				grass.GetRenderer().SetBackgroundPreparationEnabled(enabled);
				return { { "ok", true }, { "action", action }, { "persisted", false }, { "status", BuildStatus() } };
			});
		}
		if (action == "grass_telemetry") {
			if (a_args.contains("requestTelemetry") && !a_args["requestTelemetry"].is_boolean())
				return { { "error", "requestTelemetry must be a boolean" } };
			return RunOnMainThread([request = a_args.value("requestTelemetry", true)]() -> json {
				auto& grass = globals::features::grassOptimizations;
				if (request) {
					grass.RequestTelemetry();
					globals::features::grassCollision.RequestDiagnostics();
				}
				json scene = { { "frame", globals::state ? json(globals::state->frameCountAtomic.load(std::memory_order_relaxed)) : json(nullptr) } };
				scene["csxMenuOpen"] = globals::menu && globals::menu->IsEnabled;
				if (auto ui = globals::game::ui)
					scene["gamePaused"] = ui->GameIsPaused();
				if (auto player = RE::PlayerCharacter::GetSingleton()) {
					const auto position = player->GetPosition();
					scene["position"] = { position.x, position.y, position.z };
					if (auto cell = player->GetParentCell())
						scene["cellFormId"] = cell->GetFormID();
				}
				if (auto calendar = globals::game::calendar)
					scene["gameHour"] = calendar->GetHour();
				if (auto sky = globals::game::sky; sky && sky->currentWeather)
					scene["weatherFormId"] = sky->currentWeather->GetFormID();
				return { { "ok", true }, { "action", "grass_telemetry" }, { "grassOptimizations", grass.GetDiagnostics() }, { "grassCollision", globals::features::grassCollision.GetDiagnostics() }, { "scene", std::move(scene) } };
			});
		}
		if (action == "set_adaptive_balance_dof") {
			const auto patch = a_args.value("depthOfField", json{});
			std::string error;
			if (!MenuDepthOfFieldSettingsPolicy::Validate(patch, error))
				return { { "error", error }, { "action", action } };
			return RunOnMainThread([patch, action]() -> json {
				if (auto error = PrepareSettingsMutation(action))
					return *error;
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
		if (action == "set_sky_sync_disabled_worldspaces") {
			const auto worldspaces = a_args.value("disabledWorldspaces", json{});
			return RunOnMainThread([worldspaces, action]() -> json {
				if (auto error = PrepareSettingsMutation(action))
					return *error;
				if (PerformanceTuningRenderer::HasActiveMeasurements())
					return { { "ok", false }, { "error", "Finish or cancel the performance measurement before changing Sky Sync exclusions" }, { "action", action } };
				auto& skySync = globals::features::skySync;
				if (!skySync.loaded || !globals::menu)
					return { { "ok", false }, { "error", "Sky Sync or CSX menu unavailable" }, { "action", action } };
				if (!skySync.SetDisabledWorldspaces(worldspaces))
					return { { "ok", false }, { "error", fmt::format("disabledWorldspaces must contain at most {} nonempty strings of at most {} bytes without control characters", SkySync::MaxDisabledWorldspaces, SkySync::MaxWorldspaceEditorIDLength) }, { "action", action } };
				globals::menu->RequestSettingsDirtyCheck();
				return { { "ok", true }, { "action", action }, { "persisted", false }, { "skySyncWorldspaces", skySync.GetWorldspaceStatus() } };
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
		if (action == "set_adaptive_balance_preset_ui") {
			if (!a_args.contains("presetMode") || !a_args["presetMode"].is_string() ||
				!a_args.contains("presetCollection") || !a_args["presetCollection"].is_string())
				return { { "ok", false }, { "error", "presetMode and presetCollection must be strings" } };
			const auto mode = a_args["presetMode"].get<std::string>();
			const auto collection = a_args["presetCollection"].get<std::string>();
			namespace Presets = MenuUI::AdaptiveBalancePresets;
			std::optional<Presets::Collection> selected;
			for (int index = 0; index < static_cast<int>(Presets::Collection::Count); ++index) {
				const auto candidate = static_cast<Presets::Collection>(index);
				if (collection == Presets::CollectionId(candidate))
					selected = candidate;
			}
			if ((mode != "export" && mode != "import") || !selected)
				return { { "ok", false }, { "error", "Unknown preset mode or collection" } };
			return RunOnMainThread([mode, selected]() -> json {
				auto& balance = globals::features::adaptiveBrightness;
				if (!globals::menu || !globals::state || !balance.loaded || !balance.IsInMenu() ||
					balance.IsHiddenFromUserView() || globals::state->IsFeatureDisabled(balance.GetShortName()))
					return { { "ok", false }, { "error", "Adaptive Balance is unavailable" } };
				if (PerformanceTuningRenderer::HasActiveMeasurements() ||
					(ImGui::GetCurrentContext() && ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)))
					return { { "ok", false }, { "error", "Finish the active dialog or performance comparison before navigating" } };
				if (!MenuUI::SettingsPage::Select("AdaptiveBrightness", "presets"))
					return { { "ok", false }, { "error", "Presets navigation is unavailable" } };
				balance.presetUI = { mode == "import" ? Presets::Mode::Import : Presets::Mode::Export, *selected };
				balance.presetFilesDirty = true;
				globals::menu->SelectFeatureMenu("AdaptiveBrightness");
				globals::menu->OpenMenu();
				return { { "ok", true }, { "action", "set_adaptive_balance_preset_ui" },
					{ "adaptiveBalancePresets", AdaptiveBalancePresetUIStatus() } };
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
											   (!(*found)->loaded || !globals::state || globals::state->IsFeatureDisabled(featureName))))
						return { { "ok", false }, { "error", "Unknown or unavailable feature" } };
					if (!MenuUI::SettingsPage::Select(featureName.c_str(), "overview"))
						return { { "ok", false }, { "error", "The requested settings page cannot be opened while its current edit is pending." } };
					globals::menu->SelectFeatureMenu(builtIn.empty() ? featureName : builtIn);
					globals::menu->OpenMenu();
				} else if (action == "select_tab" && !MenuUI::SettingsPage::Navigate(featureName.c_str(), tab.c_str())) {
					return { { "ok", false }, { "error", "Unavailable or disabled tab. Enable profiling on the main Profiling page for feature timing views; query ui_status for availability." } };
				}
				return { { "ok", true }, { "action", action }, { "menuOpen", globals::menu->IsEnabled }, { "viewport", MenuUI::DevBenchViewport::Describe(globals::menu->IsEnabled) }, { "pages", MenuUI::SettingsPage::Describe() }, { "unsaved", globals::menu->HasUnsavedSettings() }, { "sidebarVisible", FeatureListRenderer::IsSidebarVisible() },
					{ "adaptiveBalancePresets", AdaptiveBalancePresetUIStatus() } };
			});
		}

		static const json supported = json::array({ "status", "close", "grass_telemetry", "open", "open_feature", "request_dynamic_cubemap_refresh", "reset_menu_layout", "restore_defaults", "screenshot", "scroll", "select_tab", "set_adaptive_balance_dof", "set_adaptive_balance_enabled", "set_adaptive_balance_preset_ui", "set_adaptive_balance_visuals", "set_dynamic_cubemap_resolution", "set_foliage_lighting_enabled", "set_grass_background_preparation_enabled", "set_grass_collision_strength", "set_grass_hiz_enabled", "set_grass_optimizations_diagnostics_enabled", "set_grass_optimizations_enabled", "set_grass_optimizations_settings", "set_motion_adaptive_sharpening", "set_parallax_strength", "set_pbr_grass_diagnostics_enabled", "set_pbr_grass_enabled", "set_sidebar_visible", "set_sky_sync_disabled_worldspaces", "set_sky_sync_sunlight", "set_terrain_variation_mesh_enabled", "set_truepbr_verbose_json_logging", "ui_status" });
		if (std::find(supported.begin(), supported.end(), action) == supported.end())
			return { { "error", "unknown action" }, { "action", action }, { "supported", supported } };

		if ((action == "set_adaptive_balance_enabled" || action == "set_grass_optimizations_enabled" || action == "set_grass_hiz_enabled" || action == "set_grass_optimizations_diagnostics_enabled" || action == "set_foliage_lighting_enabled" || action == "set_terrain_variation_mesh_enabled" || action == "set_pbr_grass_enabled" || action == "set_pbr_grass_diagnostics_enabled" || action == "set_truepbr_verbose_json_logging" || action == "set_sky_sync_sunlight") &&
			(!a_args.contains("enabled") || !a_args.at("enabled").is_boolean())) {
			return {
				{ "error", action + " requires boolean enabled" },
				{ "action", action },
			};
		}
		if (action == "set_dynamic_cubemap_resolution" &&
			(!a_args.contains("resolution") || !a_args.at("resolution").is_number_integer() ||
				a_args.at("resolution") < 0 || a_args.at("resolution") > UINT32_MAX)) {
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

		const bool hasHorizonFadeHours = action == "set_sky_sync_sunlight" && a_args.contains("fadeHours");
		if (hasHorizonFadeHours && (!a_args.at("fadeHours").is_number() ||
									   !std::isfinite(a_args.at("fadeHours").get<double>()) ||
									   a_args.at("fadeHours").get<double>() < 0.0 ||
									   a_args.at("fadeHours").get<double>() > SkySync::MaxHorizonFadeHours))
			return { { "error", "fadeHours must be finite and between 0 and 1.5 game hours" }, { "action", action } };
		const float horizonFadeHours = hasHorizonFadeHours ? a_args.at("fadeHours").get<float>() : SkySync::DefaultHorizonFadeHours;
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

		return RunOnMainThread([action, enabled, resolution, motionSharpening, visuals, grassUpdate, parallaxStrength, hasHorizonFadeHours, horizonFadeHours]() -> json {
			auto* menu = globals::menu;
			if (!menu)
				return { { "error", "CSX menu unavailable" } };
			if (action.starts_with("set_")) {
				if (auto error = PrepareSettingsMutation(action))
					return *error;
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
			} else if (action == "reset_menu_layout") {
				menu->RequestLayoutReset();
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
				auto godrays = balance.settings.godrays;
				if (visuals.contains("godrays")) {
					json updated = godrays;
					updated.merge_patch(visuals.at("godrays"));
					godrays = updated.get<AdaptiveBalanceGodray::Profiles>();
				}
				balance.settings.godrays = godrays;
				balance.settings.globalProfile = profile;
				balance.settings.useAmbientEffectLighting = visuals.value("useAmbientEffectLighting", balance.settings.useAmbientEffectLighting);
				balance.settings.volumetricClarity = visuals.value("volumetricClarity", balance.settings.volumetricClarity);
				balance.settings.moonReflectionStrength = visuals.value("moonReflectionStrength", balance.settings.moonReflectionStrength);
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

			if (action == "set_dynamic_cubemap_resolution")
				return { { "action", action }, { "resolution", resolution }, { "persisted", false }, { "status", BuildStatus() } };
			json result = {
				{ "action", action },
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
			R"({"description":"Desktop SE/AE menu inspection, bounded page navigation and session settings controls. Mutations are not saved. ui_status reports rendered pages, tabs and a fresh viewport; scroll acceptance is not completion, so inspect appliedGeneration and scrollY later. Navigation and settings mutation are blocked during performance comparisons. restore_defaults applies installed defaults in memory with rollback. Screenshot requests use the versioned screenshot service. Feature rendering uses the native single viewport and preserves SE frame generation.","outputSchema":{"type":"object","properties":{"ok":{"type":"boolean","description":"For UI actions, true means accepted, not completed scrolling. For prepare_coc and prepare_tuning, true only when runtime preparation postconditions are ready; applied and changed describe mutation separately."},"menuOpen":{"type":"boolean"},"viewport":{"type":"object","description":"Latest rendered settings viewport; fresh is false after navigation, closure, a missing context or stale UI frames. appliedGeneration acknowledges issuing a scroll target; verify scrollY in a later observation.","properties":{"fresh":{"type":"boolean"},"page":{"type":"string"},"tab":{"type":"string"},"frame":{"type":"integer"},"scrollY":{"type":"number"},"scrollMaxY":{"type":"number"},"height":{"type":"number"},"pending":{"type":"boolean"},"requestedGeneration":{"type":"integer"},"appliedGeneration":{"type":"integer"}}},"status":{"type":"object","properties":{"grassCollision":{"type":"object","properties":{"loaded":{"type":"boolean"},"enabled":{"type":"boolean"},"collisionStrength":{"type":"number","minimum":0,"maximum":5}}}}},"collisionStrength":{"type":"number","minimum":0,"maximum":5},"adaptiveBalancePresets":{"type":"object","properties":{"mode":{"type":"string","enum":["export","import"]},"collection":{"type":"string","enum":["full","base","locations","worldspace","region","city"]}}}}},"inputSchema":{"type":"object","properties":{"visible":{"type":"boolean","description":"Show or hide the feature sidebar for set_sidebar_visible."},"action":{"type":"string","description":"screenshot is obsolete; use communityshaders.screenshot contractMajor 1 action capture","enum":["status","open","close","screenshot","set_adaptive_balance_enabled","set_adaptive_balance_visuals","set_adaptive_balance_dof","set_foliage_lighting_enabled","set_terrain_variation_mesh_enabled","set_pbr_grass_enabled","set_pbr_grass_diagnostics_enabled","set_truepbr_verbose_json_logging","set_dynamic_cubemap_resolution","set_motion_adaptive_sharpening","set_parallax_strength","set_sky_sync_sunlight","set_grass_optimizations_enabled","set_grass_optimizations_settings","set_grass_hiz_enabled","set_grass_optimizations_diagnostics_enabled","ui_status","open_feature","select_tab","set_sidebar_visible","scroll","restore_defaults","set_grass_collision_strength","set_adaptive_balance_preset_ui"],"default":"status"},"enabled":{"type":"boolean","description":"Boolean state required by a setter action."},"adjustment":{"type":"number","minimum":-1,"maximum":1,"description":"Signed RCAS or Luma Unsharp sharpness adjustment in motion."},"thresholdPixels":{"type":"number","minimum":0,"maximum":64,"description":"Motion threshold in output pixels per frame."},"strengthCap":{"type":"number","minimum":0,"maximum":1,"description":"Maximum adjusted strength on the DLSS sharpness slider scale."},"resolution":{"type":"integer","enum":[128,256],"description":"Dynamic cubemap resolution staged for the next game restart."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."},"visuals":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"useAmbientEffectLighting":{"type":"boolean","description":"Global ambient/IBL and directional effect lighting, default false; independent of lightingAdvanced."},"lightingAdvanced":{"type":"boolean"},"skySaturation":{"type":"number","minimum":0,"maximum":2},"contrast":{"type":"number","minimum":0.5,"maximum":2},"saturation":{"type":"number","minimum":0,"maximum":2},"pointLightSaturation":{"type":"number","minimum":0,"maximum":2,"default":1,"description":"General point-light saturation; 0 is monochrome and 1 preserves color. Requires Global lightingAdvanced; omission preserves the saved value."},"pointLightCurve":{"type":"number","minimum":0.1,"maximum":4,"default":1,"description":"General point-light colour curve applied per component to nonnegative input RGB before light-space conversion, intensity and saturation; 1 bypasses exactly. Requires Global lightingAdvanced; omission preserves the saved value. Does not change radius or distance falloff."},"fireIntensity":{"type":"number","minimum":0,"maximum":5,"default":1,"description":"Visible flame intensity for classified fire effects; 1 is neutral. Requires Global lightingAdvanced; omission preserves the saved value."},"fireSaturation":{"type":"number","minimum":0,"maximum":2,"default":1,"description":"Visible flame saturation for classified fire effects; 0 is monochrome and 1 is neutral. Requires Global lightingAdvanced; omission preserves the saved value."},"fireCurve":{"type":"number","minimum":0.25,"maximum":4,"default":1,"description":"Visible flame color response curve for classified fire effects; 1 is neutral. Requires Global lightingAdvanced; omission preserves the saved value."},"ambient":{"type":"number","minimum":0,"maximum":5},"causticsStrength":{"type":"number","minimum":0,"maximum":2},"causticsTiling":{"type":"number","minimum":0.25,"maximum":4},"causticsSpeed":{"type":"number","minimum":0,"maximum":3},"causticsDispersion":{"type":"number","minimum":0,"maximum":2},"parallaxStrength":{"type":"number","minimum":0,"maximum":2},"parallaxQuality":{"type":"integer","minimum":4,"maximum":64},"cloudBrightness":{"type":"number","minimum":0,"maximum":2},"cloudSaturation":{"type":"number","minimum":0,"maximum":2},"cloudGammaOffset":{"type":"number","minimum":-1,"maximum":1},"fogIntensity":{"type":"number","minimum":0,"maximum":5},"sunGlareIntensity":{"type":"number","minimum":0,"maximum":5},"effectBrightness":{"type":"number","minimum":0,"maximum":2},"skyStaticBrightness":{"type":"number","minimum":0,"maximum":2},"skyStaticTransparency":{"type":"number","minimum":0,"maximum":1}}},"strength":{"type":"number","minimum":0,"maximum":2,"description":"Global Extended Materials parallax depth; 1 is neutral, 0 disables depth and self-shadows. Water is separate."},"fadeHours":{"type":"number","minimum":0,"maximum":1.5,"description":"Optional Sky Sync direct-light recovery/fade duration on the night side of sunset/sunrise, in game hours; omission preserves the current value."},"grassOptimizations":{"type":"object","minProperties":1,"additionalProperties":false,"properties":{"Enabled":{"type":"boolean"},"CrossCellBatching":{"type":"boolean"},"FrustumCulling":{"type":"boolean"},"DensityReduction":{"type":"boolean"},"EnableMeshLOD":{"type":"boolean"},"EnableMidLOD":{"type":"boolean"},"EnableFarLOD":{"type":"boolean"},"EnableOcclusionCulling":{"type":"boolean"},"MinPixelSize":{"type":"number","minimum":0,"maximum":64},"FullDetailPixelSize":{"type":"number","minimum":0.01,"maximum":256},"MinDensity":{"type":"number","minimum":0,"maximum":1},"MidLODPixelSize":{"type":"number","minimum":0.01,"maximum":128},"FarLODPixelSize":{"type":"number","minimum":0.01,"maximum":128},"MeshLODBandPixels":{"type":"number","minimum":0.01,"maximum":32},"OcclusionBias":{"type":"number","minimum":0,"maximum":0.05},"MeshCostBias":{"type":"number","minimum":0,"maximum":1},"CostBiasStartDistance":{"type":"number","minimum":0,"maximum":20000},"InvisibleFadeCull":{"type":"number","minimum":0,"maximum":1},"RenderDistanceOverride":{"type":"number","minimum":0,"maximum":100000},"EdgeFadeStart":{"type":"number","minimum":0,"maximum":1},"SimpleShadingPixelSize":{"type":"number","minimum":0,"maximum":32},"CollisionDistance":{"type":"number","minimum":0,"maximum":20480,"default":2048}}},"feature":{"type":"string","description":"Feature short name or built-in page ID returned by ui_status."},"tab":{"type":"string","description":"Available section ID reported in ui_status tabs; showTab indicates top-tab visibility. Feature performance and profiling tools open from the bottom overview cards. Feature performance sections combine measurements and both profiling views. All connected features expose an independent profiling section. Its card remains visible while profiling is off, but opening it requires Enabled on the main Profiling page. ui_status tabs report enabled and disabledReason. NeuralRendering GPU mode includes separate D3D12 inference timings; CPU mode does not request GPU timestamps. The PerformanceTuning page exposes quick_scan and compare. quick_scan captures instrumented feature timings together without changing settings; compare retains the individual on/off measurements."},"scrollRatio":{"type":"number","minimum":0,"maximum":1,"description":"Normalized vertical viewport offset for scroll."},"collisionStrength":{"type":"number","minimum":0,"maximum":5,"description":"Grass Collision bending multiplier. 1 is the original strength, 0 removes bending. Used by set_grass_collision_strength."},"presetMode":{"type":"string","enum":["export","import"],"description":"Session-only Adaptive Balance Presets mode; does not read or write a file."},"presetCollection":{"type":"string","enum":["full","base","locations","worldspace","region","city"],"description":"Collection selected in the consolidated Adaptive Balance Presets diagram."}}}})";
		static const std::string descriptor = [] {
			auto document = json::parse(baseDescriptor);
			document["inputSchema"]["properties"]["action"]["enum"].push_back("set_sky_sync_disabled_worldspaces");
			const json worldspaceList = {
				{ "type", "array" }, { "maxItems", SkySync::MaxDisabledWorldspaces },
				{ "items", { { "type", "string" }, { "minLength", 1 }, { "maxLength", SkySync::MaxWorldspaceEditorIDLength }, { "pattern", "^[^\\u0000-\\u001f\\u007f]+$" } } }
			};
			document["inputSchema"]["properties"]["disabledWorldspaces"] = worldspaceList;
			document["outputSchema"]["properties"]["skySyncWorldspaces"] = {
				{ "type", "object" }, { "properties", { { "disabledWorldspaces", worldspaceList },
														  { "lastWorldspaceEditorID", { { "type", "string" } } },
														  { "lastWorldspaceDisabled", { { "type", "boolean" } } },
														  { "updatePending", { { "type", "boolean" } } } } }
			};
			document["description"] = document["description"].get<std::string>() +
			                          " set_sky_sync_disabled_worldspaces replaces the entire disabledWorldspaces array without saving or enabling Sky Sync. Accepts at most 128 nonempty editor IDs, each at most 256 UTF-8 bytes and without control characters; case-insensitive duplicates are removed. An empty array clears the exclusions. Invalid input leaves the list unchanged. Active performance measurements block mutation. Applies on the next enabled Sky Sync update on SE/AE, restoring owned light and lens-flare adjustments and resetting caster state when exclusion changes. skySyncWorldspaces in status or the setter response reports the list, last evaluated worldspace and exclusion, and updatePending. In excluded realms, Sky Sync leaves lighting and weather lens flares to the game.";
			document["inputSchema"]["properties"]["action"]["enum"].push_back("request_dynamic_cubemap_refresh");
			document["description"] = document["description"].get<std::string>() +
			                          " request_dynamic_cubemap_refresh queues a render-thread history reset for both cubemap captures. It preserves settings and uses the existing high-priority update cadence. queued acknowledges the request, not completed GPU work or automatic lighting detection. Requires loaded Dynamic Cubemaps; processing waits for rendering and ready resources. Inspect DynamicCubemaps::DetectLighting and Publish profiler passes and screenshots for runtime evidence.";
			document["inputSchema"]["properties"]["action"]["enum"].push_back("reset_menu_layout");
			document["outputSchema"]["properties"]["status"]["properties"]["menuLayoutResetPending"] = {
				{ "type", "boolean" }, { "description", "Recovery is queued until the next overlay frame." }
			};
			document["description"] = document["description"].get<std::string>() +
			                          " reset_menu_layout queues render-thread recovery on SE and AE: reopen the menu, restore window geometry, sidebar proportions, zoom and font sizing. menuLayoutResetPending reports whether the next overlay frame has consumed it. Rendering settings and shader caches are unchanged; theme changes remain unsaved.";
			document["inputSchema"]["properties"]["action"]["enum"].push_back("grass_telemetry");
			document["inputSchema"]["properties"]["action"]["enum"].push_back("set_grass_background_preparation_enabled");
			document["description"] = document["description"].get<std::string>() +
			                          " set_grass_background_preparation_enabled requires boolean enabled for a session-only A/B override of default-on background grass CPU preparation in all builds. The override and diagnostics are DevBench-only. Applies at the next grass frame on SE/AE; does not save or enable GO. A single worker sorts captured sources, forms compatible buckets and prepares record layouts. Native validation, GPU uploads and drawing stay on the render thread. Unchanged complete queues remain batched; changed or unfinished queues use complete native fallback. Stale queues are rejected at publication. Disabling restores synchronous preparation without waiting for a worker. Unused completed snapshots retire at frame end, including frames without grass. Obsolete job failures do not disable a new toggle request. Active performance measurements block mutation. status.grassOptimizations.backgroundPreparation reports requested enabled, render-thread active, updatePending, pending publication, failed (synchronous fallback), cumulative submitted/published/discarded jobs, rejectedBuckets and frames, renderMilliseconds (all PrepareFrame CPU time) and workerMilliseconds (successful worker layout time). Sample counter deltas for same-build A/B; values do not measure GPU work or establish a speedup. No diagnostic GPU probes are requested.";
			json preparationProperties = json::object();
			for (const auto* name : { "enabled", "active", "updatePending", "pending", "failed" })
				preparationProperties[name] = { { "type", "boolean" } };
			preparationProperties["enabled"]["description"] = "Background preparation defaults to true in all builds; DevBench can override it for this session.";
			for (const auto* name : { "submitted", "published", "discarded", "rejectedBuckets", "frames" })
				preparationProperties[name] = { { "type", "integer" }, { "minimum", 0 } };
			for (const auto* name : { "renderMilliseconds", "workerMilliseconds" })
				preparationProperties[name] = { { "type", "number" }, { "minimum", 0 } };
			document["outputSchema"]["properties"]["status"]["properties"]["grassOptimizations"]["properties"]["backgroundPreparation"] = {
				{ "type", "object" }, { "properties", std::move(preparationProperties) }
			};
			document["inputSchema"]["properties"]["requestTelemetry"] = {
				{ "type", "boolean" }, { "default", true },
				{ "description", "grass_telemetry only: false reads the previous publication without requesting another sample." }
			};
			document["description"] = std::string("grass_telemetry requests a lightweight render-thread GO resource snapshot (maximum 1 Hz) and returns the previous publication plus scene context. It also requests three frames of DevBench-only grassCollision input, shape coordinates, upload, shader-object readiness, native/batched VS b5/t100 bindings and bound vertex shader input reflection, plus one nonblocking GPU collision-texture readback and up to sixteen paired vertex-output samples from exact native/indirect grass draws. Vertex replays use the active shader and bindings, replacing only t100 with a zero field for the baseline; stream output prevents rasterization. Each replay is capped at 96 indices and 128 instances. Completed vertex samples compare camera-relative world and clip-space displacement, including nearby vertices; zero displacement with no nearby vertices is inconclusive. The texture reports pending/completed/failed/timed_out independently of the three-frame window; a completed readback proves GPU field contents, not visible bending. Read with requestTelemetry=false after the window to preserve the observations; null queue or update and drawBindings.observed=false mean those stages were not observed. dispatchIssued is CPU submission evidence, not completed GPU execution or visible bending. telemetry is null until grass renders; its frame and age identify stale data. ok acknowledges the request/read, not snapshot freshness. Work covers the last completed grass frame. The vertex probe adds bounded diagnostic GPU work while requested and must not be treated as a performance-neutral observation. It does not reset time/weather. ") + document["description"].get<std::string>();
			const json collisionCountSchema = { { "type", "integer" }, { "minimum", 0 } };
			const json collisionNumberSchema = { { "type", { "number", "null" } } };
			const auto collisionVectorSchema = [&](size_t a_size) {
				return json{ { "type", "array" }, { "minItems", a_size }, { "maxItems", a_size }, { "items", collisionNumberSchema } };
			};
			json collisionQueueProperties = {
				{ "frame", collisionCountSchema }, { "cameraPosition", collisionVectorSchema(3) },
				{ "shapeSamplesCameraRelative", { { "type", "array" }, { "maxItems", 8 }, { "items", collisionVectorSchema(4) },
													{ "description", "Up to eight collected camera-relative spheres as x,y,z,radius." } } }
			};
			for (const char* field : { "actorCandidates", "actorRoots", "shapeVisits", "shapeBoundFailures", "radiusRejected",
					 "playerShapes", "otherActorShapes", "boundingBoxes", "collisionShapes" })
				collisionQueueProperties[field] = collisionCountSchema;
			json collisionDrawProperties = {
				{ "observed", { { "type", "boolean" } } },
				{ "firstFrame", { { "type", { "integer", "null" } } } },
				{ "lastFrame", { { "type", { "integer", "null" } } } }
			};
			for (const char* field : { "nativeChecks", "batchedChecks", "missingConstantBuffer", "wrongConstantBuffer",
					 "missingTexture", "wrongTexture", "collisionShaderChecks", "missingCollisionShaderInputs",
					 "unidentifiedVertexShaders", "shaderReflectionFailures" })
				collisionDrawProperties[field] = collisionCountSchema;
			json collisionTextureProperties = {
				{ "status", { { "type", "string" }, { "enum", json::array({ "pending", "completed", "failed", "timed_out" }) } } }
			};
			for (const char* field : { "capturedFrame", "completedFrame", "hresult", "width", "height", "nonFiniteCells",
					 "nonzeroCells", "nearCameraNonzeroCells" })
				collisionTextureProperties[field] = collisionCountSchema;
			for (const char* field : { "maxNormalizedNearCamera", "minNormalizedHeight", "maxNormalizedHeight" })
				collisionTextureProperties[field] = collisionNumberSchema;
			document["outputSchema"]["properties"]["grassCollision"] = {
				{ "type", "object" }, { "properties", { { "schemaVersion", { { "type", "integer" }, { "const", 3 } } },
														  { "requestGeneration", collisionCountSchema },
														  { "loaded", { { "type", "boolean" } } }, { "enabled", { { "type", "boolean" } } },
														  { "requestedFrame", { { "type", { "integer", "null" } } } },
														  { "captureWindowFrames", { { "type", "integer" }, { "minimum", 1 } } },
														  { "queue", { { "type", { "object", "null" } }, { "properties", std::move(collisionQueueProperties) },
																		 { "description", "Latest collision input in the requested window; null means not observed." } } },
														  { "update", { { "type", { "object", "null" } }, { "properties", { { "frame", collisionCountSchema }, { "uploadedBoundingBoxes", collisionCountSchema }, { "uploadedShapes", collisionCountSchema }, { "shaderObjectAvailable", { { "type", "boolean" } } }, { "dispatchIssued", { { "type", "boolean" } } }, { "timeDelta", collisionNumberSchema }, { "cameraHeightDelta", collisionNumberSchema }, { "collisionStrength", collisionNumberSchema }, { "previousCollisionStrength", collisionNumberSchema }, { "posOffset", collisionVectorSchema(2) }, { "arrayOrigin", collisionVectorSchema(2) }, { "validMargin", collisionVectorSchema(2) } } }, { "description", "Collision upload and CPU dispatch submission; null means not observed. It does not prove GPU completion." } } },
														  { "texture", { { "type", { "object", "null" } }, { "properties", std::move(collisionTextureProperties) },
																		   { "description", "One asynchronous full collision-field readback per request. Near-camera statistics cover a 512-unit square; null means not observed. Completion does not prove visible bending." } } },
														  { "drawBindings", { { "type", "object" }, { "properties", std::move(collisionDrawProperties) },
																				{ "description", "Bound resources and vertex bytecode inputs immediately before grass draws. Unidentified shaders and reflection failures remain separate from shaders missing collision inputs." } } } } }
			};
			json vertexSampleProperties = {
				{ "status", { { "type", "string" }, { "enum", json::array({ "pending", "failed", "timed_out", "output_count_mismatch", "invalid_outputs", "empty", "completed" }) } } },
				{ "pass", { { "type", "string" }, { "enum", json::array({ "depth", "color" }) } } },
				{ "drawKind", { { "type", "string" }, { "enum", json::array({ "native", "indirect" }) } } },
				{ "example", { { "type", "object" }, { "properties", { { "collisionOnWorld", collisionVectorSchema(3) }, { "collisionOffWorld", collisionVectorSchema(3) },
																		 { "collisionOnClip", collisionVectorSchema(4) }, { "collisionOffClip", collisionVectorSchema(4) } } } } }
			};
			for (const char* field : { "capturedFrame", "completedFrame", "shaderDescriptor", "hresult", "sampledVertices",
					 "movedVertices", "nearCameraVertices", "nearCameraMovedVertices", "nonFiniteVertices" })
				vertexSampleProperties[field] = collisionCountSchema;
			for (const char* field : { "maxWorldDisplacement", "maxClipDisplacement", "minimumCameraDistance" })
				vertexSampleProperties[field] = collisionNumberSchema;
			document["outputSchema"]["properties"]["grassCollision"]["properties"]["vertexProbe"] = {
				{ "type", "object" }, { "properties", { { "schemaVersion", { { "type", "integer" }, { "const", 1 } } }, { "requestGeneration", collisionCountSchema }, { "maximumPairs", collisionCountSchema }, { "maximumIndices", collisionCountSchema }, { "maximumInstances", collisionCountSchema }, { "skippedPipelines", collisionCountSchema }, { "samples", { { "type", "array" }, { "maxItems", 16 }, { "items", { { "type", "object" }, { "properties", std::move(vertexSampleProperties) } } } } } } }, { "description", "Paired output of the active grass vertex shader with the real and zero collision field. Query counts must match before comparison; no nearby vertices or empty samples cannot establish a bending failure." }
			};
			document["outputSchema"]["properties"]["grassCollision"]["properties"]["vertexProbeError"] = {
				{ "type", { "string", "null" } }, { "description", "Explicit diagnostic exception; null means none recorded." }
			};
			document["outputSchema"]["properties"]["grassOptimizations"] = {
				{ "type", "object" }, { "properties", { { "telemetrySchema", { { "type", "integer" }, { "const", 1 } } },
														  { "telemetry", { { "type", { "object", "null" } }, { "description", "Latest retained snapshot; frame identifies grass work, ageMilliseconds measures time since publication. Byte counts are buffer payloads, not GPU residency. Viewport dimensions are null when main-depth culling did not run." } } },
														  { "telemetryError", { { "type", { "string", "null" } }, { "description", "snapshot_failed retains the preceding snapshot after a sampling failure." } } },
														  { "telemetryScope", { { "type", "string" } } } } }
			};
			document["outputSchema"]["properties"]["grassOptimizations"]["properties"]["backgroundPreparation"] =
				document["outputSchema"]["properties"]["status"]["properties"]["grassOptimizations"]["properties"]["backgroundPreparation"];
			document["outputSchema"]["properties"]["scene"] = {
				{ "type", "object" }, { "description", "Main-thread context at the DevBench call, independently framed from the asynchronous grass and profiler samples. Unavailable scene values are omitted." }
			};
			document["inputSchema"]["properties"]["visuals"]["properties"]["appearance"] = BuildAdaptiveBalanceAppearanceSchema();
			document["inputSchema"]["properties"]["visuals"]["properties"]["godrays"] = BuildAdaptiveBalanceGodraySchema();
			document["description"] = document["description"].get<std::string>() +
			                          " set_adaptive_balance_visuals accepts godrays.exterior and godrays.interior partial profiles (ShaftIntensity 0..3, Opacity 0..2, Saturation 0..4, CustomColorContribution/Red/Green/Blue 0..1). These bases work without lightingAdvanced; profile appearance adjustments compose afterward. Disabling Adaptive Balance bypasses both bases, profile tuning and volumetricClarity. Volumetric Lighting owns only coverage and rendering quality. Status global.godrays reports saved bases and effective.godrays reports composed tuning before renderer availability gates.";
			document["description"] = document["description"].get<std::string>() +
			                          " set_adaptive_balance_visuals accepts appearance.eyeSpecularBrightness and appearance.eyeBrightness in [0,5] (default 1). They scale eye highlights/reflections and overall eye lighting respectively, require Global lightingAdvanced, and remain neutral while Adaptive Balance is disabled.";
			document["description"] = document["description"].get<std::string>() +
			                          " set_adaptive_balance_visuals accepts appearance.auroraIntensity in [0,5] and appearance.auroraCurve in [0.1,4] (both default 1). They grade sky objects under the active aurora root, including replacement models, without changing alpha or other effects. Requires Global lightingAdvanced; neutral while Adaptive Balance is disabled and in object previews.";
			document["inputSchema"]["properties"]["depthOfField"] = MenuDepthOfFieldSettingsPolicy::Schema();
			const auto dofValues = document["inputSchema"]["properties"]["depthOfField"]["properties"]["sceneDof"]["properties"]["values"];
			const json liveDof = { { "oneOf", json::array({ dofValues, { { "type", "null" } } }) } };
			const json dofStatus = { { "type", "object" }, { "properties", { { "settings", MenuDepthOfFieldSettingsPolicy::Schema() },
																			   { "overridesActive", { { "type", "boolean" } } }, { "correctionActive", { { "type", "boolean" } } },
																			   { "sceneDataAvailable", { { "type", "boolean" } } }, { "underwaterDataAvailable", { { "type", "boolean" } } },
																			   { "scene", liveDof }, { "underwater", liveDof } } } };
			document["outputSchema"]["properties"]["depthOfField"] = dofStatus;
			document["outputSchema"]["properties"]["status"]["properties"]["adaptiveBalanceDepthOfField"] = dofStatus;
			json visualsStatus = {
				{ "type", "object" },
				{ "properties", { { "global", { { "type", "object" }, { "properties", json::object() } } },
									{ "effective", { { "type", "object" }, { "properties", json::object() } } } } }
			};
			const json claritySchema = {
				{ "type", "number" }, { "minimum", AdaptiveBalanceGodray::kClarityMin }, { "maximum", AdaptiveBalanceGodray::kClarityMax },
				{ "default", AdaptiveBalanceGodray::kClarityDefault },
				{ "description", "Global Atmospheric Clarity: 0 retains broad volumetric haze, 1 removes the estimated haze independently of godray intensity, opacity, saturation and tint. Depth-aware symmetric sampling rejects foreground silhouettes and smooth ramps as shafts. Local contrast separation; broad uniform beams can also fade. Vanilla distance fog, scene textures and lens flare are unchanged. Missing or invalid scene depth preserves the original lighting. Independent of lightingAdvanced; Adaptive Balance off makes the effective value 0. Does not enable volumetric lighting. Omission preserves the saved value." }
			};
			document["inputSchema"]["properties"]["visuals"]["properties"]["volumetricClarity"] = claritySchema;
			for (const auto* layer : { "global", "effective" })
				visualsStatus["properties"][layer]["properties"]["volumetricClarity"] = claritySchema;
			document["description"] = document["description"].get<std::string>() +
			                          " set_adaptive_balance_visuals accepts volumetricClarity in [0,1] (Atmospheric Clarity, default 0) to reduce broad haze independently of shaft tuning. All godray intensity, opacity and tint controls leave the haze contribution unchanged. Depth-aware local contrast separation protects foreground silhouettes; broad uniform beams can still fade. Missing depth preserves original lighting. Atmospheric Clarity works without lightingAdvanced.";
			const json moonReflectionSchema = {
				{ "type", "number" }, { "minimum", AdaptiveBrightness::kMoonReflectionStrengthMin }, { "maximum", AdaptiveBrightness::kMoonReflectionStrengthMax },
				{ "default", AdaptiveBrightness::kMoonReflectionStrengthDefault },
				{ "description", "Global direct moonlit specular strength for materials, wet surfaces and water. Zero removes these highlights, one preserves them, two doubles them. Blends through the active climate's dusk and dawn; neutral indoors and while Adaptive Balance is inactive. Material previews retain their lighting. Independent of lightingAdvanced, diffuse light, environment reflections and moon appearance." }
			};
			document["inputSchema"]["properties"]["visuals"]["properties"]["moonReflectionStrength"] = moonReflectionSchema;
			for (const auto* layer : { "global", "effective" })
				visualsStatus["properties"][layer]["properties"]["moonReflectionStrength"] = moonReflectionSchema;
			document["description"] = document["description"].get<std::string>() +
			                          " set_adaptive_balance_visuals accepts moonReflectionStrength in [0,2] (default 1), stages it without saving or enabling Adaptive Balance, and rejects invalid values before mutation. The global slider scales direct nighttime specular highlights on materials, wet surfaces and water using the active climate's dusk and dawn; diffuse lighting and environment reflections are independent. status.adaptiveBalanceVisuals reports configured and effective strengths.";
			document["outputSchema"]["properties"]["adaptiveBalanceVisuals"] = visualsStatus;
			document["outputSchema"]["properties"]["status"]["properties"]["adaptiveBalanceVisuals"] = visualsStatus;
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
