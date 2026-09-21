#include "Features/VolumetricLightingTuning.h"
#include "Features/VolumetricLightingTuningMigration.h"

#include <cmath>
#include <limits>

namespace
{
	using namespace VolumetricLightingTuning;
	using json = nlohmann::json;

	bool Near(float a_left, float a_right, float a_epsilon = 1e-5f)
	{
		return std::abs(a_left - a_right) <= a_epsilon;
	}

	bool Near(const Color& a_left, const Color& a_right)
	{
		return Near(a_left.red, a_right.red) &&
		       Near(a_left.green, a_right.green) &&
		       Near(a_left.blue, a_right.blue);
	}

	bool CoversProfileSanitization()
	{
		Profile unsafe{
			.ShaftIntensity = std::numeric_limits<float>::infinity(),
			.Opacity = -2.0f,
			.Saturation = 7.0f,
			.CustomColorContribution = std::numeric_limits<float>::quiet_NaN(),
			.CustomColorRed = -1.0f,
			.CustomColorGreen = 0.5f,
			.CustomColorBlue = 4.0f,
		};
		const auto sanitized = SanitizeProfile(unsafe);
		return sanitized.ShaftIntensity == 1.0f &&
		       sanitized.Opacity == 0.0f &&
		       sanitized.Saturation == kSaturationMax &&
		       sanitized.CustomColorContribution == 0.0f &&
		       sanitized.CustomColorRed == 0.0f &&
		       sanitized.CustomColorGreen == 0.5f &&
		       sanitized.CustomColorBlue == 1.0f;
	}

	bool CoversAuthoredColorBaseline()
	{
		const Color descriptor{ 0.8f, 0.2f, 0.4f };
		const Color sun{ 0.1f, 0.2f, 0.3f };
		const Color expectedBlend{ 0.275f, 0.2f, 0.325f };
		const Color invalidSun{ std::numeric_limits<float>::quiet_NaN(), 0.2f, 0.3f };

		return Near(ResolveEffectiveColor({ descriptor, 0.0f }, &sun), sun) &&
		       Near(ResolveEffectiveColor({ descriptor, 1.0f }, &sun), descriptor) &&
		       Near(ResolveEffectiveColor({ descriptor, 0.25f }, &sun), expectedBlend) &&
		       Near(ResolveEffectiveColor({ descriptor, 0.25f }, nullptr), descriptor) &&
		       Near(ResolveEffectiveColor({ descriptor, 0.25f }, &invalidSun), descriptor);
	}

	bool CoversExactCustomColorComposition()
	{
		const Color sun{ 0.1f, 0.2f, 0.3f };
		const ColorBlend authored{ { 0.8f, 0.4f, 0.2f }, 0.35f };
		const Color userColor{ 0.2f, 0.7f, 0.9f };
		const float userContribution = 0.45f;
		const auto composed = ComposeUserColor(authored, userColor, userContribution);
		const auto authoredEffective = ResolveEffectiveColor(authored, &sun);
		const auto expected = LerpColor(authoredEffective, userColor, userContribution);
		const auto unchanged = ComposeUserColor(authored, userColor, 0.0f);

		return Near(ResolveEffectiveColor(composed, &sun), expected) &&
		       Near(unchanged.color, authored.color) &&
		       unchanged.contribution == authored.contribution &&
		       Near(ResolveEffectiveColor(ComposeUserColor(authored, userColor, 1.0f), &sun), userColor);
	}

	bool CoversColorSanitization()
	{
		const Color unsafe{
			std::numeric_limits<float>::infinity(),
			-1.0f,
			std::numeric_limits<float>::quiet_NaN()
		};
		const Color fallback{ 0.2f, 0.3f, 0.4f };
		const Color expected{ 0.2f, 0.0f, 0.4f };
		const Color excessive{ kColorChannelMax * 2.0f, 0.5f, 0.25f };

		return Near(SanitizeColor(unsafe, fallback), expected) &&
		       SanitizeColor(excessive).red == kColorChannelMax;
	}

