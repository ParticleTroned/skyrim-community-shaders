#include "InverseSquareLighting.h"

#include "CSEditor/EditorWindow.h"
#include "Features/InverseSquareLighting/Common.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "LightLimitFix.h"

#include <cmath>
#include <numbers>

#define I18N_KEY_PREFIX "feature.inverse_square_lighting."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	InverseSquareLighting::Settings,
	Enabled)

namespace
{
	float SanitizeRadius(const float a_radius)
	{
		return std::isfinite(a_radius) ? std::max(a_radius, 0.0f) : 0.0f;
	}

	void CaptureOriginalRadius(ISLCommon::RuntimeLightDataExt& a_runtimeData)
	{
		a_runtimeData.originalRadius = SanitizeRadius(a_runtimeData.radius);
	}

	float RestoreOriginalRadius(ISLCommon::RuntimeLightDataExt& a_runtimeData)
	{
		if (a_runtimeData.flags.any(LightLimitFix::LightFlags::Initialised))
			a_runtimeData.radius = SanitizeRadius(a_runtimeData.originalRadius);
		else
			a_runtimeData.radius = SanitizeRadius(a_runtimeData.radius);
		return a_runtimeData.radius;
	}

	void SetVanillaLightData(LightLimitFix::LightData& a_light, ISLCommon::RuntimeLightDataExt& a_runtimeData)
	{
		a_light.lightFlags.reset(
			LightLimitFix::LightFlags::Disabled,
			LightLimitFix::LightFlags::InverseSquare,
			LightLimitFix::LightFlags::Linear);

		a_light.radius = RestoreOriginalRadius(a_runtimeData);
		a_light.invRadius = a_light.radius > 0.0f ? 1.0f / a_light.radius : 0.0f;
		a_light.fadeZone = 0.0f;
		a_light.sizeBias = 0.0f;
		a_light.fade = a_runtimeData.fade;
	}
}

bool InverseSquareLighting::DrawEnabledCheckbox()
{
	bool enabled = settings.Enabled;
	if (ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &enabled))
		SetRuntimeEnabled(enabled);
	return enabled;
}

void InverseSquareLighting::DrawSettings()
{
	DrawEnabledCheckbox();
}

void InverseSquareLighting::DrawEssentialSettings()
{
	DrawEnabledCheckbox();
}

void InverseSquareLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	SetRuntimeEnabled(settings.Enabled);
}

void InverseSquareLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void InverseSquareLighting::RestoreDefaultSettings()
{
	settings = {};
	SetRuntimeEnabled(settings.Enabled);
}

void InverseSquareLighting::SetRuntimeEnabled(bool a_enabled)
{
	settings.Enabled = a_enabled;
	if (runtimeEnabled.exchange(a_enabled, std::memory_order_acq_rel) != a_enabled)
		ApplyRuntimeStateToActiveLights();
}

void InverseSquareLighting::ApplyRuntimeStateToActiveLights() const
{
	if (!globals::features::lightLimitFix.loaded)
		return;

	const auto smState = globals::game::smState;
	if (!smState)
		return;

	const auto shadowSceneNode = smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	auto applyRuntimeState = [this](const RE::NiPointer<RE::BSLight>& a_bsLight) {
		auto* bsLight = a_bsLight.get();
		if (!bsLight)
			return;

		auto* niLight = bsLight->light.get();
		if (!niLight)
			return;

		LightLimitFix::LightData light{};
		ProcessLight(light, bsLight, niLight);
	};

	for (const auto& bsLight : shadowSceneNode->GetRuntimeData().activeLights)
		applyRuntimeState(bsLight);
	for (const auto& bsLight : shadowSceneNode->GetRuntimeData().activeShadowLights)
		applyRuntimeState(bsLight);
}

void InverseSquareLighting::PostPostLoad()
{
	stl::detour_thunk<CreatePointLight>(REL::RelocationID(17208, 17610));
	stl::detour_thunk<BSLight_GetLuminance>(REL::RelocationID(101303, 108292));

	logger::info("[InverseSquareLighting] Installed hooks");
}

RE::NiPointLight* InverseSquareLighting::CreatePointLight::thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly)
{
	const auto niLight = func(ligh, refr, root, forceDynamic, useLightRadius, affectRequesterOnly);

	// Keep metadata current while runtime behavior is disabled so the same live lights can
	// immediately use their authored falloff again when the toggle is re-enabled.
	if (ligh && root && niLight)
		SetExtLightData(niLight, ligh);

	return niLight;
}

void InverseSquareLighting::SetExtLightData(RE::NiLight* niLight, const RE::TESObjectLIGH* ligh)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	CaptureOriginalRadius(*runtimeData);
	runtimeData->flags.set(LightLimitFix::LightFlags::Initialised);
	runtimeData->flags.reset(
		LightLimitFix::LightFlags::InverseSquare,
		LightLimitFix::LightFlags::Linear);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kInverseSquare)))
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kLinear)))
		runtimeData->flags.set(LightLimitFix::LightFlags::Linear);
	if (ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow)) {
		runtimeData->flags.set(LightLimitFix::LightFlags::Spot);
		runtimeData->flags.reset(LightLimitFix::LightFlags::OmniDirectional);
	} else {
		runtimeData->flags.reset(LightLimitFix::LightFlags::Spot);
		runtimeData->flags.set(LightLimitFix::LightFlags::OmniDirectional);
	}
	runtimeData->cutoffOverride = std::clamp(ligh->data.fallofExponent, 0.01f, 1.f);
	runtimeData->lighFormId = ligh->formID;
	const float size = ligh->data.fov >= 50.0f ? std::numbers::sqrt2_v<float> : ligh->data.fov;
	runtimeData->size = std::clamp(size, 0.01f, 50.0f);
}

