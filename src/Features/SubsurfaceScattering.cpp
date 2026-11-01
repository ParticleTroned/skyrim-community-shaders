#include "SubsurfaceScattering.h"
#include "I18n/I18n.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "Deferred.h"
#include "ShaderCache.h"
#include "State.h"

#include <algorithm>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubsurfaceScattering::DiffusionProfile,
	BlurRadius, Thickness, Strength, Falloff)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SubsurfaceScattering::Settings,
	EnableSubsurfaceScattering,
	EnableCharacterLighting,
	CharacterLightingStrength,
	SSMode,
	ScatterMode,
	BaseProfile,
	HumanProfile,
	BurleySamples,
	MeanFreePathBase,
	MeanFreePathHuman,
	HumanMaleSSSIntensity,
	HumanMaleSSSSaturation,
	HumanMaleSSSBrightness,
	HumanMaleSSSBaseSaturation,
	HumanFemaleSSSIntensity,
	HumanFemaleSSSSaturation,
	HumanFemaleSSSBrightness,
	HumanFemaleSSSBaseSaturation)

namespace
{
	template <class TNPC>
	auto IsFemaleImpl(TNPC* npc, int) -> decltype(npc->IsFemale(), bool{})
	{
		return npc->IsFemale();
	}

	template <class TNPC>
	auto IsFemaleImpl(TNPC* npc, long) -> decltype(npc->GetSex(), bool{})
	{
		return static_cast<uint32_t>(npc->GetSex()) != 0;
	}

	inline bool GetNPCIsFemale(RE::TESNPC* npc)
	{
		return npc ? IsFemaleImpl(npc, 0) : false;
	}

	inline float ClampHumanSkinControl(float a_value)
	{
		return std::clamp(a_value, kHumanSkinControlMin, kHumanSkinControlMax);
	}

	constexpr float kHumanSkinControlMin = 0.0f;
	constexpr float kHumanSkinControlMax = 2.0f;

	inline float ClampHumanSkinControl(float a_value)
	{
		return std::clamp(a_value, kHumanSkinControlMin, kHumanSkinControlMax);
	}

