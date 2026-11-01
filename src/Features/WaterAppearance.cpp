#include "I18n/I18n.h"
#include "WaterAppearance.h"

#include <algorithm>
#include <cmath>

#include "Utils/Finite.h"
#include "Utils/UI.h"

namespace
{
	constexpr float kWaterBrightnessMin = 0.0f;
	constexpr float kWaterBrightnessMax = 2.0f;
	constexpr float kWaterAmountMin = 0.0f;
	constexpr float kWaterAmountMax = 2.0f;
	constexpr float kWaterSunSpecularMax = 5.0f;
	constexpr float kWaterFresnelMin = 0.0f;
	constexpr float kWaterFresnelMax = 1.0f;
	constexpr float kCausticsTilingMin = 0.25f;
	constexpr float kCausticsTilingMax = 4.0f;
	constexpr float kCausticsSpeedMax = 3.0f;

	void DrawTooltip(const char* a_text)
	{
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", a_text);
	}

	void DrawWaterSlider(
		const char* a_label,
		float& a_value,
		float a_min,
		float a_max,
		const char* a_tooltip)
	{
		Util::Widgets::SliderFloat(
			a_label,
			&a_value,
			a_min,
			a_max,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		DrawTooltip(a_tooltip);
	}

	bool HasIdentityValues(const WaterAppearance::Profile& a_profile)
	{
		return a_profile.WaterBrightness == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.GlobalReflectionAmount == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.RefractionAmount == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.SunSpecularMultiplier == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.WaveAmplitude == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.FresnelMin == WaterAppearance::Profile::kIdentityFresnelMin &&
		       a_profile.FresnelMax == WaterAppearance::Profile::kIdentityFresnelMax &&
		       a_profile.Muddiness == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.CausticsStrength == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.CausticsTiling == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.CausticsSpeed == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.CausticsDispersion == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.ParallaxStrength == WaterAppearance::Profile::kIdentityScale &&
		       a_profile.ParallaxQuality == WaterAppearance::Profile::kDefaultParallaxQuality;
	}
}

void WaterAppearance::DrawProfileControls(Profile& a_profile)
{
	SanitizeProfile(a_profile);
	DrawWaterSlider(
		T("feature.water_appearance.water_brightness", "Water Brightness"),
		a_profile.WaterBrightness,
		kWaterBrightnessMin,
		kWaterBrightnessMax,
		T("feature.water_appearance.scales_the_final_water_output_including_fog_a_0b991658", "Scales the final water output, including fog and additive water-light passes."));
	SanitizeProfile(a_profile);
}

void WaterAppearance::DrawWaveAmplitudeControl(Profile& a_profile)
{
	SanitizeProfile(a_profile);
	DrawWaterSlider(
		T("feature.water_appearance.base_wave_amplitude", "Base Wave Amplitude"),
		a_profile.WaveAmplitude,
		kWaterAmountMin,
		kWaterAmountMax,
		T("feature.water_appearance.base_wave_strength_before_wind_response_wind_dfd41c84", "Base wave strength before Wind Response. Wind off keeps this fixed amplitude; final amplitude is capped at 2."));
	SanitizeProfile(a_profile);
}

void WaterAppearance::DrawAdvancedProfileSettings(Profile& a_profile)
{
	SanitizeProfile(a_profile);

	ImGui::TextWrapped(T("feature.water_appearance.identity_values_leave_this_layer_neutral_proc_39feb482", "Identity values leave this layer neutral. Processing stops if the composed water appearance is also neutral."));

	ImGui::SeparatorText(T("feature.water_appearance.surface", "Surface"));
	DrawWaterSlider(
		T("feature.water_appearance.fresnel_minimum", "Fresnel Minimum"),
		a_profile.FresnelMin,
		kWaterFresnelMin,
		a_profile.FresnelMax,
		T("feature.water_appearance.sets_the_lower_bound_of_the_water_reflection_response", "Sets the lower bound of the water reflection response."));
	DrawWaterSlider(
		T("feature.water_appearance.fresnel_maximum", "Fresnel Maximum"),
		a_profile.FresnelMax,
		a_profile.FresnelMin,
		kWaterFresnelMax,
		T("feature.water_appearance.sets_the_upper_bound_of_the_water_reflection_caf610d3", "Sets the upper bound of the water reflection response at grazing view angles."));

	ImGui::SeparatorText(T("feature.water_appearance.reflections", "Reflections"));
	DrawWaterSlider(
		T("feature.water_appearance.global_reflection_amount", "Global Reflection Amount"),
		a_profile.GlobalReflectionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		T("feature.water_appearance.scale_environment_cubemap_and_screen_space_re_9bdeb670", "Scale environment, cubemap and screen-space reflections after LOD Blending's height-faded blend."));
	DrawWaterSlider(
		T("feature.water_appearance.sun_specular_multiplier", "Sun Specular Multiplier"),
		a_profile.SunSpecularMultiplier,
		kWaterAmountMin,
		kWaterSunSpecularMax,
		T("feature.water_appearance.scales_the_direct_sun_highlight_reflected_by_13e9b0fc", "Scales the direct sun highlight reflected by the water surface."));

	ImGui::SeparatorText(T("feature.water_appearance.refraction_and_clarity", "Refraction and Clarity"));
	DrawWaterSlider(
		T("feature.water_appearance.refraction_amount", "Refraction Amount"),
		a_profile.RefractionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		T("feature.water_appearance.scales_the_distortion_applied_to_the_scene_vi_b1ff6c25", "Scales the distortion applied to the scene viewed through water."));
	DrawWaterSlider(
		T("feature.water_appearance.muddiness", "Muddiness"),
		a_profile.Muddiness,
		kWaterAmountMin,
		kWaterAmountMax,
		T("feature.water_appearance.scales_the_tinted_water_composition_over_the_cfe1c100", "Scales the tinted water composition over the refracted scene without changing shallow-fallback detection."));

	ImGui::SeparatorText(T("feature.water_appearance.caustics", "Caustics"));
	DrawWaterSlider(T("feature.water_appearance.caustics_strength", "Caustics Strength"), a_profile.CausticsStrength, kWaterAmountMin, kWaterAmountMax,
		T("feature.water_appearance.scales_underwater_light_pattern_contrast_one_95b3bd42", "Scales underwater light-pattern contrast. One preserves the current appearance; zero disables caustics."));
	DrawWaterSlider(T("feature.water_appearance.caustics_tiling", "Caustics Tiling"), a_profile.CausticsTiling, kCausticsTilingMin, kCausticsTilingMax,
		T("feature.water_appearance.higher_values_make_the_caustics_pattern_small_a2b2749f", "Higher values make the caustics pattern smaller and repeat more often."));
	DrawWaterSlider(T("feature.water_appearance.caustics_speed", "Caustics Speed"), a_profile.CausticsSpeed, kWaterAmountMin, kCausticsSpeedMax,
		T("feature.water_appearance.scales_caustics_animation_speed_zero_freezes_e3f8d6f6", "Scales caustics animation speed. Zero freezes the pattern."));
	DrawWaterSlider(T("feature.water_appearance.caustics_color_dispersion", "Caustics Color Dispersion"), a_profile.CausticsDispersion, kWaterAmountMin, kWaterAmountMax,
		T("feature.water_appearance.scales_the_color_separation_in_caustics_zero_c5398ced", "Scales the color separation in caustics. Zero removes color separation."));

	ImGui::SeparatorText(T("feature.water_appearance.parallax", "Parallax"));
	DrawWaterSlider(T("feature.water_appearance.parallax_strength", "Parallax Strength"), a_profile.ParallaxStrength, kWaterAmountMin, kWaterAmountMax,
		T("feature.water_appearance.scales_the_apparent_depth_of_water_waves_incl_e64049a7", "Scales the apparent depth of water waves, including flowmaps. Zero disables water parallax."));
	Util::Widgets::SliderInt(T("feature.water_appearance.parallax_quality", "Parallax Quality"), &a_profile.ParallaxQuality, Profile::kMinParallaxQuality, Profile::kMaxParallaxQuality, "%d", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T("feature.water_appearance.16_keeps_full_detail_quality_higher_values_us_64a87574", "16 keeps full-detail quality. Higher values use more samples across the viewport."));

	SanitizeProfile(a_profile);
}

WaterAppearance::Settings WaterAppearance::GetCommonBufferData(const Profile& a_profile)
{
	auto profile = a_profile;
	SanitizeProfile(profile);

	return {
		static_cast<uint>(!HasIdentityValues(profile)),
		profile.WaterBrightness,
		profile.GlobalReflectionAmount,
		profile.RefractionAmount,
		profile.SunSpecularMultiplier,
		profile.WaveAmplitude,
		profile.FresnelMin,
		profile.FresnelMax,
		profile.Muddiness,
		profile.CausticsStrength,
		profile.CausticsTiling,
		profile.CausticsSpeed,
		profile.CausticsDispersion,
		profile.ParallaxStrength,
		static_cast<uint>(profile.ParallaxQuality)
	};
}

WaterAppearance::Profile WaterAppearance::LerpProfiles(const Profile& a_a, const Profile& a_b, float a_t)
{
	auto from = a_a;
	auto to = a_b;
	SanitizeProfile(from);
	SanitizeProfile(to);

	const float t = Util::ClampFinite(a_t, 0.0f, 1.0f, 0.0f);
	const auto lerp = [&](float a_start, float a_end) {
		return std::lerp(a_start, a_end, t);
	};

	Profile result{
		lerp(from.WaterBrightness, to.WaterBrightness),
		lerp(from.GlobalReflectionAmount, to.GlobalReflectionAmount),
		lerp(from.RefractionAmount, to.RefractionAmount),
		lerp(from.SunSpecularMultiplier, to.SunSpecularMultiplier),
		lerp(from.WaveAmplitude, to.WaveAmplitude),
		lerp(from.FresnelMin, to.FresnelMin),
		lerp(from.FresnelMax, to.FresnelMax),
		lerp(from.Muddiness, to.Muddiness),
		lerp(from.CausticsStrength, to.CausticsStrength),
		lerp(from.CausticsTiling, to.CausticsTiling),
		lerp(from.CausticsSpeed, to.CausticsSpeed),
		lerp(from.CausticsDispersion, to.CausticsDispersion),
		lerp(from.ParallaxStrength, to.ParallaxStrength),
		static_cast<int>(std::lround(lerp(static_cast<float>(from.ParallaxQuality), static_cast<float>(to.ParallaxQuality))))
	};
	SanitizeProfile(result);
	return result;
}

void WaterAppearance::SanitizeProfile(Profile& a_profile)
{
	const Profile defaults{};
	a_profile.WaterBrightness = Util::ClampFinite(
		a_profile.WaterBrightness,
		kWaterBrightnessMin,
		kWaterBrightnessMax,
		defaults.WaterBrightness);
	a_profile.GlobalReflectionAmount = Util::ClampFinite(
		a_profile.GlobalReflectionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.GlobalReflectionAmount);
	a_profile.RefractionAmount = Util::ClampFinite(
		a_profile.RefractionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.RefractionAmount);
	a_profile.SunSpecularMultiplier = Util::ClampFinite(
		a_profile.SunSpecularMultiplier,
		kWaterAmountMin,
		kWaterSunSpecularMax,
		defaults.SunSpecularMultiplier);
	a_profile.WaveAmplitude = Util::ClampFinite(
		a_profile.WaveAmplitude,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.WaveAmplitude);
	a_profile.FresnelMin = Util::ClampFinite(
		a_profile.FresnelMin,
		kWaterFresnelMin,
		kWaterFresnelMax,
		defaults.FresnelMin);
	a_profile.FresnelMax = Util::ClampFinite(
		a_profile.FresnelMax,
		kWaterFresnelMin,
		kWaterFresnelMax,
		defaults.FresnelMax);
	a_profile.FresnelMin = std::min(a_profile.FresnelMin, a_profile.FresnelMax);
	a_profile.Muddiness = Util::ClampFinite(
		a_profile.Muddiness,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.Muddiness);
	a_profile.CausticsStrength = Util::ClampFinite(a_profile.CausticsStrength, kWaterAmountMin, kWaterAmountMax, defaults.CausticsStrength);
	a_profile.CausticsTiling = Util::ClampFinite(a_profile.CausticsTiling, kCausticsTilingMin, kCausticsTilingMax, defaults.CausticsTiling);
	a_profile.CausticsSpeed = Util::ClampFinite(a_profile.CausticsSpeed, kWaterAmountMin, kCausticsSpeedMax, defaults.CausticsSpeed);
	a_profile.CausticsDispersion = Util::ClampFinite(a_profile.CausticsDispersion, kWaterAmountMin, kWaterAmountMax, defaults.CausticsDispersion);
	a_profile.ParallaxStrength = Util::ClampFinite(a_profile.ParallaxStrength, kWaterAmountMin, kWaterAmountMax, defaults.ParallaxStrength);
	a_profile.ParallaxQuality = std::clamp(a_profile.ParallaxQuality, Profile::kMinParallaxQuality, Profile::kMaxParallaxQuality);
}
