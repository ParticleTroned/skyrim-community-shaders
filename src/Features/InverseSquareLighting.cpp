#include "InverseSquareLighting.h"
#include "Diagnostics/ISLDiagnostics.h"
#include "LocationContext.h"
#include "Menu/SettingsPage.h"
#include "State.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/RuntimeToggle.h"
#include "Utils/UI.h"

#include "CSEditor/EditorWindow.h"
#include "Features/InverseSquareLighting/Common.h"
#include "Globals.h"
#include "LightLimitFix.h"

#include <cmath>
#include <numbers>

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
		CS_ISL_SCOPE(radiusRestore, RadiusRestore);
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

void InverseSquareLighting::DrawSettings()
{
	MenuUI::SettingsPage page("InverseSquareLighting", {
														   { "appearance", "How it works", "Learn how nearby light falloff changes.", "Nearby light falloff", true, true, "Shape nearby lighting" },
													   });
	if (!page.Is("appearance"))
		return;

	MenuUI::DetailText("Nearby lights use physically based distance falloff. Use Enabled in the header to compare with the original lighting; there are no additional tuning controls.");
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
	EditorWindow::GetSingleton()->lightEditor.RestoreDefaultSettings();
	settings = {};
	SetRuntimeEnabled(settings.Enabled);
}

void InverseSquareLighting::SetRuntimeEnabled(bool a_enabled)
{
	settings.Enabled = a_enabled;
	runtimeSettingsDirty.store(true, std::memory_order_release);
}

void InverseSquareLighting::EarlyPrepass()
{
	if (!runtimeSettingsDirty.load(std::memory_order_acquire) || Util::IsRuntimeToggleBlocked(globals::state))
		return;
	Util::RendererOwnership ownership(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context));
	if (!ownership || !globals::game::smState || !globals::game::smState->shadowSceneNode[0])
		return;
	const auto* player = RE::PlayerCharacter::GetSingleton();
	const auto* cell = player ? player->GetParentCell() : nullptr;
	if (!LocationContext::HasAttachedCell(globals::game::tes, cell))
		return;
	runtimeSettingsDirty.exchange(false, std::memory_order_acq_rel);
	if (runtimeEnabled.exchange(settings.Enabled, std::memory_order_acq_rel) != settings.Enabled)
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
#ifdef DEVBENCH_BRIDGE_ENABLED
	CSX::Diagnostics::ISL::InitializeStartup();
#endif
	// Hooks must honor the startup preference before any guarded render prepass.
	runtimeEnabled.store(settings.Enabled, std::memory_order_release);
	stl::detour_thunk<CreatePointLight>(REL::RelocationID(17208, 17610));
	stl::detour_thunk<BSLight_GetLuminance>(REL::RelocationID(101303, 108292));

	logger::info("[InverseSquareLighting] Installed hooks");
}

