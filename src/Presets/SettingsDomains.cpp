#include "SettingsDomains.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <map>
#include <set>

namespace Presets
{
	namespace
	{
		// New serialized controls stay local until their ownership has been reviewed.
		const std::map<std::string_view, std::set<std::string_view>> profileKeys{
			{ "AdaptiveBrightness", { "enabled", "bloomEnabled", "useAmbientEffectLighting", "godrayFinalBrightness", "volumetricClarity", "godrays", "moonReflectionStrength", "dayStartHour", "nightStartHour", "transitionHours", "globalProfile", "depthOfField", "profiles", "locationOverrides" } },
			{ "SubsurfaceScattering", { "EnableSubsurfaceScattering", "EnableCharacterLighting", "CharacterLightingStrength", "SSMode", "BaseProfile", "HumanProfile", "BurleySamples", "MeanFreePathBase", "MeanFreePathHuman", "HumanMaleSSSIntensity", "HumanMaleSSSSaturation", "HumanMaleSSSBrightness", "HumanMaleSSSBaseSaturation", "HumanFemaleSSSIntensity", "HumanFemaleSSSSaturation", "HumanFemaleSSSBrightness", "HumanFemaleSSSBaseSaturation" } },
			{ "HairSpecular", { "Enabled", "HairGlossiness", "SpecularMult", "DiffuseMult", "EnableTangentShift", "PrimaryTangentShift", "SecondaryTangentShift", "HairSaturation", "SpecularIndirectMult", "DiffuseIndirectMult", "BaseColorMult", "Transmission", "EnableSelfShadow", "SelfShadowStrength", "SelfShadowExponent", "SelfShadowScale", "HairMode" } },
			{ "CloudShadows", { "Enabled", "Opacity" } },
			{ "FoliageLighting", { "EnableFoliageScattering", "EnableFoliageAmbientBoost", "EnableFoliageAmbientFlip", "FoliageAmbientAmount", "EnableGrassScattering" } },
			{ "GrassLighting", { "Glossiness", "SpecularStrength", "SubsurfaceScatteringAmount", "OverrideComplexGrassSettings", "BasicGrassBrightness", "EnableWrappedLighting", "ComplexGrassThreshold", "Enabled" } },
			{ "LinearLighting", { "enableLinearLighting", "DisableInInteriors", "DisableInExteriors", "lightGamma", "colorGamma", "emitColorGamma", "glowmapGamma", "ambientGamma", "fogGamma", "fogAlphaGamma", "effectGamma", "effectAlphaGamma", "skyGamma", "waterGamma", "vlGamma", "ambientMult", "vanillaDiffuseColorMult", "emitColorMult", "glowmapMult", "effectLightingMult", "membraneEffectMult", "bloodEffectMult", "projectedEffectMult", "deferredEffectMult", "otherEffectMult" } },
			{ "ExtendedTranslucency", { "Enabled", "AlphaMode", "AlphaReduction", "AlphaSoftness", "AlphaStrength", "SkinnedOnly" } },
			{ "InteriorSun", { "Enabled", "ForceDoubleSidedRendering", "ForceSingleShadowCascade", "InteriorShadowDistance" } },
			{ "InverseSquareLighting", { "Enabled" } },
			{ "LODBlending", { "LODTerrainBrightness", "LODObjectBrightness", "LODObjectSnowBrightness", "DisableTerrainVertexColors", "LODTerrainGamma", "LODObjectGamma", "LODObjectSnowGamma", "WaterReflectionStrength" } },
			{ "SkySync", { "Enabled", "UseAlternateSunPath", "EnableSunLensFlare", "MoonLightSource", "SunPath", "CustomAngle", "SunriseBeginOffset", "SunriseEndOffset", "SunsetBeginOffset", "SunsetEndOffset", "MinShadowElevation", "DimSunlightUnderHorizon", "HorizonFadeHours" } },
			{ "ExtendedMaterials", { "EnableComplexMaterial", "EnableParallax", "EnableTerrain", "EnableHeightBlending", "EnableShadows", "EnableParallaxWarpingFix", "ParallaxStrength" } },
			{ "TerrainShadows", { "EnableTerrainShadow" } },
			{ "TerrainHelper", { "EnableTerrainHelper" } },
			{ "TerrainVariation", { "enableLODTerrainTilingFix", "enableMeshSupport" } },
			{ "VolumetricShadows", { "Enabled" } },
			{ "DynamicCubemaps", { "EnabledSSR", "EnabledCreator", "CubemapResolution" } },
			{ "TerrainBlending", { "Enabled", "TerrainCullDistance", "BlendStrength", "LandscapeSeams" } },
			{ "VolumetricLighting", { "ExteriorEnabled", "DisableWeatherInteractionDuringRain", "ExteriorQuality", "ExteriorCustomSize", "InteriorEnabled", "InteriorQuality", "InteriorCustomSize" } },
			{ "FurShells", { "Enabled", "ShellCount", "Length", "BodyLength", "Droop", "RootThreshold", "TipThreshold", "RootDarkening", "ShellColor", "FadeStart", "FadeEnd", "HideCoveredFur", "OverlayFur", "MinFurPixels", "EnableWind", "WindStrength", "EnableWetFur", "WetStrength", "EnableTouch", "TouchStrength", "EnableDentRecovery", "FitHandCapsules", "PreciseContactSelection", "EnableHeldObjects", "ContactLimit", "ContactRange", "EnableInertia", "InertiaStrength", "InertiaPreset", "InertiaFrequency", "InertiaGain", "InertiaLengthExponent", "InertiaDroopExponent", "InertiaMaxBend", "ShowTouchEdges", "DistanceShellBudget" } },
			{ "WetnessEffects", { "EnableWetnessEffects", "MaxRainWetness", "MaxPuddleWetness", "MaxShoreWetness", "ShoreRange", "PuddleRadius", "PuddleMaxAngle", "PuddleMinWetness", "MinRainWetness", "SkinWetness", "WeatherTransitionSpeed", "EnableRaindropFx", "EnableSplashes", "EnableRipples", "EnableVanillaRipples", "RaindropFxRange", "RaindropGridSize", "RaindropInterval", "RaindropChance", "SplashesLifetime", "SplashesStrength", "SplashesMinRadius", "SplashesMaxRadius", "RippleStrength", "RippleRadius", "RippleBreadth", "RippleLifetime" } },
			{ "Wetterness", { "EnableWetterness", "MaxRainWetness", "MaxPuddleWetness", "MaxShoreWetness", "ShoreRange", "PuddleRadiusWorldUnits", "PuddleMaxAngle", "PuddleMinWetness", "MinRainWetness", "SkinWetness", "WeatherTransitionSpeed", "StoneDryingMultiplier", "DirtDryingMultiplier", "GrassDryingMultiplier", "EnableRaindropFx", "EnableSplashes", "EnableRipples", "EnableModernWetReflection", "EnableLegacyWetReflection", "RaindropFxRangeWorldUnits", "RaindropGridSize", "RaindropInterval", "RaindropChance", "SplashesLifetime", "SplashesStrength", "SplashesMinRadius", "SplashesMaxRadius", "RippleStrength", "RippleRadius", "RippleBreadth", "RippleLifetime", "PostRainPuddleWaterStrength", "RaindropTransitionFalloff", "WetDarkeningStrength", "WetHighlightReduction", "EnableForwardReflectionBias", "EnableVanillaReflectionCompensation", "WetFilmSpecularFloorScale", "RainContactWetnessScale", "ModernWetIndirectSpecularScale", "LegacyWetIndirectSpecularScale", "PuddleDryingHours", "PuddleLayout", "RainReflectionBalance", "PuddleSkyReflectionScale", "PostRainWaterClarity", "ShorePersistentDarkeningStrength", "WetnessDistanceFadeRange", "RainGrassGlossiness", "RainGrassSpecularStrength", "RainGrassDarkening", "PuddleMaskMode", "RestrictPuddleSlopes", "PreserveWetnessHighlights", "EnableWeatherDrivenDryingModel" } },
			{ "SnowCover", { "Enable", "EnableExpensiveFoliage", "AffectHavok", "AffectFloraTint", "SnowHeightOffset", "RooftopSnow", "RooftopHeight", "RooftopExposure", "EnableGlints", "EnableRooftops", "EnableFoliageTint", "EnableBoundsCulling", "FireMeltEnabled", "FireMeltRadiusScale", "FireMeltStrength", "MeltWetnessEnabled" } },
			{ "ImageBasedLighting", { "EnableIBL", "PreserveFogLuminance", "UseStaticIBL", "DALCAmount", "EnvIBLScale", "SkyIBLScale", "EnvIBLSaturation", "SkyIBLSaturation", "FogAmount", "DALCMode", "DisableInInteriors", "DisableInWorldMap", "DisableInLoadingScreen", "CaptureWeatherBaselineOnSliderChange" } },
			{ "ScreenSpaceGI", { "Enabled", "EnableGI", "EnableExperimentalSpecularGI", "EnableVanillaSSAO", "AOInteriorsOnly", "ILInteriorsOnly", "NumSlices", "NumSteps", "EnableAdaptiveSampling", "ResolutionMode", "ResourceProfile", "VRCullDistance", "EnableFoveated", "ExperimentalOCUEffectFoveation", "EnableStereoSync", "UseStereoReproject", "MinScreenRadius", "AORadius", "GIRadius", "Thickness", "DepthFadeRange", "GISaturation", "GIDistanceCompensation", "AOPower", "GIStrength", "EnableTemporalDenoiser", "EnableBlur", "DepthDisocclusion", "NormalDisocclusion", "MaxAccumFrames", "BlurRadius", "DistanceNormalisation" } },
			{ "ScreenSpaceShadows", { "Enable", "SampleCount", "VRBaseSamplesAtReference", "VRCullDistance", "EnableFoveated", "SurfaceThickness", "BilinearThreshold", "ShadowContrast" } },
			{ "Skylighting", { "MaxZenith", "MinDiffuseVisibility", "MinSpecularVisibility", "ProbeFieldSize", "ProbeGridQuality", "EnableSkylighting", "EnableIncrementalProbeUpdates", "StableSliceCount", "EnableReducedUpdateFrequency", "OcclusionUpdateInterval", "ProbeUpdateInterval", "IncludeMarkedRoofOccluders" } },
			{ "DetailMaps", { "Enabled", "Tiling", "AlbedoStrength", "NormalStrength", "RoughnessStrength", "FadeStart", "FadeEnd" } },
			{ "LightLimitFix", { "ParticleLightsSaturation", "ParticleBrightness", "ParticleRadius", "BillboardBrightness", "BillboardRadius", "JsonPlacedLightIntensity", "EnableContactShadows", "ContactShadowsInteriorsOnly", "EnableParticleContactShadows" } },
			{ "Advanced", { "Refraction Scale", "PBR Metal Reflection Scale", "PBR Metal Highlight Scale" } },
		};

