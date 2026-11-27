#pragma once

#include "Common/SkinTransmission.hlsli"

#include <algorithm>
#include <array>
#include <cmath>

namespace SkinTransmission
{
	/** Caches the unit-thickness profile by validated transmission width. */
	class ProfileCache
	{
	public:
		/** Matches Skin::SSSSTransmittanceProfile(0, width, 1); the owner serializes access. */
		const std::array<float, 3>& Get(float width)
		{
			if (width != cachedWidth) {
				const float scale = DistanceScale / std::max(width, MinimumWidth);
				const float dd = -scale * scale;
				profile = {};
				for (const auto& term : Gaussians) {
					const float falloff = std::exp(dd / term.variance);
					profile[0] += term.red * falloff;
					profile[1] += term.green * falloff;
					profile[2] += term.blue * falloff;
				}
				cachedWidth = width;
			}
			return profile;
		}

	private:
		float cachedWidth = -1.0f;
		std::array<float, 3> profile{};
	};
}
