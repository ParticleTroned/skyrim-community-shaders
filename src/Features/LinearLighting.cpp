#include "LinearLighting.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include <cmath>

#include "../I18n/I18n.h"
#include "AdaptiveBrightness.h"
#include "LocationContext.h"
#include "State.h"

#include "RE/B/BGSDefaultObjectManager.h"

#define I18N_KEY_PREFIX "feature.linear_lighting."

namespace
{
	constexpr uint kWhiteDiffuseCategoryObject = 0;
	constexpr uint kWhiteDiffuseCategoryAnimal = 1;
	constexpr uint32_t kPerGeometryCBRegister = 8;

	bool IsAnimalGeometry(const RE::BSGeometry* a_geometry)
	{
		const auto ref = a_geometry ? a_geometry->GetUserData() : nullptr;
		const auto actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor)
			return false;

		return actor->HasKeywordWithType(RE::DefaultObjectID::kKeywordAnimal) ||
		       actor->HasKeywordWithType(RE::DefaultObjectID::kKeywordHorse);
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLighting::Settings,
	enableLinearLighting,
	DisableInInteriors,
	DisableInExteriors,
	lightGamma,
	colorGamma,
	emitColorGamma,
	glowmapGamma,
	ambientGamma,
	fogGamma,
	fogAlphaGamma,
	effectGamma,
	effectAlphaGamma,
	skyGamma,
	waterGamma,
	vlGamma,
	vanillaDiffuseColorMult,
	ambientMult,
	emitColorMult,
	glowmapMult,
	whiteDiffuseMult,
	animalWhiteDiffuseMult,
	effectLightingMult,
	membraneEffectMult,
	bloodEffectMult,
	projectedEffectMult,
	deferredEffectMult,
	otherEffectMult)

void LinearLighting::SanitizeSettings(Settings& a_settings)
{
	a_settings.enableLinearLighting = a_settings.enableLinearLighting != 0;
	a_settings.DisableInInteriors = a_settings.DisableInInteriors != 0;
	a_settings.DisableInExteriors = a_settings.DisableInExteriors != 0;
}