		bool OneOf(std::string_view value, std::initializer_list<std::string_view> choices)
		{
			return std::find(choices.begin(), choices.end(), value) != choices.end();
		}

		std::string_view Root(std::string_view path)
		{
			if (path.starts_with('/'))
				path.remove_prefix(1);
			return path.substr(0, path.find('/'));
		}

		Json Select(std::string_view feature, const Json& value, SettingsDomain domain, const std::string& path)
		{
			if (value.is_object()) {
				Json result = Json::object();
				for (const auto& [key, item] : value.items()) {
					auto selected = Select(feature, item, domain, path + "/" + key);
					if (!selected.is_discarded())
						result[key] = std::move(selected);
				}
				if (!result.empty() || Classify(feature, path) == domain)
					return result;
			} else if (value.is_array()) {
				Json result = Json::array();
				bool selectedAny = value.empty() && Classify(feature, path) == domain;
				for (const auto& item : value) {
					auto selected = Select(feature, item, domain, path + "/*");
					selectedAny |= !selected.is_discarded();
					result.push_back(selected.is_discarded() ? Json::object() : std::move(selected));
				}
				if (selectedAny)
					return result;
			} else if (Classify(feature, path) == domain) {
				return value;
			}
			return Json(Json::value_t::discarded);
		}

