#include "CSUtility.h"

#include "AdaptiveBrightness.h"
#include "Globals.h"
#include "InverseSquareLighting.h"
#include "LightLimitFix.h"
#include "LinearLighting.h"
#include "UnderwaterDepthOfField.h"
#include "Utils/PointLightFlags.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr uint32_t kMaxVanillaPointLights = 7;
	constexpr uint32_t kFirstPointLightSceneIndex = 1;

	void SanitizeSettings(CSUtility::Settings& a_settings)
	{
		CSUtility::SanitizeDepthOfFieldOverride(a_settings.sceneDof);
		CSUtility::SanitizeDepthOfFieldOverride(a_settings.underwaterDof);
	}

	bool UsesPointLightTypeMultipliers(const SharedLightingSettings& a_settings)
	{
		return a_settings.spotlightMult != 1.0f ||
		       a_settings.omnidirectionalBulbMult != 1.0f;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::DepthOfFieldAutoFocusSettings,
	nearDistance,
	farDistance,
	nearRange,
	farRange,
	nearBlur,
	farBlur,
	blurMultiplier)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::DepthOfFieldSettings,
	strength,
	distance,
	range,
	mode,
	excludeSky,
	autoFocus,
	autoFocusSettings,
	blurRadius)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::DepthOfFieldOverride,
	locked,
	values,
	baseline)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::Settings,
	enabled,
	fixUnderwaterFogDofBlur,
	sceneDof,
	underwaterDof)

void CSUtility::DrawSettingsHeaderControls()
{
	ImGui::Checkbox("Enable DOF Utilities", &settings.enabled);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Controls the depth-of-field overrides and underwater fog blur correction on this page.");
}

void CSUtility::DrawSettings()
{
	DrawDepthOfFieldSettings();
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

CSUtility::PerFrameData CSUtility::GetCommonBufferData() const
{
	const auto effectiveSettings = globals::features::adaptiveBrightness.GetEffectiveSharedLightingSettings();

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
	if (!loaded)
		return false;

	// The shared flag buffer still classifies linear point lights for Linear
	// Lighting even when CS Utility's own runtime toggle is off.
	if (globals::features::linearLighting.IsRuntimeEnabled())
		return true;

	const auto& adaptiveBalance = globals::features::adaptiveBrightness;
	return adaptiveBalance.loaded &&
	       adaptiveBalance.settings.rendererControlsEnabled &&
	       UsesPointLightTypeMultipliers(adaptiveBalance.settings.lighting);
}

void CSUtility::UpdateVanillaPointLightData(RE::BSRenderPass* a_pass, uint32_t a_lightCount, uint32_t a_bufferRegister)
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
	globals::d3d::context->PSSetConstantBuffers(a_bufferRegister, 1, &buffer);
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
			csUtility.UpdateVanillaPointLightData(a_pass, lightCount, kWaterPointLightCBRegister);
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
	InstallDepthOfFieldHooks();
}

void CSUtility::DataLoaded()
{
	UnderwaterDepthOfField::InstallHooks();
}
