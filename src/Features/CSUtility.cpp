#include "CSUtility.h"

#include "AdaptiveBrightness.h"
#include "Globals.h"
#include "InverseSquareLighting.h"
#include "LightLimitFix.h"
#include "LinearLighting.h"
#include "Utils/PointLightFlags.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kSkyBrightnessMin = 0.0f;
	constexpr float kSkyBrightnessMax = 2.0f;
	constexpr float kMultiplierMin = 0.0f;
	constexpr float kMultiplierMax = 5.0f;
	constexpr uint32_t kMaxVanillaPointLights = 7;
	constexpr uint32_t kVanillaPointLightCBRegister = 3;
	constexpr uint32_t kFirstPointLightSceneIndex = 1;

	float ClampFiniteOrDefault(float a_value, float a_min, float a_max, float a_default)
	{
		if (!std::isfinite(a_value))
			return a_default;
		return std::clamp(a_value, a_min, a_max);
	}

	void SanitizeSettings(CSUtility::Settings& a_settings)
	{
		const CSUtility::Settings defaults{};
		a_settings.skyBrightness = ClampFiniteOrDefault(a_settings.skyBrightness, kSkyBrightnessMin, kSkyBrightnessMax, defaults.skyBrightness);
		a_settings.directionalLightMult = ClampFiniteOrDefault(a_settings.directionalLightMult, kMultiplierMin, kMultiplierMax, defaults.directionalLightMult);
		a_settings.pointLightMult = ClampFiniteOrDefault(a_settings.pointLightMult, kMultiplierMin, kMultiplierMax, defaults.pointLightMult);
		a_settings.linearPointLightMult = ClampFiniteOrDefault(a_settings.linearPointLightMult, kMultiplierMin, kMultiplierMax, defaults.linearPointLightMult);
		a_settings.spotlightMult = ClampFiniteOrDefault(a_settings.spotlightMult, kMultiplierMin, kMultiplierMax, defaults.spotlightMult);
		a_settings.linearSpotlightMult = ClampFiniteOrDefault(a_settings.linearSpotlightMult, kMultiplierMin, kMultiplierMax, defaults.linearSpotlightMult);
		a_settings.omnidirectionalBulbMult = ClampFiniteOrDefault(a_settings.omnidirectionalBulbMult, kMultiplierMin, kMultiplierMax, defaults.omnidirectionalBulbMult);
		a_settings.linearOmnidirectionalBulbMult = ClampFiniteOrDefault(a_settings.linearOmnidirectionalBulbMult, kMultiplierMin, kMultiplierMax, defaults.linearOmnidirectionalBulbMult);
	}

	void DrawMultiplierSlider(const char* a_label, float& a_value)
	{
		ImGui::SliderFloat(a_label, &a_value, kMultiplierMin, kMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}

	void DrawLinearMultiplierSlider(const char* a_label, float& a_value, bool a_linearLightingEnabled)
	{
		ImGui::BeginDisabled(!a_linearLightingEnabled);
		DrawMultiplierSlider(a_label, a_value);
		ImGui::EndDisabled();

		if (!a_linearLightingEnabled) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enable Linear Lighting to use this multiplier.");
			}
		}
	}

	bool UsesPointLightTypeMultipliers(const CSUtility::Settings& a_settings)
	{
		return a_settings.spotlightMult != 1.0f ||
		       a_settings.omnidirectionalBulbMult != 1.0f;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::Settings,
	enabled,
	skyBrightness,
	directionalLightMult,
	pointLightMult,
	linearPointLightMult,
	spotlightMult,
	linearSpotlightMult,
	omnidirectionalBulbMult,
	linearOmnidirectionalBulbMult)

void CSUtility::DrawSettingsHeaderControls()
{
	ImGui::Checkbox(("Enable " + GetDisplayName()).c_str(), &settings.enabled);
}