		Json RestoreLocal(std::string_view feature, const Json& authored, const Json& previous,
			const Json& fallback, const std::string& path)
		{
			if (authored.is_object()) {
				Json result = authored;
				const auto& source = previous.is_object() ? previous : fallback;
				if (source.is_object()) {
					for (const auto& [key, value] : source.items()) {
						const auto child = path + "/" + key;
						if (!result.contains(key)) {
							for (auto domain : { SettingsDomain::Performance, SettingsDomain::Local }) {
								auto retained = Select(feature, value, domain, child);
								if (!retained.is_discarded())
									Overlay(result[key], retained);
							}
						}
					}
				}
				for (auto& [key, value] : result.items()) {
					const auto old = source.is_object() && source.contains(key) ? source.at(key) : Json{};
					const auto seed = fallback.is_object() && fallback.contains(key) ? fallback.at(key) : Json{};
					value = RestoreLocal(feature, value, old, seed, path + "/" + key);
				}
				return result;
			}
			if (authored.is_array()) {
				Json result = Json::array();
				std::map<std::string, const Json*> locations;
				if (path == "/locationOverrides" && previous.is_array())
					for (const auto& entry : previous)
						if (entry.is_object())
							locations.emplace(entry.value("key", ""), &entry);
				for (size_t i = 0; i < authored.size(); ++i) {
					Json old;
					if (path == "/locationOverrides" && previous.is_array()) {
						if (const auto found = locations.find(authored[i].value("key", "")); found != locations.end())
							old = *found->second;
					} else if (previous.is_array() && i < previous.size())
						old = previous[i];
					Json seed = fallback;
					if (fallback.is_array())
						seed = fallback.empty() ? Json{} : fallback[std::min(i, fallback.size() - 1)];
					result.push_back(RestoreLocal(feature, authored[i], old, seed, path + "/*"));
				}
				return result;
			}
			if (Classify(feature, path) != SettingsDomain::Appearance && !previous.is_null())
				return previous;
			return authored;
		}

