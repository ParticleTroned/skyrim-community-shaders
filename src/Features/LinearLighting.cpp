#include "LinearLighting.h"

#include "../I18n/I18n.h"
#include "AdaptiveBalance.h"
#include "LocationContext.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.linear_lighting."

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
	directionalLightMult,
	pointLightMult,
	ambientMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
	membraneEffectMult,
	bloodEffectMult,
	projectedEffectMult,
	deferredEffectMult,
	otherEffectMult)

void LinearLighting::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enable"), "Enable Linear Lighting"), (bool*)&settings.enableLinearLighting);
	ImGui::Checkbox(T(TKEY("disable_in_interiors"), "Disable in interiors"), (bool*)&settings.DisableInInteriors);
	ImGui::Checkbox(T(TKEY("disable_in_exteriors"), "Disable in exteriors"), (bool*)&settings.DisableInExteriors);

	if (ImGui::BeginTabBar("##LinearLightingTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem(T(TKEY("tab_general"), "General"))) {
			ImGui::SeparatorText(T(TKEY("gamma_settings"), "Gamma Settings"));
			ImGui::SliderFloat(T(TKEY("fog_gamma"), "Fog Gamma"), &settings.fogGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("fog_transparency_gamma"), "Fog Transparency Gamma"), &settings.fogAlphaGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("sky_gamma"), "Sky Gamma"), &settings.skyGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("vl_gamma"), "Volumetric Lighting Gamma"), &settings.vlGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("water_gamma"), "Water Gamma"), &settings.waterGamma, 0.1f, 3.0f, "%.2f");

			ImGui::SeparatorText(T(TKEY("multipliers"), "Multipliers"));
			ImGui::SliderFloat(T(TKEY("directional_light_multiplier"), "Directional Light Multiplier"), &settings.directionalLightMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("ambient_multiplier"), "Ambient Multiplier"), &settings.ambientMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("glowmap_multiplier"), "Glowmap Multiplier"), &settings.glowmapMult, 0.0f, 10.0f, "%.2f");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("tab_advanced"), "Advanced"))) {
			ImGui::SeparatorText(T(TKEY("gamma_settings"), "Gamma Settings"));
			ImGui::SliderFloat(T(TKEY("light_gamma"), "Light Gamma"), &settings.lightGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("color_gamma"), "Color Gamma"), &settings.colorGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("emissive_color_gamma"), "Emissive Color Gamma"), &settings.emitColorGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("glowmap_gamma"), "Glowmap Gamma"), &settings.glowmapGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("ambient_gamma"), "Ambient Gamma"), &settings.ambientGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("effect_gamma"), "Effect Gamma"), &settings.effectGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("effect_transparency_gamma"), "Effect Transparency Gamma"), &settings.effectAlphaGamma, 0.1f, 3.0f, "%.2f");

			ImGui::SeparatorText(T(TKEY("multipliers"), "Multipliers"));
			ImGui::SliderFloat(T(TKEY("vanilla_diffuse_color_multiplier"), "Vanilla Diffuse Color Multiplier"), &settings.vanillaDiffuseColorMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("emissive_color_multiplier"), "Emissive Color Multiplier"), &settings.emitColorMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat(T(TKEY("point_light_multiplier"), "Point Light Multiplier"), &settings.pointLightMult, 0.0f, 10.0f, "%.2f");

			if (ImGui::TreeNodeEx(T(TKEY("effects"), "Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat(T(TKEY("effect_lighting_multiplier"), "Effect Lighting Multiplier"), &settings.effectLightingMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("membrane_effects_multiplier"), "Membrane Effects Multiplier"), &settings.membraneEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("blood_effects_multiplier"), "Blood Effects Multiplier"), &settings.bloodEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("projected_effects_multiplier"), "Projected Effects Multiplier"), &settings.projectedEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("deferred_effects_multiplier"), "Deferred Effects Multiplier"), &settings.deferredEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat(T(TKEY("other_effects_multiplier"), "Other Effects Multiplier"), &settings.otherEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::TreePop();
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void LinearLighting::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LinearLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LinearLighting::RestoreDefaultSettings()
{
	settings = {};
}

void LinearLighting::SetupResources()
{
	PerGeometryCB = new ConstantBuffer(ConstantBufferDesc<PerGeometryData>());
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
			globals::features::linearLighting.BSLightingShader_SetupGeometry(Pass);
			func(This, Pass, RenderFlags);
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
	const bool adaptiveBalanceEnabled = globals::features::adaptiveBalance.IsRuntimeEnabled();
	const auto effectiveSettings = globals::features::adaptiveBalance.GetEffectiveLinearLightingSettings(settings, linearLightingEnabled);
	const auto imageAdjustments = globals::features::adaptiveBalance.GetEffectiveImageAdjustments();

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
	data.waterGamma = effectiveSettings.waterGamma;
	data.vlGamma = effectiveSettings.vlGamma;
	data.vanillaDiffuseColorMult = effectiveSettings.vanillaDiffuseColorMult;
	data.directionalLightMult = effectiveSettings.directionalLightMult;
	data.pointLightMult = effectiveSettings.pointLightMult;
	data.ambientMult = effectiveSettings.ambientMult;
	data.emitColorMult = effectiveSettings.emitColorMult;
	data.glowmapMult = effectiveSettings.glowmapMult;
	data.effectLightingMult = effectiveSettings.effectLightingMult;
	data.membraneEffectMult = effectiveSettings.membraneEffectMult;
	data.bloodEffectMult = effectiveSettings.bloodEffectMult;
	data.projectedEffectMult = effectiveSettings.projectedEffectMult;
	data.deferredEffectMult = effectiveSettings.deferredEffectMult;
	data.otherEffectMult = effectiveSettings.otherEffectMult;
	data.enableAdaptiveBalance = adaptiveBalanceEnabled;
	data.adaptiveImageBrightnessMult = imageAdjustments.imageBrightness;
	data.adaptiveBloomMult = imageAdjustments.bloom;
	data.adaptiveSaturationMult = imageAdjustments.saturation;
	data.adaptiveContrastMult = imageAdjustments.contrast;
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
	if (!PerGeometryCB)
		return;

	auto& property1 = a_pass->geometry->GetGeometryRuntimeData().shaderProperty;
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;

	if (lightProperty != nullptr && (IsRuntimeEnabled() || globals::features::adaptiveBalance.IsRuntimeEnabled())) {
		PerGeometryData perGeometryData{};
		perGeometryData.emissiveMult = lightProperty->emissiveMult;
		PerGeometryCB->Update(perGeometryData);

		ID3D11Buffer* buffer = { PerGeometryCB->CB() };
		auto context = globals::d3d::context;
		context->PSSetConstantBuffers(8, 1, &buffer);
	}
}

#undef I18N_KEY_PREFIX