	void DrawHumanSkinControls(
		const char* a_sectionTitle,
		float& a_intensity,
		float& a_saturation,
		float& a_brightness,
		float& a_baseSaturation)
	{
		ImGui::SeparatorText(a_sectionTitle);
		ImGui::PushID(a_sectionTitle);
		Util::Widgets::SliderFloat(T("feature.subsurface_scattering.sss_intensity", "SSS Intensity"), &a_intensity, kHumanSkinControlMin, kHumanSkinControlMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::Widgets::SliderFloat(T("feature.subsurface_scattering.sss_saturation", "SSS Saturation"), &a_saturation, kHumanSkinControlMin, kHumanSkinControlMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::Widgets::SliderFloat(T("feature.subsurface_scattering.skin_brightness", "Skin Brightness"), &a_brightness, kHumanSkinControlMin, kHumanSkinControlMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::Widgets::SliderFloat(T("feature.subsurface_scattering.skin_saturation", "Skin Saturation"), &a_baseSaturation, kHumanSkinControlMin, kHumanSkinControlMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::PopID();
	}

	void ApplyClampedHumanSkinControls(
		float& a_dstIntensity,
		float& a_dstSaturation,
		float& a_dstBrightness,
		float& a_dstBaseSaturation,
		float a_srcIntensity,
		float a_srcSaturation,
		float a_srcBrightness,
		float a_srcBaseSaturation)
	{
		a_dstIntensity = ClampHumanSkinControl(a_srcIntensity);
		a_dstSaturation = ClampHumanSkinControl(a_srcSaturation);
		a_dstBrightness = ClampHumanSkinControl(a_srcBrightness);
		a_dstBaseSaturation = ClampHumanSkinControl(a_srcBaseSaturation);
	}

	void SanitizeHumanSkinControls(SubsurfaceScattering::Settings& a_settings)
	{
		ApplyClampedHumanSkinControls(
			a_settings.HumanMaleSSSIntensity,
			a_settings.HumanMaleSSSSaturation,
			a_settings.HumanMaleSSSBrightness,
			a_settings.HumanMaleSSSBaseSaturation,
			a_settings.HumanMaleSSSIntensity,
			a_settings.HumanMaleSSSSaturation,
			a_settings.HumanMaleSSSBrightness,
			a_settings.HumanMaleSSSBaseSaturation);
		ApplyClampedHumanSkinControls(
			a_settings.HumanFemaleSSSIntensity,
			a_settings.HumanFemaleSSSSaturation,
			a_settings.HumanFemaleSSSBrightness,
			a_settings.HumanFemaleSSSBaseSaturation,
			a_settings.HumanFemaleSSSIntensity,
			a_settings.HumanFemaleSSSSaturation,
			a_settings.HumanFemaleSSSBrightness,
			a_settings.HumanFemaleSSSBaseSaturation);
	}

	void SanitizeSettings(SubsurfaceScattering::Settings& a_settings)
	{
		SanitizeHumanSkinControls(a_settings);
		a_settings.SSMode = std::clamp(a_settings.SSMode, 0, 1);
		a_settings.ScatterMode = std::clamp(
			a_settings.ScatterMode,
			static_cast<int>(SubsurfaceScattering::kPreScatter),
			static_cast<int>(SubsurfaceScattering::kPreAndPostScatter));
		a_settings.BurleySamples = std::clamp(a_settings.BurleySamples, 1u, 64u);
	}

	void ApplyLegacyHumanControl(
		const json& a_json,
		const char* a_legacyKey,
		const char* a_maleKey,
		const char* a_femaleKey,
		float& a_maleSetting,
		float& a_femaleSetting)
	{
		if (a_json.contains(a_legacyKey) && a_json[a_legacyKey].is_number()) {
			const float value = a_json[a_legacyKey].get<float>();
			if (!a_json.contains(a_maleKey) || !a_json[a_maleKey].is_number())
				a_maleSetting = value;
			if (!a_json.contains(a_femaleKey) || !a_json[a_femaleKey].is_number())
				a_femaleSetting = value;
		}
	}
}

void SubsurfaceScattering::DrawSettings()
{
	MenuUI::SettingsPage page("SubsurfaceScattering", {
														  { "model", T("feature.subsurface_scattering.ui_model", "Model"), T("feature.subsurface_scattering.ui_choose_how_light_passes_through_skin", "Choose how light passes through skin."), T("feature.subsurface_scattering.ui_separable_or_burley_scattering", "Separable or Burley scattering"), true, true, T("feature.subsurface_scattering.ui_choose_model_and_profiles", "Choose model and profiles") },
														  { "profiles", T("feature.adaptive_brightness.profiles", "Profiles"), T("feature.subsurface_scattering.ui_refine_the_chosen_model_for_ordinary_and_humanoid_materials", "Refine the chosen model for ordinary and humanoid materials."), T("feature.subsurface_scattering.ui_base_and_humanoid_skin_response", "Base and humanoid skin response"), true, true, nullptr },
													  });

	if (page.Is("model")) {
		Util::UIntCheckbox(T("feature.subsurface_scattering.enable_character_lighting", "Enable Character Lighting"), settings.EnableCharacterLighting);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.subsurface_scattering.vanilla_feature_not_recommended", "Vanilla feature, not recommended."));
		}
		if (settings.EnableCharacterLighting) {
			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.strength", "Strength"), &settings.CharacterLightingStrength, 0, 5, "%.2f");
		}
	}

	if (page.Is("model")) {
		const char* scatteringModels[] = {
			T("feature.subsurface_scattering.separable_sss", "Separable SSS"),
			T("feature.subsurface_scattering.burley", "Burley")
		};
		MenuUI::ChoiceSetting(T("feature.subsurface_scattering.ui_scattering_model", "Scattering model"), &settings.SSMode, scatteringModels, static_cast<int>(std::size(scatteringModels)));
	}

	if (page.Is("model") && settings.SSMode == 0) {
		ImGui::Spacing();
		ImGui::TextUnformatted(T("feature.subsurface_scattering.albedo_handling", "Albedo Handling"));
		ImGui::RadioButton(T("feature.subsurface_scattering.pre_scatter", "Pre-scatter"), &settings.ScatterMode, kPreScatter);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.subsurface_scattering.blur_the_lit_color_directly_fastest_but_blurs_8737133f", "Blur the lit color directly. Fastest, but blurs albedo texture detail along with lighting."));
		}
		ImGui::SameLine();
		ImGui::RadioButton(T("feature.subsurface_scattering.post_scatter", "Post-scatter"), &settings.ScatterMode, kPostScatter);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.subsurface_scattering.divide_out_albedo_blur_the_irradiance_multipl_f61c41ba", "Divide out albedo, blur the irradiance, multiply albedo back. Preserves texture detail."));
		}
		ImGui::SameLine();
		ImGui::RadioButton(T("feature.subsurface_scattering.pre_and_post", "Pre and Post"), &settings.ScatterMode, kPreAndPostScatter);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.subsurface_scattering.split_albedo_across_the_blur_using_sqrt_albed_c7b89841", "Split albedo across the blur using sqrt(albedo) on each side. A physically motivated middle ground."));
		}
	}