		Json Personal(std::string_view feature, const Json& value, const Json& baseline, const std::string& path)
		{
			if (value.is_object()) {
				Json result = Json::object();
				for (const auto& [key, child] : value.items()) {
					const auto old = baseline.is_object() && baseline.contains(key) ? baseline.at(key) : Json{};
					auto edit = Personal(feature, child, old, path + "/" + key);
					if (!edit.is_discarded())
						result[key] = std::move(edit);
				}
				if (!result.empty() || (value.empty() && Classify(feature, path) != SettingsDomain::Appearance))
					return result;
			} else {
				if (value.is_array()) {
					// Arrays are atomic in the upstream format; partial quality-only arrays would erase the art.
					for (auto domain : { SettingsDomain::Performance, SettingsDomain::Local })
						if (!Select(feature, value, domain, path).is_discarded())
							return value;
				} else if (Classify(feature, path) != SettingsDomain::Appearance)
					return value;
				if (!Difference(Json{ { "value", value } }, Json{ { "value", baseline } }).empty())
					return value;
			}
			return Json(Json::value_t::discarded);
		}
	}

	bool HasProfileSettings(std::string_view feature)
	{
		return profileKeys.contains(feature);
	}

	SettingsDomain Classify(std::string_view feature, std::string_view path)
	{
		const auto key = Root(path);
		const auto found = profileKeys.find(feature);
		if (found == profileKeys.end() || !found->second.contains(key))
			return SettingsDomain::Local;
		if (OneOf(key, { "Enabled", "Enable", "enabled", "EnableWetterness", "EnableWetnessEffects", "EnableSkylighting", "EnableSubsurfaceScattering", "EnableTerrainHelper", "EnableTerrainShadow", "EnableIBL" }))
			return SettingsDomain::Performance;
		if (feature == "InteriorSun" && key == "ForceSingleShadowCascade")
			return SettingsDomain::Performance;
		if (feature == "AdaptiveBrightness") {
			if (path.ends_with("/advanced") || path.ends_with("/bloomAdvanced") || path.ends_with("/waterAdvanced"))
				return SettingsDomain::Local;
			if (path.ends_with("/ParallaxQuality"))
				return SettingsDomain::Performance;
			return SettingsDomain::Appearance;
		}
		if (feature == "SubsurfaceScattering")
			return key == "BurleySamples" ? SettingsDomain::Performance : SettingsDomain::Appearance;
		if (OneOf(feature, { "HairSpecular", "CloudShadows", "FoliageLighting", "GrassLighting", "LinearLighting",
							   "ExtendedTranslucency", "InteriorSun", "InverseSquareLighting", "LODBlending", "SkySync",
							   "ExtendedMaterials", "TerrainShadows", "TerrainHelper", "TerrainVariation", "VolumetricShadows" }))
			return SettingsDomain::Appearance;
		if (feature == "DynamicCubemaps")
			return key == "CubemapResolution" ? SettingsDomain::Performance : SettingsDomain::Appearance;
		if (feature == "TerrainBlending")
			return key == "TerrainCullDistance" ? SettingsDomain::Performance : SettingsDomain::Appearance;
		if (feature == "DetailMaps")
			return key == "FadeStart" || key == "FadeEnd" ? SettingsDomain::Performance : SettingsDomain::Appearance;
		if (feature == "VolumetricLighting") {
			if (OneOf(key, { "ExteriorQuality", "InteriorQuality", "ExteriorCustomSize", "InteriorCustomSize" }))
				return SettingsDomain::Performance;
			return SettingsDomain::Appearance;
		}
		if (feature == "LightLimitFix") {
			if (OneOf(key, { "ParticleLightsSaturation", "ParticleBrightness", "ParticleRadius", "BillboardBrightness", "BillboardRadius",
							   "JsonPlacedLightIntensity", "EnableContactShadows", "ContactShadowsInteriorsOnly", "EnableParticleContactShadows" }))
				return SettingsDomain::Appearance;
			return SettingsDomain::Local;
		}
		if (feature == "FurShells") {
			if (OneOf(key, { "ShellCount", "FadeStart", "FadeEnd", "MinFurPixels", "ContactLimit", "ContactRange", "DistanceShellBudget" }))
				return SettingsDomain::Performance;
			if (key == "ShowTouchEdges" || key == "Overrides")
				return SettingsDomain::Local;
			return SettingsDomain::Appearance;
		}
		if (OneOf(feature, { "Wetterness", "WetnessEffects" })) {
			if (key == "DebugSettings")
				return SettingsDomain::Local;
			if (OneOf(key, { "RaindropFxRange", "RaindropFxRangeWorldUnits", "WetternessFadeRange", "WetnessDistanceFadeRange", "RaindropGridSize", "RaindropInterval", "RaindropChance", "SplashesLifetime", "RippleLifetime" }))
				return SettingsDomain::Performance;
			return SettingsDomain::Appearance;
		}
		if (feature == "SnowCover") {
			if (key == "EnableBoundsCulling" || key == "EnableExpensiveFoliage")
				return SettingsDomain::Performance;
			if (key == "AffectHavok" || key == "SurvivalEnabled")
				return SettingsDomain::Local;
			return SettingsDomain::Appearance;
		}
		if (feature == "ImageBasedLighting")
			return key == "CaptureWeatherBaselineOnSliderChange" ? SettingsDomain::Local : SettingsDomain::Appearance;
		if (feature == "ScreenSpaceGI") {
			if (OneOf(key, { "NumSlices", "NumSteps", "EnableAdaptiveSampling", "ResolutionMode", "ResourceProfile",
							   "VRCullDistance", "EnableFoveated", "ExperimentalOCUEffectFoveation", "EnableStereoSync", "UseStereoReproject",
							   "EnableTemporalDenoiser", "EnableBlur", "DepthDisocclusion", "NormalDisocclusion", "MaxAccumFrames", "BlurRadius" }))
				return SettingsDomain::Performance;
			return SettingsDomain::Appearance;
		}
		if (feature == "ScreenSpaceShadows") {
			if (OneOf(key, { "SampleCount", "VRBaseSamplesAtReference", "VRCullDistance", "EnableFoveated", "EnableStereoSync", "UseStereoReproject" }))
				return SettingsDomain::Performance;
			return SettingsDomain::Appearance;
		}
		if (feature == "Skylighting") {
			if (OneOf(key, { "ProbeGridQuality", "ProbeFieldSize", "IncludeMarkedRoofOccluders", "EnableIncrementalProbeUpdates", "StableSliceCount", "EnableReducedUpdateFrequency", "OcclusionUpdateInterval", "ProbeUpdateInterval" }))
				return SettingsDomain::Performance;
			return SettingsDomain::Appearance;
		}
		if (feature == "Advanced" && OneOf(key, { "Refraction Scale", "PBR Metal Reflection Scale", "PBR Metal Highlight Scale" }))
			return SettingsDomain::Appearance;
		return SettingsDomain::Local;
	}

