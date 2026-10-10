#pragma once

#include "Utils/Finite.h"

#include <cmath>

/** Global brightness applied after the godray colour transform. */
namespace AdaptiveBalanceGodray
{
	inline constexpr float kMin = 0.0f;
	inline constexpr float kMax = 5.0f;
	inline constexpr float kDefault = 1.0f;

	/** Validate external writes without silently replacing an unsupported value. */
	[[nodiscard]] inline bool IsValidFinalBrightness(double a_value) noexcept
	{
		return std::isfinite(a_value) && a_value >= kMin && a_value <= kMax;
	}

	/** Keep malformed saved values neutral and bound the final GPU multiplier. */
	[[nodiscard]] inline float SanitizeFinalBrightness(float a_value) noexcept
	{
		return Util::ClampFinite(a_value, kMin, kMax, kDefault);
	}
}
