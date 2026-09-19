#pragma once

#include <algorithm>
#include <cmath>

namespace VRDepthCullingEnablePolicy
{
	inline constexpr float kMinimumExtent = 0.0f;
	inline constexpr float kMaximumExtent = 1000.0f;
	inline constexpr float kDefaultMinimumExtent = 10.0f;

	constexpr bool IsEnabled(
		bool a_isInterior,
		bool a_exteriorEnabled,
		bool a_interiorEnabled)
	{
		return a_isInterior ? a_interiorEnabled : a_exteriorEnabled;
	}

	/** Validate persisted thresholds before converting potentially large JSON numbers. */
	inline float SanitizeMinimumExtent(double a_value)
	{
		return std::isfinite(a_value) ?
		           static_cast<float>(std::clamp(a_value, static_cast<double>(kMinimumExtent), static_cast<double>(kMaximumExtent))) :
		           kDefaultMinimumExtent;
	}

	/** Select the location threshold already validated at the settings boundary. */
	constexpr float SelectMinimumExtent(bool a_isInterior, float a_exteriorExtent, float a_interiorExtent)
	{
		return a_isInterior ? a_interiorExtent : a_exteriorExtent;
	}
}