	if (page.Is("profiles") && settings.SSMode == 0) {
		MenuUI::DetailGrid profileGrid("SeparableProfiles", 2, ImGui::GetFontSize() * 32);
		const Util::Widgets::ControlLayout controls(true);
		{
			profileGrid.Next();
			MenuUI::SectionHeading(T("feature.subsurface_scattering.base_profile", "Base Profile"));
			ImGui::PushID("Base Profile");
			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.blur_radius", "Blur Radius"), &settings.BaseProfile.BlurRadius, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.blur_radius_tooltip", "Blur radius."));
			}

			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.thickness", "Thickness"), &settings.BaseProfile.Thickness, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.blur_radius_relative_to_depth", "Blur radius relative to depth."));
			}

			updateKernels |= Util::Widgets::ColorEdit3(T("feature.subsurface_scattering.strength", "Strength"), (float*)&settings.BaseProfile.Strength);
			updateKernels |= Util::Widgets::ColorEdit3(T("feature.subsurface_scattering.falloff", "Falloff"), (float*)&settings.BaseProfile.Falloff);

			ImGui::PopID();
		}

		{
			profileGrid.Next();
			MenuUI::SectionHeading(T("feature.subsurface_scattering.humanoid_profile", "Humanoid Profile"));
			ImGui::PushID("Humanoid Profile");
			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.blur_radius", "Blur Radius"), &settings.HumanProfile.BlurRadius, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.blur_radius_tooltip", "Blur radius."));
			}

			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.thickness", "Thickness"), &settings.HumanProfile.Thickness, 0, 3, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.blur_radius_relative_to_depth", "Blur radius relative to depth."));
			}

			updateKernels |= Util::Widgets::ColorEdit3(T("feature.subsurface_scattering.strength", "Strength"), (float*)&settings.HumanProfile.Strength);
			updateKernels |= Util::Widgets::ColorEdit3(T("feature.subsurface_scattering.falloff", "Falloff"), (float*)&settings.HumanProfile.Falloff);

			ImGui::PopID();
		}
	} else if (page.Is("profiles") && settings.SSMode == 1) {
		int burleySamples = static_cast<int>(settings.BurleySamples);
		if (Util::Widgets::SliderInt(T("feature.subsurface_scattering.burley_samples", "Burley Samples"), &burleySamples, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp))
			settings.BurleySamples = static_cast<uint>(std::clamp(burleySamples, 1, 64));
		MenuUI::DetailGrid profileGrid("BurleyProfiles", 2, ImGui::GetFontSize() * 32);
		const Util::Widgets::ControlLayout controls(true);
		{
			profileGrid.Next();
			MenuUI::SectionHeading(T("feature.subsurface_scattering.base_profile", "Base Profile"));
			ImGui::PushID("Base Profile");
			Util::Widgets::ColorEdit3(T("feature.subsurface_scattering.mean_free_path_color", "Mean Free Path Color"), (float*)&settings.MeanFreePathBase);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.ui_light_penetration_in_red_green_and_blue_scaled_by_mean_781fde15", "Light penetration in red, green and blue, scaled by Mean Free Path Distance."));
			}
			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.mean_free_path_distance", "Mean Free Path Distance"), &settings.MeanFreePathBase.w, 0.01f, 10.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.controls_the_distance_that_mean_free_path_col_d75d2e19", "Controls the distance that Mean Free Path Color goes into subsurface."));
			}
			ImGui::PopID();
		}

		{
			profileGrid.Next();
			MenuUI::SectionHeading(T("feature.subsurface_scattering.humanoid_profile", "Humanoid Profile"));
			ImGui::PushID("Humanoid Profile");
			Util::Widgets::ColorEdit3(T("feature.subsurface_scattering.mean_free_path_color", "Mean Free Path Color"), (float*)&settings.MeanFreePathHuman);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.ui_light_penetration_in_red_green_and_blue_scaled_by_mean_781fde15", "Light penetration in red, green and blue, scaled by Mean Free Path Distance."));
			}
			Util::Widgets::SliderFloat(T("feature.subsurface_scattering.mean_free_path_distance", "Mean Free Path Distance"), &settings.MeanFreePathHuman.w, 0.01f, 10.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.subsurface_scattering.controls_the_distance_that_mean_free_path_col_d75d2e19", "Controls the distance that Mean Free Path Color goes into subsurface."));
			}

			profileGrid.Next();
			DrawHumanSkinControls(T("feature.subsurface_scattering.ui_humanoid_skin_male", "Humanoid Skin (Male)"), settings.HumanMaleSSSIntensity, settings.HumanMaleSSSSaturation, settings.HumanMaleSSSBrightness, settings.HumanMaleSSSBaseSaturation);
			profileGrid.Next();
			DrawHumanSkinControls(T("feature.subsurface_scattering.ui_humanoid_skin_female", "Humanoid Skin (Female)"), settings.HumanFemaleSSSIntensity, settings.HumanFemaleSSSSaturation, settings.HumanFemaleSSSBrightness, settings.HumanFemaleSSSBaseSaturation);

			ImGui::PopID();
		}
	}

	ImGui::Spacing();
	ImGui::Spacing();
}

