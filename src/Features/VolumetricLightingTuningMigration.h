#pragma once

#include <nlohmann/json.hpp>

#include "VolumetricLightingTuning.h"

namespace VolumetricLightingTuning
{
	namespace detail
	{
		inline bool ReadFloat(const nlohmann::json& a_settings, const char* a_key, float& a_destination)
		{
			const auto it = a_settings.find(a_key);
			if (it == a_settings.end() || !it->is_number())
				return false;
			a_destination = it->get<float>();
			return true;
		}
	}

	inline Profile ReadProfile(const nlohmann::json& a_settings)
	{
		Profile profile{};
		if (!a_settings.is_object())
			return profile;

		detail::ReadFloat(a_settings, "ShaftIntensity", profile.ShaftIntensity);
		detail::ReadFloat(a_settings, "Opacity", profile.Opacity);
		detail::ReadFloat(a_settings, "Saturation", profile.Saturation);
		detail::ReadFloat(a_settings, "CustomColorContribution", profile.CustomColorContribution);
		detail::ReadFloat(a_settings, "CustomColorRed", profile.CustomColorRed);
		detail::ReadFloat(a_settings, "CustomColorGreen", profile.CustomColorGreen);
		detail::ReadFloat(a_settings, "CustomColorBlue", profile.CustomColorBlue);
		return SanitizeProfile(profile);
	}

	inline Profile ReadLegacyProfile(const nlohmann::json& a_settings)
	{
		Profile profile{};
		if (!a_settings.is_object())
			return profile;

		if (!detail::ReadFloat(a_settings, "GodrayShaftIntensity", profile.ShaftIntensity))
			detail::ReadFloat(a_settings, "GodrayIntensity", profile.ShaftIntensity);
		detail::ReadFloat(a_settings, "GodrayOpacity", profile.Opacity);
		detail::ReadFloat(a_settings, "GodraySaturation", profile.Saturation);
		detail::ReadFloat(a_settings, "CustomColorContribution", profile.CustomColorContribution);
		detail::ReadFloat(a_settings, "CustomColorRed", profile.CustomColorRed);
		detail::ReadFloat(a_settings, "CustomColorGreen", profile.CustomColorGreen);
		detail::ReadFloat(a_settings, "CustomColorBlue", profile.CustomColorBlue);
		return SanitizeProfile(profile);
	}
}
