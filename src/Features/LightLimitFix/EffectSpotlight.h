#pragma once

#include "Utils/Finite.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace LightLimitFixDetail
{
	inline constexpr std::uint32_t kInvalidEffectSpotlight = std::numeric_limits<std::uint32_t>::max();

	struct alignas(16) EffectSpotlight
	{
		std::array<float, 4> clipX;
		std::array<float, 4> clipY;
		std::array<float, 4> clipW;
	};
	static_assert(sizeof(EffectSpotlight) == 48);

	/** Pack the native row-vector projection for shader dot products; reject unusable cones. */
	[[nodiscard]] inline bool TryBuildEffectSpotlight(const float (&a_matrix)[4][4], float a_falloff,
		EffectSpotlight& a_projection, float& a_halfFalloff)
	{
		const float halfFalloff = a_falloff * 0.5f;
		if (!Util::IsFinite(halfFalloff) || halfFalloff <= 0.0f)
			return false;

		EffectSpotlight projection{};
		for (std::size_t i = 0; i < 4; ++i) {
			projection.clipX[i] = a_matrix[i][0];
			projection.clipY[i] = a_matrix[i][1];
			projection.clipW[i] = a_matrix[i][3];
			if (!Util::IsFinite(projection.clipX[i]) || !Util::IsFinite(projection.clipY[i]) ||
				!Util::IsFinite(projection.clipW[i]))
				return false;
		}

		const auto& x = projection.clipX;
		const auto& y = projection.clipY;
		const auto& w = projection.clipW;
		// A degenerate perspective could turn an uninitialized transform into an omnidirectional light.
		const double determinant = double(x[0]) * (double(y[1]) * w[2] - double(y[2]) * w[1]) -
		                           double(x[1]) * (double(y[0]) * w[2] - double(y[2]) * w[0]) +
		                           double(x[2]) * (double(y[0]) * w[1] - double(y[1]) * w[0]);
		if (determinant == 0.0)
			return false;

		a_projection = projection;
		a_halfFalloff = halfFalloff;
		return true;
	}
}
