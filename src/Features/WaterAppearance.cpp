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
		"Water Brightness",
		a_profile.WaterBrightness,
		kWaterBrightnessMin,
		kWaterBrightnessMax,
		"Scales the final water output, including fog and additive water-light passes.");
	SanitizeProfile(a_profile);
}

void WaterAppearance::DrawWaveAmplitudeControl(Profile& a_profile)
{
	SanitizeProfile(a_profile);
	DrawWaterSlider(
		"Base Wave Amplitude",
		a_profile.WaveAmplitude,
		kWaterAmountMin,
		kWaterAmountMax,
		"Sets wave strength before Wind Response multiplies it. With Wind Response off this is the fixed amplitude; the final amplitude is capped at 2.");
	SanitizeProfile(a_profile);
}

void WaterAppearance::DrawAdvancedProfileSettings(Profile& a_profile)
{
	SanitizeProfile(a_profile);

	ImGui::TextWrapped("Identity values make this layer neutral. Water appearance processing is disabled when the composed result is also neutral.");

	ImGui::SeparatorText("Surface");
	DrawWaterSlider(
		"Fresnel Minimum",
		a_profile.FresnelMin,
		kWaterFresnelMin,
		a_profile.FresnelMax,
		"Sets the lower bound of the water reflection response.");
	DrawWaterSlider(
		"Fresnel Maximum",
		a_profile.FresnelMax,
		a_profile.FresnelMin,
		kWaterFresnelMax,
		"Sets the upper bound of the water reflection response at grazing view angles.");

	ImGui::SeparatorText("Reflections");
	DrawWaterSlider(
		"Global Reflection Amount",
		a_profile.GlobalReflectionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		"Scales the environment, cubemap, and screen-space reflection result after LOD Blending's height-faded reflection blend.");
	DrawWaterSlider(
		"Sun Specular Multiplier",
		a_profile.SunSpecularMultiplier,
		kWaterAmountMin,
		kWaterSunSpecularMax,
		"Scales the direct sun highlight reflected by the water surface.");

	ImGui::SeparatorText("Refraction and Clarity");
	DrawWaterSlider(
		"Refraction Amount",
		a_profile.RefractionAmount,
		kWaterAmountMin,
		kWaterAmountMax,
		"Scales the distortion applied to the scene viewed through water.");
	DrawWaterSlider(
		"Muddiness",
		a_profile.Muddiness,
		kWaterAmountMin,
		kWaterAmountMax,
		"Scales the tinted water composition over the refracted scene without changing shallow-fallback detection.");

	ImGui::SeparatorText("Caustics");
	DrawWaterSlider("Caustics Strength", a_profile.CausticsStrength, kWaterAmountMin, kWaterAmountMax,
		"Scales underwater light-pattern contrast. One preserves the current appearance; zero disables caustics.");
	DrawWaterSlider("Caustics Tiling", a_profile.CausticsTiling, kCausticsTilingMin, kCausticsTilingMax,
		"Higher values make the caustics pattern smaller and repeat more often.");
	DrawWaterSlider("Caustics Speed", a_profile.CausticsSpeed, kWaterAmountMin, kCausticsSpeedMax,
		"Scales caustics animation speed. Zero freezes the pattern.");
	DrawWaterSlider("Caustics Color Dispersion", a_profile.CausticsDispersion, kWaterAmountMin, kWaterAmountMax,
		"Scales the color separation in caustics. Zero removes color separation.");

	ImGui::SeparatorText("Parallax");
	DrawWaterSlider("Parallax Strength", a_profile.ParallaxStrength, kWaterAmountMin, kWaterAmountMax,
		"Scales the apparent depth of water waves, including flowmaps. Zero disables water parallax.");
	ImGui::SliderInt("Parallax Quality", &a_profile.ParallaxQuality, Profile::kMinParallaxQuality, Profile::kMaxParallaxQuality, "%d", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip("16 preserves the current full-detail quality. Higher values increase sampling cost. VR retains foveated detail reduction.");

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
