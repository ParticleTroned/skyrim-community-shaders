#pragma once

#include <cstdint>

namespace ShadowPassPolicy
{
	/** Only lightless utility shadow-map draws are eligible for shadow-only rejection. */
	constexpr bool IsIsolatedShadowPass(bool utility, std::uint32_t technique, std::uint8_t lights, std::uint8_t shadowLights)
	{
		constexpr std::uint32_t utilityTechniqueStart = 0x2B;
		constexpr std::uint32_t renderShadowmap = 1U << 14;
		return utility && technique >= utilityTechniqueStart &&
		       ((technique - utilityTechniqueStart) & renderShadowmap) != 0 && lights == 0 && shadowLights == 0;
	}
}
