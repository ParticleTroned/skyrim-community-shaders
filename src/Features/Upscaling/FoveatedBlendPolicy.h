#pragma once

#include <algorithm>
#include <cmath>

namespace FoveatedBlendPolicy
{
	inline constexpr float MinFalloff = 0.5f;
	inline constexpr float MaxFalloff = 2.0f;
	inline constexpr float NeutralFalloff = 1.0f;

	/** Keep saved and shader values finite without changing the mask geometry. */
	inline float ClampFalloff(float value)
	{
		return std::isfinite(value) ? std::clamp(value, MinFalloff, MaxFalloff) : NeutralFalloff;
	}

	/** Disabling the curve preserves its saved value and restores legacy feathering. */
	inline float EffectiveFalloff(bool enabled, float savedFalloff)
	{
		return enabled ? ClampFalloff(savedFalloff) : NeutralFalloff;
	}
}
