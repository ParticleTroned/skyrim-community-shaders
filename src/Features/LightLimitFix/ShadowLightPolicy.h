#pragma once

#include <cstdint>

namespace LightLimitFixShadowPolicy
{
	// Skyrim's shadow mask is a float4. Values outside these channels mean
	// that the light has no shadow sample this frame, not that it emits no light.
	inline constexpr std::uint32_t kShadowMaskChannelCount = 4;

	[[nodiscard]] constexpr bool IsValidShadowMask(std::uint32_t a_maskIndex)
	{
		return a_maskIndex < kShadowMaskChannelCount;
	}

	[[nodiscard]] constexpr float ResolveEffectiveLodDimmer(
		bool a_isShadowLight,
		bool a_lodFade,
		float a_lodDimmer)
	{
		// Shadow-distance LOD can hard-zero lodDimmer before releasing the mask.
		// Recover only that terminal state; partial fades remain engine-controlled.
		const bool lodFaded = a_isShadowLight && a_lodFade && a_lodDimmer == 0.0f;
		return lodFaded ? 1.0f : a_lodDimmer;
	}
}
