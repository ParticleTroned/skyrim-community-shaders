#include "LinearLighting.h"

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
	ImGui::Checkbox("Enable Linear Lighting", (bool*)&settings.enableLinearLighting);
	ImGui::Checkbox("Disable in interiors", (bool*)&settings.DisableInInteriors);
	ImGui::Checkbox("Disable in exteriors", (bool*)&settings.DisableInExteriors);

	if (ImGui::BeginTabBar("##LinearLightingTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("General")) {
			ImGui::SeparatorText("Gamma Settings");
			ImGui::SliderFloat("Fog Gamma", &settings.fogGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Fog Transparency Gamma", &settings.fogAlphaGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Sky Gamma", &settings.skyGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Volumetric Lighting Gamma", &settings.vlGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Water Gamma", &settings.waterGamma, 0.1f, 3.0f, "%.2f");

			ImGui::SeparatorText("Multipliers");
			ImGui::SliderFloat("Directional Light Multiplier", &settings.directionalLightMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat("Ambient Multiplier", &settings.ambientMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat("Glowmap Multiplier", &settings.glowmapMult, 0.0f, 10.0f, "%.2f");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Advanced")) {
			ImGui::SeparatorText("Gamma Settings");
			ImGui::SliderFloat("Light Gamma", &settings.lightGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Color Gamma", &settings.colorGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Emissive Color Gamma", &settings.emitColorGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Glowmap Gamma", &settings.glowmapGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Ambient Gamma", &settings.ambientGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Effect Gamma", &settings.effectGamma, 0.1f, 3.0f, "%.2f");
			ImGui::SliderFloat("Effect Transparency Gamma", &settings.effectAlphaGamma, 0.1f, 3.0f, "%.2f");

			ImGui::SeparatorText("Multipliers");
			ImGui::SliderFloat("Vanilla Diffuse Color Multiplier", &settings.vanillaDiffuseColorMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat("Emissive Color Multiplier", &settings.emitColorMult, 0.0f, 10.0f, "%.2f");
			ImGui::SliderFloat("Point Light Multiplier", &settings.pointLightMult, 0.0f, 10.0f, "%.2f");

			if (ImGui::TreeNodeEx("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Effect Lighting Multiplier", &settings.effectLightingMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat("Membrane Effects Multiplier", &settings.membraneEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat("Blood Effects Multiplier", &settings.bloodEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat("Projected Effects Multiplier", &settings.projectedEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat("Deferred Effects Multiplier", &settings.deferredEffectMult, 0.0f, 10.0f, "%.2f");
				ImGui::SliderFloat("Other Effects Multiplier", &settings.otherEffectMult, 0.0f, 10.0f, "%.2f");
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

	dirLightMult = !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;
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
	const bool adaptiveBrightnessEnabled = globals::features::adaptiveBrightness.IsRuntimeEnabled();
	const auto effectiveSettings = globals::features::adaptiveBrightness.GetEffectiveLinearLightingSettings(settings, linearLightingEnabled);

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
	data.enableAdaptiveBrightness = adaptiveBrightnessEnabled;
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

	const bool linearLightingEnabled = IsRuntimeEnabled();
	if (lightProperty != nullptr && (linearLightingEnabled || globals::features::adaptiveBrightness.IsRuntimeEnabled())) {
		PerGeometryData perGeometryData{};
		perGeometryData.emissiveMult = lightProperty->emissiveMult;
		PerGeometryCB->Update(perGeometryData);

		ID3D11Buffer* buffer = { PerGeometryCB->CB() };
		auto context = globals::d3d::context;
		context->PSSetConstantBuffers(8, 1, &buffer);
	}
}