void CSUtility::DrawSettings()
{
	if (ImGui::BeginTabBar("##CSUtilityTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Atmosphere")) {
			ImGui::SliderFloat("Sky Brightness", &settings.skyBrightness, kSkyBrightnessMin, kSkyBrightnessMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Multipliers")) {
			if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
				const bool linearLightingEnabled = globals::features::linearLighting.settings.enableLinearLighting;
				DrawMultiplierSlider("Global Point Lighting", settings.pointLightMult);
				DrawLinearMultiplierSlider("Global Point Lighting (Linear)", settings.linearPointLightMult, linearLightingEnabled);
				DrawMultiplierSlider("Spotlights", settings.spotlightMult);
				DrawLinearMultiplierSlider("Spotlights (Linear)", settings.linearSpotlightMult, linearLightingEnabled);
				DrawMultiplierSlider("Omnidirectional Bulbs", settings.omnidirectionalBulbMult);
				DrawLinearMultiplierSlider("Omnidirectional Bulbs (Linear)", settings.linearOmnidirectionalBulbMult, linearLightingEnabled);
				DrawMultiplierSlider("Directional Light Multiplier", settings.directionalLightMult);
				ImGui::TreePop();
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void CSUtility::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void CSUtility::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void CSUtility::RestoreDefaultSettings()
{
	settings = {};
}

void CSUtility::SetupResources()
{
	vanillaPointLightCB = new ConstantBuffer(ConstantBufferDesc<VanillaPointLightData>(), "CSUtility::VanillaPointLightData");
}

CSUtility::Settings CSUtility::GetNeutralSettings()
{
	auto neutralSettings = Settings{};
	neutralSettings.enabled = false;
	return neutralSettings;
}

CSUtility::PerFrameData CSUtility::GetCommonBufferData() const
{
	// Adaptive Brightness composes onto live utility settings, or onto a neutral
	// base while CS Utility is off so its own profile adjustments stay active.
	Settings effectiveSettings = globals::features::adaptiveBrightness.GetEffectiveCSUtilitySettings(settings, IsRuntimeEnabled());
	SanitizeSettings(effectiveSettings);

	PerFrameData data{};
	data.skyBrightness = effectiveSettings.skyBrightness;
	data.directionalLightMult = effectiveSettings.directionalLightMult;
	data.pointLightMult = effectiveSettings.pointLightMult;
	data.linearPointLightMult = effectiveSettings.linearPointLightMult;
	data.spotlightMult = effectiveSettings.spotlightMult;
	data.linearSpotlightMult = effectiveSettings.linearSpotlightMult;
	data.omnidirectionalBulbMult = effectiveSettings.omnidirectionalBulbMult;
	data.linearOmnidirectionalBulbMult = effectiveSettings.linearOmnidirectionalBulbMult;
	return data;
}

bool CSUtility::IsRuntimeEnabled() const
{
	return loaded && settings.enabled;
}

bool CSUtility::NeedsVanillaPointLightData() const
{
	if (!loaded || globals::features::lightLimitFix.loaded)
		return false;

	// Linear Lighting also consumes this buffer's linear-light classification,
	// independently of CS Utility's runtime toggle.
	if (globals::features::linearLighting.IsRuntimeEnabled())
		return true;

	return IsRuntimeEnabled() && UsesPointLightTypeMultipliers(settings);
}

void CSUtility::UpdateVanillaPointLightData(RE::BSRenderPass* a_pass, uint32_t a_lightCount)
{
	if (!vanillaPointLightCB || !globals::d3d::context || !a_pass || !a_pass->sceneLights)
		return;

	VanillaPointLightData data{};
	const uint32_t lightCount = std::min(a_lightCount, kMaxVanillaPointLights);
	for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
		const uint32_t sceneLightIndex = lightIndex + kFirstPointLightSceneIndex;
		if (sceneLightIndex >= a_pass->numLights)
			break;

		auto* bsLight = a_pass->sceneLights[sceneLightIndex];
		if (!bsLight)
			continue;

		auto* niLight = bsLight->light.get();
		auto pointLightFlags = PointLightFlags::GetVanillaPointLightFlags(bsLight, niLight);
		if (!globals::features::inverseSquareLighting.IsEnabled())
			pointLightFlags &= ~PointLightFlags::ToMask(PointLightFlags::Flags::Linear);
		data.pointLightFlags[lightIndex] = pointLightFlags;
	}

	vanillaPointLightCB->Update(data);

	ID3D11Buffer* buffer = vanillaPointLightCB->CB();
	globals::d3d::context->PSSetConstantBuffers(kVanillaPointLightCBRegister, 1, &buffer);
}

struct CSUtility::Hooks
{
	struct BSWaterShader_SetupGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags)
		{
			func(a_shader, a_pass, a_renderFlags);

			auto& csUtility = globals::features::csUtility;
			if (!csUtility.NeedsVanillaPointLightData())
				return;

			const uint32_t lightCount = a_pass && a_pass->numLights > 0 ? a_pass->numLights - kFirstPointLightSceneIndex : 0;
			csUtility.UpdateVanillaPointLightData(a_pass, lightCount);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);
		logger::info("[CSUtility] Installed hooks");
	}
};

void CSUtility::PostPostLoad()
{
	Hooks::Install();
}
