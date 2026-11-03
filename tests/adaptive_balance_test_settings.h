#pragma once

#include <array>

namespace AdaptiveBalanceTest
{
	using Appearance = std::array<float, 88>;

	/** Neutral nested GPU settings; the shader tests verify the complete reflected layout. */
	constexpr Appearance NeutralAppearance()
	{
		Appearance result{};
		result.fill(1.0f);
		result[47] = 0.0f;  // Custom godray tint contributes only when explicitly requested.
		result[53] = result[54] = result[55] = 0.0f;
		result[56] = 0.0f;
		result[57] = 0.0f;
		result[58] = 0.0f;
		result[59] = 0.0f;
		result[61] = 0.0f;
		result[63] = 0.0f;
		result[65] = 0.0f;
		result[67] = 0.0f;
		result[69] = 0.0f;
		result[71] = 0.0f;
		result[73] = 0.0f;
		result[75] = 0.0f;
		result[77] = 0.0f;
		result[79] = 0.0f;
		result[81] = 0.0f;
		result[86] = result[87] = 0.0f;
		return result;
	}
}
