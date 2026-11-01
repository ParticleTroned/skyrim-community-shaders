#pragma once

#include <algorithm>
#include <cmath>

namespace WetternessLightingPolicy
{
	/** Climate-relative night weight; invalid timing preserves the daytime response. */
	inline float NightFactor(float hour, float sunriseBegin, float sunriseEnd, float sunsetBegin, float sunsetEnd)
	{
		if (!std::isfinite(hour) || !std::isfinite(sunriseBegin) || !std::isfinite(sunriseEnd) ||
			!std::isfinite(sunsetBegin) || !std::isfinite(sunsetEnd) ||
			sunriseBegin < 0.0f || sunriseBegin > sunriseEnd || sunriseEnd >= sunsetBegin ||
			sunsetBegin > sunsetEnd || sunsetEnd > 24.0f) {
			return 0.0f;
		}
		hour = std::fmod(hour, 24.0f);
		if (hour < 0.0f)
			hour += 24.0f;
		if (hour >= sunriseEnd && hour < sunsetBegin)
			return 0.0f;
		if (hour < sunriseBegin || hour >= sunsetEnd)
			return 1.0f;

		const bool sunrise = hour < sunriseEnd;
		const float start = sunrise ? sunriseBegin : sunsetBegin;
		const float end = sunrise ? sunriseEnd : sunsetEnd;
		const float phase = std::clamp((hour - start) / (end - start), 0.0f, 1.0f);
		const float blend = phase * phase * (3.0f - 2.0f * phase);
		return sunrise ? 1.0f - blend : blend;
	}
}
