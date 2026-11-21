#pragma once

#include "Utils/Finite.h"

#include <algorithm>

namespace SkinWetness
{
	/** Independent stamina sliders define an ordered ramp; equal endpoints disable sweat. */
	inline float Sweat(float a_stamina, float a_start, float a_full)
	{
		const auto minimum = std::min(a_start, a_full);
		const auto maximum = std::max(a_start, a_full);
		return maximum - minimum > 1e-5f ? std::clamp((maximum - a_stamina) / (maximum - minimum), 0.0f, 1.0f) : 0.0f;
	}

	/** Full-scale water and sweat both dry in the configured number of seconds. */
	inline float Fade(float a_previous, float a_target, float a_seconds, float a_duration, float a_fullScale)
	{
		if (a_duration <= 0)
			return a_target;
		return std::max(a_target, a_previous - a_fullScale * a_seconds / a_duration);
	}
}