void SubsurfaceScattering::DrawPerformanceSettings(bool)
{
	ImGui::Checkbox(T("feature.subsurface_scattering.enable", "Enable"), &settings.EnableSubsurfaceScattering);

	ImGui::TextUnformatted(T("feature.subsurface_scattering.sss_mode", "SSS Mode"));
	ImGui::RadioButton(T("feature.subsurface_scattering.separable_sss", "Separable SSS"), &settings.SSMode, 0);
	ImGui::SameLine();
	ImGui::RadioButton(T("feature.subsurface_scattering.burley", "Burley"), &settings.SSMode, 1);

	if (settings.SSMode == 1) {
		int burleySamples = static_cast<int>(settings.BurleySamples);
		if (ImGui::SliderInt(T("feature.subsurface_scattering.burley_samples", "Burley Samples"), &burleySamples, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp))
			settings.BurleySamples = static_cast<uint>(std::clamp(burleySamples, 1, 64));
	}
}

json SubsurfaceScattering::CapturePerformanceSettingsState() const
{
	auto capturedSettings = settings;
	SanitizeSettings(capturedSettings);
	return capturedSettings;
}

void SubsurfaceScattering::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	if (a_enabled) {
		settings = Settings{};
		return;
	}

	settings.EnableSubsurfaceScattering = false;
}

json SubsurfaceScattering::CapturePerformanceCostMeasurementState() const
{
	return CapturePerformanceSettingsState();
}

void SubsurfaceScattering::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object())
		return;

	settings = a_state.get<Settings>();
	SanitizeSettings(settings);
}

float3 SubsurfaceScattering::Gaussian(DiffusionProfile& a_profile, float variance, float r)
{
	/**
     * We use a falloff to modulate the shape of the profile. Big falloffs
     * spreads the shape making it wider, while small falloffs make it
     * narrower.
     */
	float falloff[3] = { a_profile.Falloff.x, a_profile.Falloff.y, a_profile.Falloff.z };
	float g[3];
	for (int i = 0; i < 3; i++) {
		float rr = r / (0.001f + falloff[i]);
		g[i] = exp((-(rr * rr)) / (2.0f * variance)) / (2.0f * 3.14f * variance);
	}
	return float3(g[0], g[1], g[2]);
}

