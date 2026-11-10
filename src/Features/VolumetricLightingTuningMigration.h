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

		for (const auto& field : kProfileFields)
			detail::ReadFloat(a_settings, field.name, profile.*field.member);
		return SanitizeProfile(profile);
	}

	inline void to_json(nlohmann::json& a_json, const Profile& a_profile)
	{
		a_json = nlohmann::json::object();
		const auto profile = SanitizeProfile(a_profile);
		for (const auto& field : kProfileFields)
			a_json[field.name] = profile.*field.member;
	}

	inline void from_json(const nlohmann::json& a_json, Profile& a_profile)
	{
		a_profile = ReadProfile(a_json);
	}

	inline Profile ReadLegacyProfile(const nlohmann::json& a_settings)
	{
		Profile profile{};
		if (!a_settings.is_object())
			return profile;

		detail::ReadFloat(a_settings, "GodrayIntensity", profile.ShaftIntensity);
		for (const auto& field : kProfileFields)
			detail::ReadFloat(a_settings, field.legacyName, profile.*field.member);
		return SanitizeProfile(profile);
	}
}
