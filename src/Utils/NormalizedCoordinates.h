#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Util::NormalizedCoordinates
{
	/**
	 * @brief Resolves a normalized coordinate to the nearest pixel boundary.
	 *
	 * The returned value is in [0, a_extent]. Non-finite coordinates resolve to
	 * zero so callers never convert NaN or infinity directly to an unsigned type.
	 */
	[[nodiscard]] inline uint32_t ResolvePixelBoundary(float a_coordinate, uint32_t a_extent) noexcept
	{
		if (a_extent == 0 || !std::isfinite(a_coordinate)) {
			return 0;
		}

		const double normalized = std::clamp(static_cast<double>(a_coordinate), 0.0, 1.0);
		return static_cast<uint32_t>(std::llround(normalized * static_cast<double>(a_extent)));
	}
}
