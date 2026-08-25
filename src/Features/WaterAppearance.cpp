#include "WaterAppearance.h"

#include <algorithm>
#include <cmath>

#include "I18n/I18n.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.unified_water."

namespace
{
	constexpr float kWaterBrightnessMin = 0.0f;
	constexpr float kWaterBrightnessMax = 2.0f;
	constexpr float kWaterAmountMin = 0.0f;
	constexpr float kWaterAmountMax = 2.0f;
	constexpr float kWaterSunSpecularMax = 5.0f;
	constexpr float kWaterFresnelMin = 0.0f;
	constexpr float kWaterFresnelMax = 1.0f;

	float ClampFiniteOrDefault(float a_value, float a_min, float a_max, float a_default)
	{
		return std::isfinite(a_value) ? std::clamp(a_value, a_min, a_max) : a_default;
	}

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
		ImGui::SliderFloat(
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
		       a_profile.Muddiness == WaterAppearance::Profile::kIdentityScale;
	}
}

void WaterAppearance::DrawProfileControls(Profile& a_profile)
{
	SanitizeProfile(a_profile);

	ImGui::TextWrapped("%s", T(TKEY("profile_identity_tooltip"), "Identity values disable all water appearance processing and preserve Unified Water's native output path."));

	ImGui::SeparatorText(T(TKEY("water_tuning_output"), "Output"));
	DrawWaterSlider(
		T(TKEY("water_brightness"), "Water Brightness"),
		a_profile.WaterBrightness,
		kWaterBrightnessMin,
		kWaterBrightnessMax,
		T(TKEY("water_brightness_tooltip"), "Scales the final water output, including fog and additive water-light passes."));

	ImGui::SeparatorText(T(TKEY("water_tuning_surface"), "Surface"));
	DrawWaterSlider(
		T(TKEY("wave_amplitude"), "Wave Amplitude"),
		a_profile.WaveAmplitude,
		kWaterAmountMin,
		kWaterAmountMax,
		T(TKEY("wave_amplitude_tooltip"), "Scales the final water normal after flowmap, displacement, and rain-ripple detail are combined."));
	DrawWaterSlider(
		T(TKEY("fresnel_minimum"), "Fresnel Minimum"),
		a_profile.FresnelMin,
		kWaterFresnelMin,
		a_profile.FresnelMax,
		T(TKEY("fresnel_minimum_tooltip"), "Sets the minimum reflection response when viewing the water surface head-on."));
	DrawWaterSlider(
		T(TKEY("fresnel_maximum"), "Fresnel Maximum"),
		a_profile.FresnelMax,
		a_profile.FresnelMin,
		kWaterFresnelMax,
		T(TKEY("fresnel_maximum_tooltip"), "Sets the maximum reflection response at grazing view angles."));

	ImGui::SeparatorText(T(TKEY("water_tuning_reflections"), "Reflections"));
	DrawWaterSlider(
		T(TKEY("global_reflection_amount"), "Global Reflection Amount"),
		a_profile.GlobalReflectionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		T(TKEY("global_reflection_amount_tooltip"), "Scales the environment, cubemap, and screen-space reflection result after LOD Blending's height-faded reflection blend."));
	DrawWaterSlider(
		T(TKEY("sun_specular_multiplier"), "Sun Specular Multiplier"),
		a_profile.SunSpecularMultiplier,
		kWaterAmountMin,
		kWaterSunSpecularMax,
		T(TKEY("sun_specular_multiplier_tooltip"), "Scales the direct sun highlight reflected by the water surface."));

	ImGui::SeparatorText(T(TKEY("water_tuning_refraction_and_clarity"), "Refraction and Clarity"));
	DrawWaterSlider(
		T(TKEY("refraction_amount"), "Refraction Amount"),
		a_profile.RefractionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		T(TKEY("refraction_amount_tooltip"), "Scales the distortion applied to the scene viewed through water."));
	DrawWaterSlider(
		T(TKEY("muddiness"), "Muddiness"),
		a_profile.Muddiness,
		kWaterAmountMin,
		kWaterAmountMax,
		T(TKEY("muddiness_tooltip"), "Scales the tinted water composition over the refracted scene without changing shallow-fallback detection."));

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
		profile.Muddiness
	};
}

WaterAppearance::Profile WaterAppearance::LerpProfiles(const Profile& a_a, const Profile& a_b, float a_t)
{
	auto from = a_a;
	auto to = a_b;
	SanitizeProfile(from);
	SanitizeProfile(to);

	const float t = std::clamp(std::isfinite(a_t) ? a_t : 0.0f, 0.0f, 1.0f);
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
		lerp(from.Muddiness, to.Muddiness)
	};
	SanitizeProfile(result);
	return result;
}

void WaterAppearance::SanitizeProfile(Profile& a_profile)
{
	const Profile defaults{};
	a_profile.WaterBrightness = ClampFiniteOrDefault(
		a_profile.WaterBrightness,
		kWaterBrightnessMin,
		kWaterBrightnessMax,
		defaults.WaterBrightness);
	a_profile.GlobalReflectionAmount = ClampFiniteOrDefault(
		a_profile.GlobalReflectionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.GlobalReflectionAmount);
	a_profile.RefractionAmount = ClampFiniteOrDefault(
		a_profile.RefractionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.RefractionAmount);
	a_profile.SunSpecularMultiplier = ClampFiniteOrDefault(
		a_profile.SunSpecularMultiplier,
		kWaterAmountMin,
		kWaterSunSpecularMax,
		defaults.SunSpecularMultiplier);
	a_profile.WaveAmplitude = ClampFiniteOrDefault(
		a_profile.WaveAmplitude,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.WaveAmplitude);
	a_profile.FresnelMin = ClampFiniteOrDefault(
		a_profile.FresnelMin,
		kWaterFresnelMin,
		kWaterFresnelMax,
		defaults.FresnelMin);
	a_profile.FresnelMax = ClampFiniteOrDefault(
		a_profile.FresnelMax,
		kWaterFresnelMin,
		kWaterFresnelMax,
		defaults.FresnelMax);
	a_profile.FresnelMin = std::min(a_profile.FresnelMin, a_profile.FresnelMax);
	a_profile.Muddiness = ClampFiniteOrDefault(
		a_profile.Muddiness,
		kWaterAmountMin,
		kWaterAmountMax,
		defaults.Muddiness);
}

#undef I18N_KEY_PREFIX