	Json SelectDomain(std::string_view feature, const Json& settings, SettingsDomain domain)
	{
		auto result = Select(feature, settings, domain, "");
		return result.is_discarded() ? Json::object() : result;
	}

	void Overlay(Json& target, const Json& patch)
	{
		if (patch.is_object() && target.is_object()) {
			for (const auto& [key, value] : patch.items()) Overlay(target[key], value);
		} else
			target = patch;
	}

	Json ReplaceAppearance(std::string_view feature, const Json& current, const Json& appearance)
	{
		Json fallback = current;
		if (feature == "AdaptiveBrightness" && current.contains("globalProfile"))
			fallback["locationOverrides"] = Json::array({ Json{ { "profile", current["globalProfile"] } } });
		return RestoreLocal(feature, appearance, current, fallback, "");
	}

	Json PersonalSettings(std::string_view feature, const Json& current, const Json& baseline)
	{
		auto result = Personal(feature, current, baseline, "");
		return result.is_discarded() ? Json::object() : result;
	}

	Json Difference(const Json& current, const Json& baseline)
	{
		Json edits = Json::object();
		for (const auto& [key, value] : current.items()) {
			if (!baseline.contains(key)) {
				edits[key] = value;
				continue;
			}
			const auto& old = baseline.at(key);
			if (value.is_object() && old.is_object()) {
				auto nested = Difference(value, old);
				if (!nested.empty())
					edits[key] = std::move(nested);
			} else if (value.is_number() && old.is_number()) {
				const auto number = old.get<double>();
				if (std::abs(value.get<double>() - number) > std::max(1e-6, std::abs(number) * 1e-5))
					edits[key] = value;
			} else if (value != old)
				edits[key] = value;
		}
		return edits;
	}

