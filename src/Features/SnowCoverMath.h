#pragma once

#include <algorithm>
#include <cmath>

namespace SnowCoverMath
{
	constexpr float MonthsPerYear = 12.0f;
	constexpr float MaxWeatherAccumulation = 2.0f;

	/** Game-hour accumulation rate, shared with melt history's time-skip endpoint calculation. */
	inline float AccumulationRate(float snow, bool snowing, bool raining, float snowSpeed, float meltSpeed)
	{
		snowSpeed = std::isfinite(snowSpeed) ? std::max(snowSpeed, 0.0f) : 0.0f;
		meltSpeed = std::isfinite(meltSpeed) ? std::max(meltSpeed, 0.0f) : 0.0f;
		return snowing ? snowSpeed : (raining ? -2.0f * meltSpeed : (snow > 0 ? -meltSpeed : (snow < 0 ? meltSpeed : 0.0f)));
	}

	/** @brief Interpolates the cyclic snow line between the summer and winter peaks. */
	inline float SeasonalAltitude(float month, unsigned summerMonth, unsigned winterMonth, float summerHeight, float winterHeight)
	{
		month = std::isfinite(month) ? std::fmod(month, MonthsPerYear) : 0.0f;
		if (month < 0.0f)
			month += MonthsPerYear;
		summerMonth %= static_cast<unsigned>(MonthsPerYear);
		winterMonth %= static_cast<unsigned>(MonthsPerYear);
		summerHeight = std::isfinite(summerHeight) ? summerHeight : 0.0f;
		winterHeight = std::isfinite(winterHeight) ? winterHeight : 0.0f;
		if (summerMonth == winterMonth)
			return -0.5f * (summerHeight + winterHeight);
		const float phase = std::fmod(month - static_cast<float>(summerMonth) + MonthsPerYear, MonthsPerYear);
		const float winterPhase = std::fmod(static_cast<float>(winterMonth) - static_cast<float>(summerMonth) + MonthsPerYear, MonthsPerYear);
		const float weight = phase <= winterPhase ? phase / winterPhase : (MonthsPerYear - phase) / (MonthsPerYear - winterPhase);
		return -std::lerp(summerHeight, winterHeight, weight);
	}

	/** @brief Advances weather snow by elapsed game hours, with rain melting twice as quickly. */
	inline float Accumulate(float snow, float hours, bool snowing, bool raining, float snowSpeed, float meltSpeed)
	{
		snow = std::isfinite(snow) ? std::clamp(snow, -MaxWeatherAccumulation, MaxWeatherAccumulation) : 0.0f;
		if (!std::isfinite(hours) || hours <= 0.0f)
			return snow;
		const float delta = hours * AccumulationRate(snow, snowing, raining, snowSpeed, meltSpeed);
		if (snowing || raining)
			snow += delta;
		else if (snow > 0.0f)
			snow = std::max(snow + delta, 0.0f);
		else
			snow = std::min(snow + delta, 0.0f);
		return std::clamp(snow, -MaxWeatherAccumulation, MaxWeatherAccumulation);
	}
}