RE::NiPointLight* InverseSquareLighting::CreatePointLight::thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly)
{
	CS_ISL_SCOPE(creation, CreatePointLight);
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
	CS_ISL_SCOPE(metadata, MetadataInit);
	CS_ISL_COUNT(MetadataReinit, runtimeData->flags.any(LightLimitFix::LightFlags::Initialised));
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
	CS_ISL_SCOPE(process, ProcessLight);
#ifdef DEVBENCH_BRIDGE_ENABLED
	process.Unique(niLight);
#endif

	if (runtimeData->flags.none(LightLimitFix::LightFlags::Initialised)) {
		CS_ISL_COUNT(Uninitialized, 1);
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
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (!process.policy.editorBypass)
#endif
		editorRef.ApplyOverrides(niLight, runtimeData);

	light.lightFlags = runtimeData->flags;
	light.color = { runtimeData->diffuse.red, runtimeData->diffuse.green, runtimeData->diffuse.blue };

	const bool isInvSq = light.lightFlags.any(LightLimitFix::LightFlags::InverseSquare);
	if (bsLight->pointLight && editorRef.ShouldDisableLight(isInvSq))
		light.lightFlags.set(LightLimitFix::LightFlags::Disabled);

	if (bsLight->pointLight && isInvSq) {
		CS_ISL_COUNT(EligibleInverseSquare, 1);
		CS_ISL_COUNT(EligibleShadow, bsLight->IsShadowLight());
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto vanillaOutput = [&] {
			light.lightFlags.reset(LightLimitFix::LightFlags::InverseSquare, LightLimitFix::LightFlags::Linear);
			light.radius = SanitizeRadius(runtimeData->originalRadius);
			light.invRadius = light.radius > 0.0f ? 1.0f / light.radius : 0.0f;
			light.fadeZone = light.sizeBias = 0.0f;
			light.fade = runtimeData->fade;
		};
		if (process.policy.renderer == CSX::Diagnostics::ISL::Renderer::VanillaDirect &&
			CSX::Diagnostics::ISL::SuppressRadiusWrites() &&
			process.policy.luminance == CSX::Diagnostics::ISL::Luminance::Native &&
			!editorRef.IsEnabled() && !editorRef.HasDeferredWork()) {
			vanillaOutput();
			return;
		}
#endif
		CS_ISL_SCOPE(conversion, Conversion);
		const float intensity = runtimeData->fade * 4;
		light.radius = CalculateRadius(intensity, bsLight->IsShadowLight(), runtimeData->cutoffOverride, runtimeData->size);
		CS_ISL_COUNT(ComputedRadius, 1);
		CS_ISL_COUNT(NonfiniteRadius, !std::isfinite(light.radius));
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (CSX::Diagnostics::ISL::SuppressRadiusWrites()) {
			process.Count(CSX::Diagnostics::ISL::Metric::RadiusSuppressed);
		} else {
			process.Count(CSX::Diagnostics::ISL::Metric::RadiusWrite);
			process.Count(CSX::Diagnostics::ISL::Metric::RadiusChanged, runtimeData->radius != light.radius);
			runtimeData->radius = light.radius;
		}
#else
		runtimeData->radius = light.radius;
#endif
		light.invRadius = 1.f / light.radius;
		light.fadeZone = 1.f / (light.radius * std::clamp(FadeZoneBase * light.invRadius, 0.f, 1.f));
		light.sizeBias = ScaledUnitsSq * runtimeData->size * runtimeData->size * 0.5f;
		// light.color *= intensity;
		light.fade = intensity;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (process.policy.renderer == CSX::Diagnostics::ISL::Renderer::VanillaOutput)
			vanillaOutput();
#endif
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
	// Clamp degenerate results so callers never divide by a bad radius.
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

namespace
{
	float EvaluateCustomLuminance(RE::BSLight* bsLight, RE::NiLight* niLight, const ISLCommon::RuntimeLightDataExt* runtimeData, const RE::NiPoint3* targetPosition)
	{
		const float dist = niLight->world.translate.GetDistance(*targetPosition);
		const float radius = InverseSquareLighting::CalculateRadius(runtimeData->fade * 4, bsLight->IsShadowLight(), runtimeData->cutoffOverride, runtimeData->size);
		const float attenuation = InverseSquareLighting::GetAttenuation(dist, radius, runtimeData->size);
		const float luminance = (runtimeData->diffuse.red + runtimeData->diffuse.green + runtimeData->diffuse.blue) * runtimeData->fade * 4 * attenuation * (1.0f / 3.0f);
		return luminance;
	}
}
float InverseSquareLighting::BSLight_GetLuminance::thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight)
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	using namespace CSX::Diagnostics::ISL;
	Scope observation(Metric::LuminanceCalls, false, reinterpret_cast<uintptr_t>(_ReturnAddress()));
	const auto classify = [&](float value) {
		observation.Count(Metric::LuminanceNonfinite, !std::isfinite(value));
		observation.Count(Metric::LuminanceZero, value == 0);
		observation.Count(Metric::LuminancePositive, std::isfinite(value) && value > 0);
		observation.Count(Metric::LuminanceNegative, std::isfinite(value) && value < 0);
		return value;
	};
#endif
	const auto native = [&] {
		CS_ISL_COUNT(LuminanceNative, 1);
		const auto result = func(bsLight, targetPosition, refLight);
#ifdef DEVBENCH_BRIDGE_ENABLED
		return classify(result);
#else
		return result;
#endif
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (observation.policy.luminance != Luminance::Normal) {
		auto* source = bsLight ? bsLight->light.get() : nullptr;
		if (source && targetPosition && globals::features::inverseSquareLighting.IsEnabled()) {
			const auto* data = ISLCommon::RuntimeLightDataExt::Get(source);
			if (source != refLight && data->flags.none(LightLimitFix::LightFlags::Disabled) &&
				bsLight->pointLight && data->flags.any(LightLimitFix::LightFlags::InverseSquare)) {
				observation.Count(Metric::LuminanceEligible);
				if (observation.policy.luminance == Luminance::ComputeDiscard) {
					const float candidate = EvaluateCustomLuminance(bsLight, source, data, targetPosition);
					observation.Count(Metric::LuminanceCustom);
					observation.Candidate(candidate);
					observation.Count(Metric::LuminanceCandidateNonfinite, !std::isfinite(candidate));
				}
			}
		}
		return native();
	}
#endif
	if (!bsLight || !targetPosition) {
		CS_ISL_COUNT(LuminanceFallback, 1);
		return native();
	}

	auto* niLight = bsLight->light.get();
	if (!niLight) {
		CS_ISL_COUNT(LuminanceFallback, 1);
		return native();
	}

	if (!globals::features::inverseSquareLighting.IsEnabled()) {
		RestoreOriginalRadius(*ISLCommon::RuntimeLightDataExt::Get(niLight));
		{
			CS_ISL_COUNT(LuminanceFallback, 1);
			return native();
		}
	}

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);

	if (refLight == niLight || runtimeData->flags.any(LightLimitFix::LightFlags::Disabled)) {
		CS_ISL_COUNT(LuminanceSelf, refLight == niLight);
		CS_ISL_COUNT(LuminanceDisabled, runtimeData->flags.any(LightLimitFix::LightFlags::Disabled));
		CS_ISL_COUNT(LuminanceZero, 1);
		return 0.0f;
	}

	if (!bsLight->pointLight || runtimeData->flags.none(LightLimitFix::LightFlags::InverseSquare)) {
		CS_ISL_COUNT(LuminanceFallback, 1);
		return native();
	}

	CS_ISL_COUNT(LuminanceEligible, 1);
	const float luminance = EvaluateCustomLuminance(bsLight, niLight, runtimeData, targetPosition);
	CS_ISL_COUNT(LuminanceCustom, 1);
#ifdef DEVBENCH_BRIDGE_ENABLED
	classify(luminance);
#endif
	bsLight->luminance = luminance;

	return luminance;
}
