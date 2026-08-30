#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace VolumetricLightingTuning
{
	inline constexpr float kShaftIntensityMax = 3.0f;
	inline constexpr float kOpacityMax = 2.0f;
	inline constexpr float kSaturationMax = 4.0f;
	inline constexpr float kColorChannelMax = 65504.0f;  // Largest finite binary16 value used by the lighting targets.
	inline constexpr float kFloatEpsilon = 1e-4f;

	struct Color
	{
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;

		bool operator==(const Color&) const = default;
	};

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

	struct ColorBlend
	{
		Color color;
		float contribution = 0.0f;

		bool operator==(const ColorBlend&) const = default;
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

	inline bool IsFinite(const Color& a_color)
	{
		return std::isfinite(a_color.red) &&
		       std::isfinite(a_color.green) &&
		       std::isfinite(a_color.blue);
	}

	inline Color SanitizeColor(const Color& a_color, const Color& a_fallback = {})
	{
		const auto sanitizeChannel = [](float a_value, float a_fallbackValue) {
			const float fallback = std::isfinite(a_fallbackValue) ? a_fallbackValue : 0.0f;
			return std::clamp(std::isfinite(a_value) ? a_value : fallback, 0.0f, kColorChannelMax);
		};
		return {
			sanitizeChannel(a_color.red, a_fallback.red),
			sanitizeChannel(a_color.green, a_fallback.green),
			sanitizeChannel(a_color.blue, a_fallback.blue)
		};
	}

	inline Color ClampColor01(const Color& a_color)
	{
		const auto color = SanitizeColor(a_color);
		return {
			std::min(color.red, 1.0f),
			std::min(color.green, 1.0f),
			std::min(color.blue, 1.0f)
		};
	}

	inline Color LerpColor(const Color& a_from, const Color& a_to, float a_factor)
	{
		a_factor = ClampFinite(a_factor, 0.0f, 1.0f, 0.0f);
		return {
			a_from.red + (a_to.red - a_from.red) * a_factor,
			a_from.green + (a_to.green - a_from.green) * a_factor,
			a_from.blue + (a_to.blue - a_from.blue) * a_factor
		};
	}

	inline float GetLuminance(const Color& a_color)
	{
		return a_color.red * 0.2126f + a_color.green * 0.7152f + a_color.blue * 0.0722f;
	}

	inline Color SaturateColor(const Color& a_color, float a_saturation)
	{
		const auto color = SanitizeColor(a_color);
		a_saturation = ClampFinite(a_saturation, 0.0f, kSaturationMax, 1.0f);

		const float exposure = std::max({ 1.0f, color.red, color.green, color.blue });
		const Color normalized{ color.red / exposure, color.green / exposure, color.blue / exposure };
		const float luminance = GetLuminance(normalized);

		float maximumSaturation = std::numeric_limits<float>::max();
		const auto constrainChannel = [&](float a_channel) {
			const float chroma = a_channel - luminance;
			if (chroma > kFloatEpsilon) {
				maximumSaturation = std::min(maximumSaturation, (1.0f - luminance) / chroma);
			} else if (chroma < -kFloatEpsilon) {
				maximumSaturation = std::min(maximumSaturation, -luminance / chroma);
			}
		};
		constrainChannel(normalized.red);
		constrainChannel(normalized.green);
		constrainChannel(normalized.blue);

		const float appliedSaturation = std::min(a_saturation, std::max(maximumSaturation, 0.0f));
		return SanitizeColor({
			exposure * (luminance + (normalized.red - luminance) * appliedSaturation),
			exposure * (luminance + (normalized.green - luminance) * appliedSaturation),
			exposure * (luminance + (normalized.blue - luminance) * appliedSaturation)
		});
	}

	inline ColorBlend ComposeUserColor(
		const ColorBlend& a_authored,
		const Color& a_userColor,
		float a_userContribution)
	{
		const auto authoredColor = SanitizeColor(a_authored.color);
		const auto userColor = ClampColor01(a_userColor);
		const float authoredContribution = ClampFinite(a_authored.contribution, 0.0f, 1.0f, 0.0f);
		const float userContribution = ClampFinite(a_userContribution, 0.0f, 1.0f, 0.0f);
		if (userContribution <= 0.0f)
			return { authoredColor, authoredContribution };

		// Re-encode the combined blend so the engine still supplies the exact sun-side color.
		const float combinedContribution = std::clamp(
			authoredContribution + userContribution - authoredContribution * userContribution,
			0.0f,
			1.0f);
		const float authoredWeight = authoredContribution * (1.0f - userContribution);
		return {
			SanitizeColor({
				(authoredColor.red * authoredWeight + userColor.red * userContribution) / combinedContribution,
				(authoredColor.green * authoredWeight + userColor.green * userContribution) / combinedContribution,
				(authoredColor.blue * authoredWeight + userColor.blue * userContribution) / combinedContribution,
			}),
			combinedContribution
		};
	}

	inline Color ResolveEffectiveColor(const ColorBlend& a_authored, const Color* a_sunColor)
	{
		const auto authoredColor = SanitizeColor(a_authored.color);
		if (!a_sunColor || !IsFinite(*a_sunColor))
			return authoredColor;

		const auto sunColor = SanitizeColor(*a_sunColor);
		const float contribution = ClampFinite(a_authored.contribution, 0.0f, 1.0f, 0.0f);
		return LerpColor(sunColor, authoredColor, contribution);
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