void InverseSquareLighting::ProcessLight(LightLimitFix::LightData& light, RE::BSLight* bsLight, RE::NiLight* niLight) const
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	if (runtimeData->flags.none(LightLimitFix::LightFlags::Initialised)) {
		const auto userData = niLight->GetUserData();
		logger::debug("[InverseSquareLighting] FormID: 0x{:08X} | Light*: {:p} | Name: {} - light uninitialised", userData ? userData->formID : 0, static_cast<void*>(niLight), niLight->name);
		CaptureOriginalRadius(*runtimeData);
		runtimeData->flags.set(LightLimitFix::LightFlags::Initialised);
	}

	if (!IsEnabled()) {
		SetVanillaLightData(light, *runtimeData);
		return;
	}

	const auto& editorRef = EditorWindow::GetSingleton()->lightEditor;
	editorRef.ApplyOverrides(niLight, runtimeData);

	light.lightFlags = runtimeData->flags;
	light.color = { runtimeData->diffuse.red, runtimeData->diffuse.green, runtimeData->diffuse.blue };

	const bool isInvSq = light.lightFlags.any(LightLimitFix::LightFlags::InverseSquare);
	if (bsLight->pointLight && editorRef.enabled && ((isInvSq && editorRef.disableInvSqLights) || (!isInvSq && editorRef.disableRegularLights)))
		light.lightFlags.set(LightLimitFix::LightFlags::Disabled);

	if (bsLight->pointLight && isInvSq) {
		const float intensity = runtimeData->fade * 4;
		light.radius = CalculateRadius(intensity, bsLight->IsShadowLight(), runtimeData->cutoffOverride, runtimeData->size);
		runtimeData->radius = light.radius;
		light.invRadius = 1.f / light.radius;
		light.fadeZone = 1.f / (light.radius * std::clamp(FadeZoneBase * light.invRadius, 0.f, 1.f));
		light.sizeBias = ScaledUnitsSq * runtimeData->size * runtimeData->size * 0.5f;
		// light.color *= intensity;
		light.fade = intensity;
	} else {
		light.radius = SanitizeRadius(runtimeData->radius);
		light.invRadius = light.radius > 0.0f ? 1.0f / light.radius : 0.0f;
		// light.color *= runtimeData->fade;
		light.fade = runtimeData->fade;
	}
}

float InverseSquareLighting::CalculateRadius(const float intensity, const bool shadowCaster, const float cutoffOverride, const float size)
{
	float cutoff = shadowCaster ? DefaultShadowCasterCutoff : DefaultCutoff;
	cutoff = cutoffOverride == 1.f ? cutoff : cutoffOverride;
	const float radius = std::sqrt(ScaledUnitsSq * ((2 * intensity - cutoff * size * size) / (2 * cutoff)));
	return std::isfinite(radius) && radius > 0.f ? radius : 1.f;
}

inline float InverseSquareLighting::SmoothStep(const float edge0, const float edge1, const float x)
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float InverseSquareLighting::GetAttenuation(const float distance, const float radius, const float size)
{
	const float attenuation = ScaledUnitsSq / (distance * distance + ScaledUnitsSq * size * size / 2);
	const float fadeZone = std::clamp(FadeZoneBase / radius, 0.0f, 1.0f);
	const float fade = SmoothStep(0, radius * fadeZone, radius - distance);
	return attenuation * fade;
}

float InverseSquareLighting::BSLight_GetLuminance::thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight)
{
	if (!bsLight || !targetPosition)
		return func(bsLight, targetPosition, refLight);

	auto* niLight = bsLight->light.get();
	if (!niLight)
		return func(bsLight, targetPosition, refLight);

	if (!globals::features::inverseSquareLighting.IsEnabled()) {
		RestoreOriginalRadius(*ISLCommon::RuntimeLightDataExt::Get(niLight));
		return func(bsLight, targetPosition, refLight);
	}

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);

	if (refLight == niLight || runtimeData->flags.any(LightLimitFix::LightFlags::Disabled))
		return 0.0f;

	if (!bsLight->pointLight || runtimeData->flags.none(LightLimitFix::LightFlags::InverseSquare))
		return func(bsLight, targetPosition, refLight);

	const float dist = niLight->world.translate.GetDistance(*targetPosition);
	const float radius = CalculateRadius(runtimeData->fade * 4, bsLight->IsShadowLight(), runtimeData->cutoffOverride, runtimeData->size);
	const float attenuation = GetAttenuation(dist, radius, runtimeData->size);
	const float luminance = (runtimeData->diffuse.red + runtimeData->diffuse.green + runtimeData->diffuse.blue) * runtimeData->fade * 4 * attenuation * (1.0f / 3.0f);
	bsLight->luminance = luminance;

	return luminance;
}

#undef I18N_KEY_PREFIX
