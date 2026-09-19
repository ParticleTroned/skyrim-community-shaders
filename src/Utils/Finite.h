#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>

namespace Util
{
	/**
	 * @brief Clamp finite input to the bounds; return the fallback unchanged for NaN or infinity.
	 * @pre Bounds are finite and ordered. The caller supplies a trusted fallback.
	 */
	template <std::floating_point T>
	[[nodiscard]] constexpr T ClampFinite(T a_value, T a_min, T a_max, T a_default) noexcept
	{
		return std::isfinite(a_value) ? std::clamp(a_value, a_min, a_max) : a_default;
	}
}