float3 SubsurfaceScattering::Profile(DiffusionProfile& a_profile, float r)
{
	/**
     * We used the red channel of the original skin profile defined in
     * [d'Eon07] for all three channels. We noticed it can be used for green
     * and blue channels (scaled using the falloff parameter) without
     * introducing noticeable differences and allowing for total control over
     * the profile. For example, it allows to create blue SSS gradients, which
     * could be useful in case of rendering blue creatures.
     */
	return  // 0.233f * gaussian(0.0064f, r) + /* We consider this one to be directly bounced light, accounted by the strength parameter (see @STRENGTH) */
		0.100f * Gaussian(a_profile, 0.0484f, r) +
		0.118f * Gaussian(a_profile, 0.187f, r) +
		0.113f * Gaussian(a_profile, 0.567f, r) +
		0.358f * Gaussian(a_profile, 1.99f, r) +
		0.078f * Gaussian(a_profile, 7.41f, r);
}

void SubsurfaceScattering::CalculateKernel(DiffusionProfile& a_profile, Kernel& kernel)
{
	uint nSamples = SSSS_N_SAMPLES;

	const float RANGE = nSamples > 20 ? 3.0f : 2.0f;
	const float EXPONENT = 2.0f;

	// Calculate the offsets:
	float step = 2.0f * RANGE / (nSamples - 1);
	for (uint i = 0; i < nSamples; i++) {
		float o = -RANGE + float(i) * step;
		float sign = o < 0.0f ? -1.0f : 1.0f;
		kernel.Sample[i].w = RANGE * sign * abs(pow(o, EXPONENT)) / pow(RANGE, EXPONENT);
	}

	// Calculate the weights:
	for (uint i = 0; i < nSamples; i++) {
		float w0 = i > 0 ? abs(kernel.Sample[i].w - kernel.Sample[i - 1].w) : 0.0f;
		float w1 = i < nSamples - 1 ? abs(kernel.Sample[i].w - kernel.Sample[i + 1].w) : 0.0f;
		float area = (w0 + w1) / 2.0f;
		float3 t = area * Profile(a_profile, kernel.Sample[i].w);
		kernel.Sample[i].x = t.x;
		kernel.Sample[i].y = t.y;
		kernel.Sample[i].z = t.z;
	}

	// We want the offset 0.0 to come first:
	float4 t = kernel.Sample[nSamples / 2];
	for (uint i = nSamples / 2; i > 0; i--)
		kernel.Sample[i] = kernel.Sample[i - 1];
	kernel.Sample[0] = t;

	// Calculate the sum of the weights, we will need to normalize them below:
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	for (uint i = 0; i < nSamples; i++)
		sum += float3(kernel.Sample[i]);

	// Normalize the weights:
	for (uint i = 0; i < nSamples; i++) {
		kernel.Sample[i].x /= sum.x;
		kernel.Sample[i].y /= sum.y;
		kernel.Sample[i].z /= sum.z;
	}

	// Tweak them using the desired strength. The first one is:
	//     lerp(1.0, kernel[0].rgb, strength)
	kernel.Sample[0].x = (1.0f - a_profile.Strength.x) * 1.0f + a_profile.Strength.x * kernel.Sample[0].x;
	kernel.Sample[0].y = (1.0f - a_profile.Strength.y) * 1.0f + a_profile.Strength.y * kernel.Sample[0].y;
	kernel.Sample[0].z = (1.0f - a_profile.Strength.z) * 1.0f + a_profile.Strength.z * kernel.Sample[0].z;

	// The others:
	//     lerp(0.0, kernel[0].rgb, strength)
	for (uint i = 1; i < nSamples; i++) {
		kernel.Sample[i].x *= a_profile.Strength.x;
		kernel.Sample[i].y *= a_profile.Strength.y;
		kernel.Sample[i].z *= a_profile.Strength.z;
	}
}