void LinearLighting::DrawSettings()
{
	SanitizeSettings(settings);
	constexpr float kGammaMin = 0.1f;
	constexpr float kGammaMax = 3.0f;
	constexpr float kMultiplierMin = 0.0f;
	constexpr float kMultiplierMax = 10.0f;
	constexpr float kAmbientMultiplierMax = 10.0f;

	MenuUI::SettingsPage page("LinearLighting", {
													{ "coverage", T("feature.linear_lighting.ui_coverage", "Coverage"), T("feature.linear_lighting.ui_choose_where_linear_lighting_is_enabled", "Choose where linear lighting is enabled."), T("feature.linear_lighting.ui_interior_and_exterior_response", "Interior and exterior response"), true, true, T("feature.linear_lighting.ui_choose_coverage_and_colour", "Choose coverage and colour") },
													{ "colour", T("feature.linear_lighting.ui_colour", "Colour"), T("feature.linear_lighting.ui_refine_brightness_response_after_enabling_linear_lighting", "Refine brightness response after enabling linear lighting."), T("feature.linear_lighting.ui_brightness_and_colour_response", "Brightness and colour response"), true, true, nullptr },
													{ "lighting", T("feature.linear_lighting.ui_lighting", "Lighting"), T("feature.linear_lighting.ui_refine_the_contribution_from_each_light_source", "Refine the contribution from each light source."), T("feature.linear_lighting.ui_individual_light_contributions", "Individual light contributions"), true, true, T("feature.linear_lighting.ui_balance_light_sources", "Balance light sources") },
												});

	if (page.Is("coverage")) {
		Util::UIntCheckbox(T("feature.linear_lighting.disable_in_interiors", "Disable in interiors"), settings.DisableInInteriors);
		Util::UIntCheckbox(T("feature.linear_lighting.disable_in_exteriors", "Disable in exteriors"), settings.DisableInExteriors);
	}

	if (page.Is("colour")) {
		MenuUI::DetailGrid colourGrid("colourGrid", 2, ImGui::GetFontSize() * 32);
		const Util::Widgets::ControlLayout controls(true);
		colourGrid.Next();
		MenuUI::SectionHeading(T("feature.linear_lighting.ui_light_and_surfaces", "Light and surfaces"));
		Util::Widgets::SliderFloat(T("feature.linear_lighting.ambient_gamma", "Ambient Gamma"), &settings.ambientGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.ui_colour_gamma", "Colour Gamma"), &settings.colorGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.effect_gamma", "Effect Gamma"), &settings.effectGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.effect_transparency_gamma", "Effect Transparency Gamma"), &settings.effectAlphaGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.ui_emissive_colour_gamma", "Emissive Colour Gamma"), &settings.emitColorGamma, kGammaMin, kGammaMax, "%.2f");
		colourGrid.Next();
		MenuUI::SectionHeading(T("feature.linear_lighting.ui_atmosphere_and_effects", "Atmosphere and effects"));
		Util::Widgets::SliderFloat(T("feature.linear_lighting.fog_gamma", "Fog Gamma"), &settings.fogGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.fog_transparency_gamma", "Fog Transparency Gamma"), &settings.fogAlphaGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.glowmap_gamma", "Glowmap Gamma"), &settings.glowmapGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.light_gamma", "Light Gamma"), &settings.lightGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.sky_gamma", "Sky Gamma"), &settings.skyGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.vl_gamma", "Volumetric Lighting Gamma"), &settings.vlGamma, kGammaMin, kGammaMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.water_gamma", "Water Gamma"), &settings.waterGamma, kGammaMin, kGammaMax, "%.2f");
	}

	if (page.Is("lighting")) {
		Util::Widgets::SliderFloat(T("feature.linear_lighting.ambient_multiplier", "Ambient Multiplier"), &settings.ambientMult, kMultiplierMin, kAmbientMultiplierMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.vanilla_diffuse_color_multiplier", "Vanilla Diffuse Color Multiplier"), &settings.vanillaDiffuseColorMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.emissive_color_multiplier", "Emissive Color Multiplier"), &settings.emitColorMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.glowmap_multiplier", "Glowmap Multiplier"), &settings.glowmapMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::Widgets::SliderFloat(T("feature.linear_lighting.effect_lighting_multiplier", "Effect Lighting Multiplier"), &settings.effectLightingMult, kMultiplierMin, kMultiplierMax, "%.2f");

		if (ImGui::TreeNodeEx(T("feature.linear_lighting.effects", "Effects"))) {
			Util::Widgets::SliderFloat(T("feature.linear_lighting.blood_effects_multiplier", "Blood Effects Multiplier"), &settings.bloodEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::Widgets::SliderFloat(T("feature.linear_lighting.deferred_effects_multiplier", "Deferred Effects Multiplier"), &settings.deferredEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::Widgets::SliderFloat(T("feature.linear_lighting.membrane_effects_multiplier", "Membrane Effects Multiplier"), &settings.membraneEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::Widgets::SliderFloat(T("feature.linear_lighting.projected_effects_multiplier", "Projected Effects Multiplier"), &settings.projectedEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::Widgets::SliderFloat(T("feature.linear_lighting.other_effects_multiplier", "Other Effects Multiplier"), &settings.otherEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			ImGui::TreePop();
		}
	}
}

void LinearLighting::DrawEssentialSettings()
{
	SanitizeSettings(settings);
	Util::UIntCheckbox(T("feature.linear_lighting.enable", "Enable"), settings.enableLinearLighting);
}

void LinearLighting::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	const bool availableInCurrentCell = !IsDisabledForCurrentCell();
	ImGui::BeginDisabled(!availableInCurrentCell);
	Util::UIntCheckbox(T("feature.linear_lighting.enable", "Enable"), settings.enableLinearLighting);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T("feature.linear_lighting.disabling_bypasses_linear_color_space_convers_23e748ae", "Disabling bypasses linear color-space conversions and per-geometry updates."));
	}
	if (!availableInCurrentCell)
		ImGui::TextDisabled(T("feature.linear_lighting.linear_lighting_is_disabled_for_the_current_l_ba7e0800", "Linear Lighting is disabled for the current location type."));
}

json LinearLighting::CapturePerformanceSettingsState() const
{
	return {
		{ "enableLinearLighting", settings.enableLinearLighting != 0 }
	};
}

bool LinearLighting::IsPerformanceCostMeasurementEnabled() const
{
	return IsRuntimeEnabled();
}

void LinearLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void LinearLighting::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void LinearLighting::RestoreDefaultSettings()
{
	settings = {};
}

void LinearLighting::SetupResources()
{
	PerGeometryCB = new ConstantBuffer(ConstantBufferDesc<PerGeometryData>(), "LinearLighting::PerGeometry");
}

void LinearLighting::Prepass()
{
	dirLightMult = 1.0f;
	if (!IsRuntimeEnabled())
		return;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	if (!imageSpaceManager)
		return;

	dirLightMult = imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale;
}

struct LinearLighting::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			func(This, Pass, RenderFlags);
			globals::features::linearLighting.BSLightingShader_SetupGeometry(Pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		logger::info("[LinearLighting] Installed hooks - BSLightingShader_SetupGeometry");
	}
};

void LinearLighting::PostPostLoad()
{
	LinearLighting::Hooks::Install();
}

