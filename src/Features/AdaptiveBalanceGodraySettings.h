#pragma once

#include "Utils/Finite.h"
#include "VolumetricLightingTuningMigration.h"

#include <cmath>

/** Context godray tuning, atmospheric clarity and legacy preset validation. */
namespace AdaptiveBalanceGodray
{
	/** Context bases compose with profile adjustments before reaching the renderer. */
	struct Profiles
	{
		VolumetricLightingTuning::Profile exterior;
		VolumetricLightingTuning::Profile interior;
		bool operator==(const Profiles&) const = default;
	};

	inline void to_json(nlohmann::json& a_json, const Profiles& a_profiles)
	{
		a_json = { { "exterior", a_profiles.exterior }, { "interior", a_profiles.interior } };
	}

	inline void from_json(const nlohmann::json& a_json, Profiles& a_profiles)
	{
		Profiles profiles;
		if (a_json.is_object()) {
			if (const auto it = a_json.find("exterior"); it != a_json.end())
				profiles.exterior = VolumetricLightingTuning::ReadProfile(*it);
			if (const auto it = a_json.find("interior"); it != a_json.end())
				profiles.interior = VolumetricLightingTuning::ReadProfile(*it);
		}
		a_profiles = profiles;
	}

	inline constexpr float kMin = 0.0f;
	inline constexpr float kMax = 5.0f;
	inline constexpr float kDefault = 1.0f;
	inline constexpr float kClarityMin = 0.0f;
	inline constexpr float kClarityMax = 1.0f;
	inline constexpr float kClarityDefault = 0.0f;

	/** Validate external writes without silently replacing an unsupported value. */
	[[nodiscard]] inline bool IsValidFinalBrightness(double a_value) noexcept
	{
		return std::isfinite(a_value) && a_value >= kMin && a_value <= kMax;
	}

	/** Keep the inert legacy preset/API value finite and compatible with older exports. */
	[[nodiscard]] inline float SanitizeFinalBrightness(float a_value) noexcept
	{
		return Util::ClampFinite(a_value, kMin, kMax, kDefault);
	}

	/** Bound smooth volumetric haze suppression; invalid values preserve the original image. */
	[[nodiscard]] inline float SanitizeClarity(float a_value) noexcept
	{
		return Util::ClampFinite(a_value, kClarityMin, kClarityMax, kClarityDefault);
	}
}