	void RemoveCovered(Json& edits, const Json& patch)
	{
		for (const auto& [key, value] : patch.items()) {
			if (!edits.contains(key))
				continue;
			if (value.is_object() && edits[key].is_object()) {
				RemoveCovered(edits[key], value);
				if (edits[key].empty())
					edits.erase(key);
			} else
				edits.erase(key);
		}
	}

	bool ValidatePatch(const Json& patch, const Json& schema, std::string& error, const std::string& path)
	{
		if (std::count(path.begin(), path.end(), '/') > 64) {
			error = "Settings are nested too deeply";
			return false;
		}
		if (patch.is_object() && schema.is_object()) {
			for (const auto& [key, value] : patch.items()) {
				if (!schema.contains(key)) {
					error = "Unrecognized setting: " + path + "/" + key;
					return false;
				}
				if (!ValidatePatch(value, schema.at(key), error, path + "/" + key))
					return false;
			}
			return true;
		}
		if (patch.is_array() && schema.is_array()) {
			if (patch.size() > 4096) {
				error = "Too many entries: " + path;
				return false;
			}
			if (!path.ends_with("/locationOverrides") && patch.size() != schema.size()) {
				error = "Incorrect array length: " + path;
				return false;
			}
			std::set<std::string> locations;
			for (size_t i = 0; i < patch.size(); ++i) {
				if (schema.empty()) {
					error = "No entry schema available: " + path;
					return false;
				}
				if (path.ends_with("/locationOverrides")) {
					const auto& entry = patch[i];
					if (!entry.is_object() || !entry.contains("key") || !entry["key"].is_string() ||
						entry["key"].get_ref<const std::string&>().empty() || !locations.insert(entry["key"].get<std::string>()).second) {
						error = "Missing or duplicate location key: " + path;
						return false;
					}
				}
				if (!ValidatePatch(patch[i], schema[std::min(i, schema.size() - 1)], error, path + "/" + std::to_string(i)))
					return false;
			}
			return true;
		}
		const bool number = patch.is_number() && schema.is_number();
		if (number && (path == "/SubsurfaceScattering/SSMode" || path == "/HairSpecular/HairMode") &&
			(patch.get<double>() < 0.0 || patch.get<double>() > 1.0)) {
			error = "Unsupported rendering mode: " + path;
			return false;
		}
		if (number && (path == "/WetnessEffects/RaindropGridSize" || path == "/WetnessEffects/RaindropInterval" || path == "/WetnessEffects/RippleLifetime") && patch.get<double>() <= 0.0) {
			error = "Expected a positive value: " + path;
			return false;
		}
		if ((number && (!std::isfinite(patch.get<double>()) || std::abs(patch.get<double>()) > 1e9)) ||
			(schema.is_number_unsigned() && number && patch.get<double>() < 0.0) ||
			(!number && patch.type() != schema.type()) || patch.is_null() ||
			(schema.is_number_integer() && !patch.is_number_integer()) ||
			(patch.is_string() && patch.get_ref<const std::string&>().size() > 4096)) {
			error = "Invalid setting type or value: " + path;
			return false;
		}
		return true;
	}
}