	bool CoversGamutPreservingSaturation()
	{
		const Color hdrColor{ 2.0f, 0.6f, 0.2f };
		const auto neutral = SaturateColor(hdrColor, 1.0f);
		const auto grayscale = SaturateColor(hdrColor, 0.0f);
		const auto saturated = SaturateColor(hdrColor, kSaturationMax);
		const float originalLuminance = GetLuminance(hdrColor);

		return Near(neutral, hdrColor) &&
		       Near(grayscale.red, originalLuminance) &&
		       Near(grayscale.green, originalLuminance) &&
		       Near(grayscale.blue, originalLuminance) &&
		       Near(GetLuminance(saturated), originalLuminance) &&
		       saturated.red >= 0.0f && saturated.red <= 2.0f &&
		       saturated.green >= 0.0f && saturated.green <= 2.0f &&
		       saturated.blue >= 0.0f && saturated.blue <= 2.0f;
	}

	bool CoversDistinctOpacityCurve()
	{
		const float input = 0.5f;
		const float reduced = ApplyOpacityCurve(input, 0.5f);
		const float boosted = ApplyOpacityCurve(input, 2.0f);
		return ApplyOpacityCurve(input, 0.0f) == 0.0f &&
		       ApplyOpacityCurve(input, 1.0f) == input &&
		       reduced > input * 0.5f && reduced < input &&
		       boosted > input && boosted < 1.0f &&
		       ApplyOpacityCurve(1.25f, 1.0f) == 1.25f &&
		       ApplyOpacityCurve(input, std::numeric_limits<float>::quiet_NaN()) == input &&
		       ApplyOpacityCurve(std::numeric_limits<float>::quiet_NaN(), 2.0f) == 0.0f;
	}

	bool CoversLegacyMigration()
	{
		const json oldSettings{
			{ "GodrayIntensity", 1.5f },
			{ "GodrayOpacity", 0.4f },
			{ "GodraySaturation", 1.8f },
			{ "CustomColorContribution", 0.25f },
			{ "CustomColorRed", 0.2f },
			{ "CustomColorGreen", 0.3f },
			{ "CustomColorBlue", 0.4f },
		};
		const auto oldProfile = ReadLegacyProfile(oldSettings);

		json mixedSettings = oldSettings;
		mixedSettings["GodrayShaftIntensity"] = 2.0f;
		const auto preferredProfile = ReadLegacyProfile(mixedSettings);
		mixedSettings["GodrayShaftIntensity"] = "malformed";
		const auto fallbackProfile = ReadLegacyProfile(mixedSettings);

		return oldProfile.ShaftIntensity == 1.5f &&
		       oldProfile.Opacity == 0.4f &&
		       oldProfile.Saturation == 1.8f &&
		       oldProfile.CustomColorContribution == 0.25f &&
		       oldProfile.CustomColorRed == 0.2f &&
		       oldProfile.CustomColorGreen == 0.3f &&
		       oldProfile.CustomColorBlue == 0.4f &&
		       preferredProfile.ShaftIntensity == 2.0f &&
		       fallbackProfile.ShaftIntensity == 1.5f;
	}

	bool CoversNestedProfileParsing()
	{
		const json partialProfile{
			{ "ShaftIntensity", 2.5f },
			{ "Opacity", "malformed" },
			{ "Saturation", -1.0f },
			{ "CustomColorContribution", 0.6f },
		};
		const auto parsed = ReadProfile(partialProfile);

		return parsed.ShaftIntensity == 2.5f &&
		       parsed.Opacity == 1.0f &&
		       parsed.Saturation == 0.0f &&
		       parsed.CustomColorContribution == 0.6f &&
		       ReadProfile(json::array()) == Profile{};
	}
}

int main()
{
	return CoversProfileSanitization() &&
	               CoversAuthoredColorBaseline() &&
	               CoversExactCustomColorComposition() &&
	               CoversColorSanitization() &&
	               CoversGamutPreservingSaturation() &&
	               CoversDistinctOpacityCurve() &&
	               CoversLegacyMigration() &&
	               CoversNestedProfileParsing() ?
	           0 :
	           1;
}
