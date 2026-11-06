#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>

namespace Util
{
	/** Classify float bits even when fast floating-point optimization is enabled. */
	[[nodiscard]] constexpr bool IsFinite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
	}

	/** Classify double bits at configuration and native-data boundaries. */
	[[nodiscard]] constexpr bool IsFinite(double value) noexcept
	{
		return (std::bit_cast<std::uint64_t>(value) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
	}

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