void SubsurfaceScattering::DrawSSS()
{
	if (!settings.EnableSubsurfaceScattering) {
		validMaterials = false;
		return;
	}

	if (!validMaterials)
		return;

	if (!GetComputeShaderPrepass())
		return;
	if (settings.SSMode == 0) {
		if (!GetComputeShaderHorizontalBlur() || !GetComputeShaderVerticalBlur())
			return;
	} else if (settings.SSMode == 1) {
		if (!GetComputeShaderBurley())
			return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering");

	validMaterials = false;

	auto dispatchCount = Util::GetScreenDispatchCount();

	{
		auto cameraData = globals::game::shadowState->GetRuntimeData().cameraData.getEye();

		blurCBData.SSSS_FOVY = atan(1.0f / cameraData.projMat.m[0][0]) * 2.0f * (180.0f / 3.14159265359f);

		blurCBData.BaseProfile = { settings.BaseProfile.BlurRadius, settings.BaseProfile.Thickness, 0, 0 };
		blurCBData.HumanProfile = { settings.HumanProfile.BlurRadius, settings.HumanProfile.Thickness, 0, 0 };

		blurCBData.BurleySamples = settings.BurleySamples;
		// Burley always does full albedo removal/reapply; scatter mode only applies to Separable SSS.
		blurCBData.ScatterMode = (settings.SSMode == 0) ? (uint)std::clamp(settings.ScatterMode, (int)kPreScatter, (int)kPreAndPostScatter) : (uint)kPostScatter;

		blurCBData.MeanFreePathBase = settings.MeanFreePathBase;
		blurCBData.MeanFreePathHuman = settings.MeanFreePathHuman;
		blurCBData.HumanMaleControls = {
			settings.HumanMaleSSSIntensity,
			settings.HumanMaleSSSSaturation,
			settings.HumanMaleSSSBrightness,
			settings.HumanMaleSSSBaseSaturation
		};
		blurCBData.HumanFemaleControls = {
			settings.HumanFemaleSSSIntensity,
			settings.HumanFemaleSSSSaturation,
			settings.HumanFemaleSSSBrightness,
			settings.HumanFemaleSSSBaseSaturation
		};

		blurCB->Update(blurCBData);
	}

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		ID3D11Buffer* buffer[1] = { blurCB->CB() };
		context->CSSetConstantBuffers(1, 1, buffer);
		context->CSSetSamplers(0, 1, &globals::deferred->pointSampler);

		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		auto mask = renderer->GetRuntimeData().renderTargets[MASKS];
		auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
		auto normal = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];

		ID3D11ShaderResourceView* views[5];
		views[0] = main.SRV;
		views[1] = Util::GetCurrentSceneDepthSRV(true);
		views[2] = mask.SRV;
		views[3] = albedo.SRV;
		views[4] = normal.SRV;

		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		// Pre-pass: remove albedo from diffuse, write to diffuseNoAlbedoTex
		{
			TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering - Prepass");

			ID3D11UnorderedAccessView* uav = diffuseNoAlbedoTex->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

			auto shader = GetComputeShaderPrepass();
			context->CSSetShader(shader, nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

			uav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}

		// Swap color input to pre-processed texture
		views[0] = diffuseNoAlbedoTex->srv.get();
		context->CSSetShaderResources(0, 1, views);

		if (settings.SSMode == 0) {
			// Horizontal pass: diffuseNoAlbedoTex -> blurHorizontalTemp
			{
				TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering - Horizontal");

				ID3D11UnorderedAccessView* uav = blurHorizontalTemp->uav.get();
				context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

				auto shader = GetComputeShaderHorizontalBlur();
				context->CSSetShader(shader, nullptr, 0);

				globals::profiler->BeginPass("SubsurfaceScattering::HorizontalBlur");
				context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
				globals::profiler->EndPass();

				uav = nullptr;
				context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			}

			// Vertical pass: blurHorizontalTemp -> main
			{
				TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering - Vertical");

				views[0] = blurHorizontalTemp->srv.get();
				context->CSSetShaderResources(0, 1, views);

				ID3D11UnorderedAccessView* uavs[1] = { main.UAV };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

				auto shader = GetComputeShaderVerticalBlur();
				context->CSSetShader(shader, nullptr, 0);

				globals::profiler->BeginPass("SubsurfaceScattering::VerticalBlur");
				context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
				globals::profiler->EndPass();
			}
		} else if (settings.SSMode == 1) {
			// Burley pass: diffuseNoAlbedoTex -> main (SSS pixels only)
			{
				TracyD3D11Zone(globals::state->tracyCtx, "Subsurface Scattering - Burley");

				ID3D11UnorderedAccessView* uavs[1] = { main.UAV };
				context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

				auto shader = GetComputeShaderBurley();
				context->CSSetShader(shader, nullptr, 0);

				globals::profiler->BeginPass("SubsurfaceScattering::Burley");
				context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
				globals::profiler->EndPass();
			}
		}
	}

	ID3D11Buffer* buffer = nullptr;
	context->CSSetConstantBuffers(1, 1, &buffer);

	ID3D11SamplerState* nullSampler = nullptr;
	context->CSSetSamplers(0, 1, &nullSampler);

	ID3D11ShaderResourceView* views[5]{ nullptr, nullptr, nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1]{ nullptr };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
}

