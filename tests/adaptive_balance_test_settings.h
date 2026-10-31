#pragma once

#include <array>

namespace AdaptiveBalanceTest
{
	using Appearance = std::array<float, 56>;

	/** Neutral nested GPU settings; the shader tests verify the complete reflected layout. */
	constexpr Appearance NeutralAppearance()
	{
		Appearance result{};
		result.fill(1.0f);
		result[47] = 0.0f;  // Custom godray tint contributes only when explicitly requested.
		result[53] = result[54] = result[55] = 0.0f;
		return result;
	}
}
