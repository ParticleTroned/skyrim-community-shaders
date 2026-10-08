#include "Features/VolumetricLightingTuning.h"
#include "Features/VolumetricLightingTuningMigration.h"

#include <limits>

namespace
{
	using namespace VolumetricLightingTuning;
	using json = nlohmann::json;

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
	               CoversDistinctOpacityCurve() &&
	               CoversLegacyMigration() &&
	               CoversNestedProfileParsing() ?
	           0 :
	           1;
}
