#include "LinearLighting.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "AdaptiveBrightness.h"
#include "LocationContext.h"
#include "State.h"

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
	ambientMult,
	vanillaDiffuseColorMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
	membraneEffectMult,
	bloodEffectMult,
	projectedEffectMult,
	deferredEffectMult,
	otherEffectMult)

namespace
{
	constexpr float kGammaMin = 0.1f;
	constexpr float kGammaMax = 3.0f;
	constexpr float kMultiplierMin = 0.0f;
	constexpr float kMultiplierMax = 5.0f;
	constexpr float kAmbientMultiplierMax = 5.0f;
	constexpr uint32_t kPerGeometryCBRegister = 8;
}

void LinearLighting::SanitizeSettings(Settings& a_settings)
{
	a_settings.enableLinearLighting = a_settings.enableLinearLighting != 0;
	a_settings.DisableInInteriors = a_settings.DisableInInteriors != 0;
	a_settings.DisableInExteriors = a_settings.DisableInExteriors != 0;
}

void LinearLighting::DrawSettings()
{
	SanitizeSettings(settings);

	MenuUI::SettingsPage page("LinearLighting", {
													{ "coverage", "Coverage", "Choose where linear lighting is enabled.", "Interior and exterior response", true, true, "Choose coverage and colour" },
													{ "colour", "Colour", "Refine brightness response after enabling linear lighting.", "Brightness and colour response", true, true, nullptr },
													{ "lighting", "Lighting", "Refine the contribution from each light source.", "Individual light contributions", true, true, "Balance light sources" },
												});

	if (page.Is("coverage")) {
		Util::UIntCheckbox("Disable in interiors", settings.DisableInInteriors);
		Util::AddTooltip("On: keeps the original lighting response indoors. Off: applies Linear Lighting there when the feature is enabled.");
		Util::UIntCheckbox("Disable in exteriors", settings.DisableInExteriors);
		Util::AddTooltip("On: keeps the original lighting response outdoors. Off: applies Linear Lighting there when the feature is enabled.");
	}

	if (page.Is("colour")) {
		MenuUI::DetailGrid colourGrid("colourGrid", 2, ImGui::GetFontSize() * 32);
		const Util::Widgets::ControlLayout controls(true);
		colourGrid.Next();
		MenuUI::SectionHeading("Light and surfaces");
		Util::Widgets::SliderFloat("Ambient Gamma", &settings.ambientGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes contrast in ambient light. Higher values generally darken midtones; 1 keeps the original response.");
		Util::Widgets::SliderFloat("Colour Gamma", &settings.colorGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes surface-color contrast. Higher values darken midtones; 1 keeps the original response.");
		Util::Widgets::SliderFloat("Effect Gamma", &settings.effectGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes brightness contrast in effects such as magic and smoke. Higher values generally darken midtones.");
		Util::Widgets::SliderFloat("Effect Transparency Gamma", &settings.effectAlphaGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes effect transparency. Higher values make partly transparent effects fainter; 1 keeps their original opacity.");
		Util::Widgets::SliderFloat("Emissive Colour Gamma", &settings.emitColorGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes contrast in light-emitting surface colors. Higher values generally darken midtones.");
		colourGrid.Next();
		MenuUI::SectionHeading("Atmosphere and effects");
		Util::Widgets::SliderFloat("Fog Gamma", &settings.fogGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes fog-color contrast. Higher values generally darken midtones; 1 keeps the original response.");
		Util::Widgets::SliderFloat("Fog Transparency Gamma", &settings.fogAlphaGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes fog transparency. Higher values make partly transparent fog fainter.");
		Util::Widgets::SliderFloat("Glowmap Gamma", &settings.glowmapGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes contrast in glowing texture details. Higher values generally darken midtones.");
		Util::Widgets::SliderFloat("Light Gamma", &settings.lightGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes direct-light color contrast. Higher values generally darken midtones.");
		Util::Widgets::SliderFloat("Sky Gamma", &settings.skyGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes sky-color contrast. Higher values generally darken midtones.");
		Util::Widgets::SliderFloat("Volumetric Lighting Gamma", &settings.vlGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes contrast in sunbeams and lit haze. Higher values generally darken midtones.");
		Util::Widgets::SliderFloat("Water Gamma", &settings.waterGamma, kGammaMin, kGammaMax, "%.2f");
		Util::AddTooltip("Changes water-color contrast. Higher values generally darken midtones.");
	}

	if (page.Is("lighting")) {
		Util::Widgets::SliderFloat("Ambient Multiplier", &settings.ambientMult, kMultiplierMin, kAmbientMultiplierMax, "%.2f");
		Util::AddTooltip("Brightness of ambient fill light. Higher values brighten shaded areas.");
		Util::Widgets::SliderFloat("Vanilla Diffuse Color Multiplier", &settings.vanillaDiffuseColorMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::AddTooltip("Brightness of ordinary surface colors before lighting. Lower values darken them.");
		Util::Widgets::SliderFloat("Emissive Color Multiplier", &settings.emitColorMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::AddTooltip("Brightness of surfaces that emit their own light. Higher values make them glow more.");
		Util::Widgets::SliderFloat("Glowmap Multiplier", &settings.glowmapMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::AddTooltip("Brightness of glowing details supplied by texture mods. Higher values strengthen the glow.");
		Util::Widgets::SliderFloat("Effect Lighting Multiplier", &settings.effectLightingMult, kMultiplierMin, kMultiplierMax, "%.2f");
		Util::AddTooltip("Overall brightness of lit effects such as magic and smoke.");

		if (ImGui::TreeNodeEx("Effects")) {
			Util::Widgets::SliderFloat("Blood Effects Multiplier", &settings.bloodEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::AddTooltip("Brightness of blood effects. Lower values make them darker.");
			Util::Widgets::SliderFloat("Deferred Effects Multiplier", &settings.deferredEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::AddTooltip("Brightness of effects blended with scene lighting. Higher values make them brighter.");
			Util::Widgets::SliderFloat("Membrane Effects Multiplier", &settings.membraneEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::AddTooltip("Brightness of surface-covering effects such as magical coatings.");
			Util::Widgets::SliderFloat("Projected Effects Multiplier", &settings.projectedEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::AddTooltip("Brightness of effects projected onto surfaces. Higher values make them more visible.");
			Util::Widgets::SliderFloat("Other Effects Multiplier", &settings.otherEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
			Util::AddTooltip("Brightness of remaining effect types not covered by the separate controls.");
			ImGui::TreePop();
		}
	}
}

void LinearLighting::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	const bool availableInCurrentCell = !IsDisabledForCurrentCell();
	ImGui::BeginDisabled(!availableInCurrentCell);
	Util::UIntCheckbox("Enabled", settings.enableLinearLighting);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Disabling bypasses linear color-space conversions and per-geometry updates.");
	}
	if (!availableInCurrentCell)
		ImGui::TextDisabled("Linear Lighting is disabled for the current location type.");
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

	dirLightMult = imageSpaceManager->GetImageSpaceData().baseData.hdr.sunlightScale;
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
	const auto adaptiveBrightnessSettings = globals::features::adaptiveBrightness.GetEffectiveLinearLightingSettings(
		settings,
		linearLightingEnabled);
	const auto& effectiveSettings = adaptiveBrightnessSettings.settings;

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
	data.effectLightingMult = effectiveSettings.effectLightingMult;
	data.membraneEffectMult = effectiveSettings.membraneEffectMult;
	data.bloodEffectMult = effectiveSettings.bloodEffectMult;
	data.projectedEffectMult = effectiveSettings.projectedEffectMult;
	data.deferredEffectMult = effectiveSettings.deferredEffectMult;
	data.otherEffectMult = effectiveSettings.otherEffectMult;
	// Keep neutral Adaptive Brightness profiles on the exact pre-feature shader path.
	data.enableAdaptiveBrightnessColorAdjustments = adaptiveBrightnessSettings.hasColorAdjustments;
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
	if (!PerGeometryCB || !a_pass || !globals::d3d::context)
		return;

	if (!IsRuntimeEnabled())
		return;

	PerGeometryData perGeometryData{};
	perGeometryData.emissiveMult = 1.0f;
	if (auto* shaderProperty = a_pass->shaderProperty;
		shaderProperty && shaderProperty->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get()) {
		auto* lightProperty = static_cast<RE::BSLightingShaderProperty*>(shaderProperty);
		perGeometryData.emissiveMult = lightProperty->emissiveMult;
	}
	PerGeometryCB->Update(perGeometryData);

	ID3D11Buffer* buffer = { PerGeometryCB->CB() };
	globals::d3d::context->PSSetConstantBuffers(kPerGeometryCBRegister, 1, &buffer);
}