void SubsurfaceScattering::SetupResources()
{
	{
		blurCB = new ConstantBuffer(ConstantBufferDesc<BlurCB>());
	}

	auto renderer = globals::game::renderer;

	{
		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);

		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		main.SRV->GetDesc(&srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		main.UAV->GetDesc(&uavDesc);

		blurHorizontalTemp = new Texture2D(texDesc);
		blurHorizontalTemp->CreateSRV(srvDesc);
		blurHorizontalTemp->CreateUAV(uavDesc);

		diffuseNoAlbedoTex = new Texture2D(texDesc);
		diffuseNoAlbedoTex->CreateSRV(srvDesc);
		diffuseNoAlbedoTex->CreateUAV(uavDesc);
	}
}

void SubsurfaceScattering::Reset()
{
	ApplyClampedHumanSkinControls(
		settings.HumanMaleSSSIntensity,
		settings.HumanMaleSSSSaturation,
		settings.HumanMaleSSSBrightness,
		settings.HumanMaleSSSBaseSaturation,
		settings.HumanMaleSSSIntensity,
		settings.HumanMaleSSSSaturation,
		settings.HumanMaleSSSBrightness,
		settings.HumanMaleSSSBaseSaturation);
	ApplyClampedHumanSkinControls(
		settings.HumanFemaleSSSIntensity,
		settings.HumanFemaleSSSSaturation,
		settings.HumanFemaleSSSBrightness,
		settings.HumanFemaleSSSBaseSaturation,
		settings.HumanFemaleSSSIntensity,
		settings.HumanFemaleSSSSaturation,
		settings.HumanFemaleSSSBrightness,
		settings.HumanFemaleSSSBaseSaturation);

	auto shaderManager = globals::game::smState;
	auto shaderCache = globals::shaderCache;
	shaderManager->characterLightEnabled = shaderCache->IsEnabled() ? settings.EnableCharacterLighting : true;
	if (shaderManager->characterLightEnabled) {
		if (CharacterLightingStrengthOriginal == -1.0f) {
			CharacterLightingStrengthOriginal = shaderManager->characterLightParams[2];
		}
		shaderManager->characterLightParams[2] = settings.CharacterLightingStrength * CharacterLightingStrengthOriginal;
	}

	if (updateKernels) {
		updateKernels = false;
		CalculateKernel(settings.BaseProfile, blurCBData.BaseKernel);
		CalculateKernel(settings.HumanProfile, blurCBData.HumanKernel);
	}
}

void SubsurfaceScattering::DrawEssentialSettings()
{
	ImGui::Checkbox(T("feature.subsurface_scattering.enable", "Enable"), &settings.EnableSubsurfaceScattering);
}

void SubsurfaceScattering::RestoreDefaultSettings()
{
	settings = {};
}

void SubsurfaceScattering::LoadSettings(json& o_json)
{
	settings = o_json;

	// Backward compatibility: older configs used one Human* control set.
	ApplyLegacyHumanControl(o_json, "HumanSSSIntensity", "HumanMaleSSSIntensity", "HumanFemaleSSSIntensity", settings.HumanMaleSSSIntensity, settings.HumanFemaleSSSIntensity);
	ApplyLegacyHumanControl(o_json, "HumanSSSSaturation", "HumanMaleSSSSaturation", "HumanFemaleSSSSaturation", settings.HumanMaleSSSSaturation, settings.HumanFemaleSSSSaturation);
	ApplyLegacyHumanControl(o_json, "HumanSSSBrightness", "HumanMaleSSSBrightness", "HumanFemaleSSSBrightness", settings.HumanMaleSSSBrightness, settings.HumanFemaleSSSBrightness);
	ApplyLegacyHumanControl(o_json, "HumanSSSBaseSaturation", "HumanMaleSSSBaseSaturation", "HumanFemaleSSSBaseSaturation", settings.HumanMaleSSSBaseSaturation, settings.HumanFemaleSSSBaseSaturation);
	SanitizeSettings(settings);
}

