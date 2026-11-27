#pragma once

#include "Utils/Finite.h"

#include <algorithm>
#include <cmath>

namespace SnowCoverMath
{
	constexpr float MonthsPerYear = 12.0f;
	constexpr float MaxWeatherAccumulation = 2.0f;

	/** Game-hour rates on either side of calm weather's zero-snow equilibrium. */
	struct AccumulationRates
	{
		float belowZero = 0.0f;
		float aboveZero = 0.0f;

		/** Whether both rates can be used for accumulation and melt-history timing. */
		bool IsFinite() const { return std::isfinite(belowZero) && std::isfinite(aboveZero); }
		/** Selects the local rate, stopping at zero when the two sides converge there. */
		float At(float snow) const
		{
			if (snow < 0.0f)
				return belowZero;
			if (snow > 0.0f)
				return aboveZero;
			// Opposing rates converge at zero instead of oscillating across it.
			return std::max(aboveZero, 0.0f) + std::min(belowZero, 0.0f);
		}
	};

	/** Weather classification and the existing coverage-density input. */
	struct Weather
	{
		bool snowing = false;
		bool raining = false;
		float density = 0.0f;
	};

	/** Shared CPU weather state for snow coverage, survival and melt history. */
	struct WeatherBlend
	{
		AccumulationRates rates;
		float density = 0.0f;
	};

	/** Game-hour accumulation rate, shared with melt history's time-skip endpoint calculation. */
	inline float AccumulationRate(float snow, bool snowing, bool raining, float snowSpeed, float meltSpeed)
	{
		snowSpeed = std::isfinite(snowSpeed) ? std::max(snowSpeed, 0.0f) : 0.0f;
		meltSpeed = std::isfinite(meltSpeed) ? std::max(meltSpeed, 0.0f) : 0.0f;
		return snowing ? snowSpeed : (raining ? -2.0f * meltSpeed : (snow > 0 ? -meltSpeed : (snow < 0 ? meltSpeed : 0.0f)));
	}

	/** Blends weather endpoints; an invalid transition selects the current weather. */
	inline WeatherBlend BlendWeather(Weather previous, Weather current, float transition, float snowSpeed, float meltSpeed)
	{
		transition = Util::ClampFinite(transition, 0.0f, 1.0f, 1.0f);
		const auto rate = [&](float snow) {
			return std::lerp(AccumulationRate(snow, previous.snowing, previous.raining, snowSpeed, meltSpeed),
				AccumulationRate(snow, current.snowing, current.raining, snowSpeed, meltSpeed), transition);
		};
		const auto density = [](float value) { return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f; };
		return { { rate(-1.0f), rate(1.0f) }, std::lerp(density(previous.density), density(current.density), transition) };
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

	/** Advances elapsed game hours, splitting at zero before applying the other side's rate. */
	inline float Accumulate(float snow, float hours, AccumulationRates rates)
	{
		snow = std::isfinite(snow) ? std::clamp(snow, -MaxWeatherAccumulation, MaxWeatherAccumulation) : 0.0f;
		if (!std::isfinite(hours) || hours <= 0.0f || !rates.IsFinite())
			return snow;
		float rate = rates.At(snow);
		double remaining = hours;
		if ((snow > 0.0f && rate < 0.0f) || (snow < 0.0f && rate > 0.0f)) {
			const double crossing = -static_cast<double>(snow) / rate;
			if (crossing <= remaining) {
				remaining -= crossing;
				snow = 0.0f;
				rate = rates.At(0.0f);
			}
		}
		return static_cast<float>(std::clamp(snow + remaining * rate,
			-static_cast<double>(MaxWeatherAccumulation), static_cast<double>(MaxWeatherAccumulation)));
	}

	/** Advances a single weather, retaining rain's doubled melting rate. */
	inline float Accumulate(float snow, float hours, bool snowing, bool raining, float snowSpeed, float meltSpeed)
	{
		return Accumulate(snow, hours, { AccumulationRate(-1.0f, snowing, raining, snowSpeed, meltSpeed), AccumulationRate(1.0f, snowing, raining, snowSpeed, meltSpeed) });
	}
}
