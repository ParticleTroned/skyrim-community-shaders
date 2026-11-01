#pragma once

#include <cmath>

namespace Util::UISmoothing
{
	/** @brief Frame-independent damping for active headset slider and placement edits. */
	inline float Amount(float seconds)
	{
		constexpr float responseSeconds = 0.045f;
		if (!std::isfinite(seconds) || seconds <= 0)
			return 0;
		return -std::expm1(-seconds / responseSeconds);
	}
}