void SubsurfaceScattering::SaveSettings(json& o_json)
{
	auto savedSettings = settings;
	SanitizeSettings(savedSettings);
	o_json = savedSettings;
}

void SubsurfaceScattering::ClearShaderCache()
{
	prepassSS.Reset();
	horizontalSSBlur.Reset();
	verticalSSBlur.Reset();
	burleySS.Reset();
}

ID3D11ComputeShader* SubsurfaceScattering::GetComputeShaderPrepass()
{
	return prepassSS.Get(L"Data\\Shaders\\SubsurfaceScattering\\DiffuseExtractionCS.hlsl", {}, "cs_5_0");
}

ID3D11ComputeShader* SubsurfaceScattering::GetComputeShaderHorizontalBlur()
{
	return horizontalSSBlur.Get(L"Data\\Shaders\\SubsurfaceScattering\\SeparableSSSCS.hlsl", { { "HORIZONTAL", "" } }, "cs_5_0");
}

ID3D11ComputeShader* SubsurfaceScattering::GetComputeShaderVerticalBlur()
{
	return verticalSSBlur.Get(L"Data\\Shaders\\SubsurfaceScattering\\SeparableSSSCS.hlsl", {}, "cs_5_0");
}

ID3D11ComputeShader* SubsurfaceScattering::GetComputeShaderBurley()
{
	return burleySS.Get(L"Data\\Shaders\\SubsurfaceScattering\\SeparableSSSCS.hlsl", { { "BURLEY", "" } }, "cs_5_0");
}

void SubsurfaceScattering::DataLoaded()
{
	if (auto* form = RE::TESForm::LookupByEditorID("IsBeastRace")) {
		isBeastRaceKeyword = form->As<RE::BGSKeyword>();
	} else {
		isBeastRaceKeyword = nullptr;
	}

	if (!isBeastRaceKeyword) {
		logger::warn("[Subsurface Scattering] IsBeastRace keyword was not found; humanoid skin classification may be less accurate.");
	}
}

void SubsurfaceScattering::PostPostLoad()
{
	Hooks::Install();
}

void SubsurfaceScattering::BSLightingShader_SetupSkin(RE::BSRenderPass* a_pass)
{
	auto deferred = globals::deferred;
	auto state = globals::state;

	if (deferred->deferredPass) {
		bool isBeastRace = true;
		bool isFemale = false;

		if (a_pass && a_pass->shaderProperty &&
			a_pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kFace, RE::BSShaderProperty::EShaderPropertyFlag::kFaceGenRGBTint,
				RE::BSShaderProperty::EShaderPropertyFlag::kHairTint)) {
			auto geometry = a_pass->geometry;
			if (geometry) {
				if (auto userData = geometry->GetUserData()) {
					if (auto actor = userData->As<RE::Actor>()) {
						if (auto race = actor->GetRace(); race && isBeastRaceKeyword)
							isBeastRace = race->HasKeyword(isBeastRaceKeyword);
						if (auto base = actor->GetActorBase())
							isFemale = GetNPCIsFemale(base);
					}
				}
			}

			validMaterials = true;
		}

		state->permutationData.ExtraShaderDescriptor &= ~((uint)State::ExtraShaderDescriptors::IsBeastRace | (uint)State::ExtraShaderDescriptors::IsFemale);
		if (isBeastRace)
			state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::IsBeastRace;
		if (isFemale)
			state->permutationData.ExtraShaderDescriptor |= (uint)State::ExtraShaderDescriptors::IsFemale;
	}
}

void SubsurfaceScattering::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::subsurfaceScattering.BSLightingShader_SetupSkin(Pass);
	func(This, Pass, RenderFlags);
}
