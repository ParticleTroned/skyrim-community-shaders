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
	Util::UIntCheckbox("Enable", settings.enableLinearLighting);
	Util::UIntCheckbox("Disable in interiors", settings.DisableInInteriors);
	Util::UIntCheckbox("Disable in exteriors", settings.DisableInExteriors);

	if (ImGui::BeginTabBar("##LinearLightingTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Gamma")) {
			ImGui::SliderFloat("Ambient Gamma", &settings.ambientGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Color Gamma", &settings.colorGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Effect Gamma", &settings.effectGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Effect Transparency Gamma", &settings.effectAlphaGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Emissive Color Gamma", &settings.emitColorGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Fog Gamma", &settings.fogGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Fog Transparency Gamma", &settings.fogAlphaGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Glowmap Gamma", &settings.glowmapGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Light Gamma", &settings.lightGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Sky Gamma", &settings.skyGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Volumetric Lighting Gamma", &settings.vlGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::SliderFloat("Water Gamma", &settings.waterGamma, kGammaMin, kGammaMax, "%.2f");
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Multipliers")) {
			ImGui::SliderFloat("Ambient Multiplier", &settings.ambientMult, kMultiplierMin, kAmbientMultiplierMax, "%.2f");
			ImGui::SliderFloat("Vanilla Diffuse Color Multiplier", &settings.vanillaDiffuseColorMult, kMultiplierMin, kMultiplierMax, "%.2f");
			ImGui::SliderFloat("Emissive Color Multiplier", &settings.emitColorMult, kMultiplierMin, kMultiplierMax, "%.2f");
			ImGui::SliderFloat("Glowmap Multiplier", &settings.glowmapMult, kMultiplierMin, kMultiplierMax, "%.2f");
			ImGui::SliderFloat("Effect Lighting Multiplier", &settings.effectLightingMult, kMultiplierMin, kMultiplierMax, "%.2f");

			if (ImGui::TreeNodeEx("Effects")) {
				ImGui::SliderFloat("Blood Effects Multiplier", &settings.bloodEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
				ImGui::SliderFloat("Deferred Effects Multiplier", &settings.deferredEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
				ImGui::SliderFloat("Membrane Effects Multiplier", &settings.membraneEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
				ImGui::SliderFloat("Projected Effects Multiplier", &settings.projectedEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
				ImGui::SliderFloat("Other Effects Multiplier", &settings.otherEffectMult, kMultiplierMin, kMultiplierMax, "%.2f");
				ImGui::TreePop();
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void LinearLighting::DrawEssentialSettings()
{
	SanitizeSettings(settings);
	Util::UIntCheckbox("Enable", settings.enableLinearLighting);
}

void LinearLighting::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	const bool availableInCurrentCell = !IsDisabledForCurrentCell();
	ImGui::BeginDisabled(!availableInCurrentCell);
	Util::UIntCheckbox("Enable", settings.enableLinearLighting);
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
