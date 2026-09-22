#pragma once

#include <algorithm>
#include <cmath>

namespace VolumetricLightingTuning
{
	inline constexpr float kShaftIntensityMax = 3.0f;
	inline constexpr float kOpacityMax = 2.0f;
	inline constexpr float kSaturationMax = 4.0f;
	inline constexpr float kFloatEpsilon = 1e-4f;

	struct Profile
	{
		float ShaftIntensity = 1.0f;
		float Opacity = 1.0f;
		float Saturation = 1.0f;
		float CustomColorContribution = 0.0f;
		float CustomColorRed = 1.0f;
		float CustomColorGreen = 1.0f;
		float CustomColorBlue = 1.0f;

		bool operator==(const Profile&) const = default;
	};

	inline float ClampFinite(float a_value, float a_min, float a_max, float a_fallback)
	{
		if (!std::isfinite(a_value))
			a_value = a_fallback;
		return std::clamp(a_value, a_min, a_max);
	}

	inline bool IsNear(float a_value, float a_target, float a_epsilon = kFloatEpsilon)
	{
		return std::abs(a_value - a_target) <= a_epsilon;
	}

	inline Profile SanitizeProfile(const Profile& a_profile)
	{
		auto result = a_profile;
		result.ShaftIntensity = ClampFinite(result.ShaftIntensity, 0.0f, kShaftIntensityMax, 1.0f);
		result.Opacity = ClampFinite(result.Opacity, 0.0f, kOpacityMax, 1.0f);
		result.Saturation = ClampFinite(result.Saturation, 0.0f, kSaturationMax, 1.0f);
		result.CustomColorContribution = ClampFinite(result.CustomColorContribution, 0.0f, 1.0f, 0.0f);
		result.CustomColorRed = ClampFinite(result.CustomColorRed, 0.0f, 1.0f, 1.0f);
		result.CustomColorGreen = ClampFinite(result.CustomColorGreen, 0.0f, 1.0f, 1.0f);
		result.CustomColorBlue = ClampFinite(result.CustomColorBlue, 0.0f, 1.0f, 1.0f);
		return result;
	}

	inline float ApplyOpacityCurve(float a_value, float a_opacity)
	{
		// This reference curve stays aligned with the final volumetric composite shader.
		const float value = std::max(std::isfinite(a_value) ? a_value : 0.0f, 0.0f);
		const float opacity = ClampFinite(a_opacity, 0.0f, kOpacityMax, 1.0f);
		if (opacity <= kFloatEpsilon)
			return 0.0f;
		if (IsNear(opacity, 1.0f))
			return value;

		const float bounded = std::min(value, 1.0f);
		const float shaped = 1.0f - std::pow(std::max(1.0f - bounded, 0.0f), opacity);
		return shaped + std::max(value - 1.0f, 0.0f) * opacity;
	}
}