LinearLighting::PerFrameData LinearLighting::GetCommonBufferData()
{
	const bool linearLightingEnabled = IsRuntimeEnabled();
	const bool adaptiveBrightnessEnabled = globals::features::adaptiveBrightness.IsRuntimeEnabled();
	const auto adaptiveBalanceSettings = globals::features::adaptiveBrightness.GetEffectiveLinearLightingSettings(
		settings,
		linearLightingEnabled);
	const auto& effectiveSettings = adaptiveBalanceSettings.settings;

	auto data = PerFrameData{};
	data.enableLinearLighting = linearLightingEnabled;
	data.isDirLightLinear = isDirLightLinear;
	data.dirLightMult = dirLightMult;
	data.lightGamma = effectiveSettings.lightGamma;
	data.colorGamma = effectiveSettings.colorGamma;
	data.emitColorGamma = effectiveSettings.emitColorGamma;
	data.glowmapGamma = effectiveSettings.glowmapGamma;
	data.ambientGamma = effectiveSettings.ambientGamma;
	data.fogGamma = effectiveSettings.fogGamma;
	data.fogAlphaGamma = effectiveSettings.fogAlphaGamma;
	data.effectGamma = effectiveSettings.effectGamma;
	data.effectAlphaGamma = effectiveSettings.effectAlphaGamma;
	data.skyGamma = effectiveSettings.skyGamma;
	data.cloudGamma = effectiveSettings.cloudGamma;
	data.waterGamma = effectiveSettings.waterGamma;
	data.vlGamma = effectiveSettings.vlGamma;
	data.vanillaDiffuseColorMult = effectiveSettings.vanillaDiffuseColorMult;
	data.ambientMult = effectiveSettings.ambientMult;
	data.emitColorMult = effectiveSettings.emitColorMult;
	data.glowmapMult = effectiveSettings.glowmapMult;
	data.whiteDiffuseMult = effectiveSettings.whiteDiffuseMult;
	data.animalWhiteDiffuseMult = effectiveSettings.animalWhiteDiffuseMult;
	useAnimalWhiteDiffuseCategory = adaptiveBrightnessEnabled &&
	                                std::abs(effectiveSettings.animalWhiteDiffuseMult - effectiveSettings.whiteDiffuseMult) > 1e-5f;
	data.effectLightingMult = effectiveSettings.effectLightingMult;
	data.membraneEffectMult = effectiveSettings.membraneEffectMult;
	data.bloodEffectMult = effectiveSettings.bloodEffectMult;
	data.projectedEffectMult = effectiveSettings.projectedEffectMult;
	data.deferredEffectMult = effectiveSettings.deferredEffectMult;
	data.otherEffectMult = effectiveSettings.otherEffectMult;
	// Keep neutral Adaptive Balance profiles on the exact pre-feature shader path.
	data.enableAdaptiveBrightnessColorAdjustments = adaptiveBalanceSettings.hasColorAdjustments;
	return data;
}

bool LinearLighting::IsRuntimeEnabled() const
{
	if (!loaded || !settings.enableLinearLighting)
		return false;

	auto state = globals::state;
	if (state && state->IsMainOrLoadingMenuOpen())
		return false;

	if (IsDisabledForCurrentCell())
		return false;

	return true;
}

bool LinearLighting::IsDisabledForCurrentCell() const
{
	return LocationContext::IsDisabledByLocation(settings.DisableInInteriors, settings.DisableInExteriors);
}

RE::NiColor LinearLighting::ColorToLinear(RE::NiColor inColor, float gamma)
{
	RE::NiColor outColor;
	outColor.red = std::pow(inColor.red, gamma);
	outColor.green = std::pow(inColor.green, gamma);
	outColor.blue = std::pow(inColor.blue, gamma);
	return outColor;
}

void LinearLighting::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	if (!PerGeometryCB || !a_pass || !a_pass->geometry || !globals::d3d::context)
		return;

	const bool linearLightingEnabled = IsRuntimeEnabled();
	const bool adaptiveBrightnessEnabled = globals::features::adaptiveBrightness.IsRuntimeEnabled();
	if (!linearLightingEnabled && !adaptiveBrightnessEnabled)
		return;

	PerGeometryData perGeometryData{};
	perGeometryData.emissiveMult = 1.0f;
	if (auto* shaderProperty = a_pass->shaderProperty;
		linearLightingEnabled && shaderProperty && shaderProperty->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get()) {
		auto* lightProperty = static_cast<RE::BSLightingShaderProperty*>(shaderProperty);
		perGeometryData.emissiveMult = lightProperty->emissiveMult;
	}
	perGeometryData.whiteDiffuseCategory = useAnimalWhiteDiffuseCategory && IsAnimalGeometry(a_pass->geometry) ?
	                                           kWhiteDiffuseCategoryAnimal :
	                                           kWhiteDiffuseCategoryObject;
	PerGeometryCB->Update(perGeometryData);

	ID3D11Buffer* buffer = { PerGeometryCB->CB() };
	globals::d3d::context->PSSetConstantBuffers(kPerGeometryCBRegister, 1, &buffer);
}

#undef I18N_KEY_PREFIX
