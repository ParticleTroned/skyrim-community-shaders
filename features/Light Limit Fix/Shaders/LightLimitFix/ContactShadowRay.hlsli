#ifndef __LLF_CONTACT_SHADOW_RAY_HLSL__
#define __LLF_CONTACT_SHADOW_RAY_HLSL__

namespace LightLimitFix
{
	/** Returns the active integer depth extent, with at least one texel. */
	uint2 GetContactShadowRenderDimensions(float2 bufferDimensions, float2 resolutionScale)
	{
		const uint2 minimumDimensions = uint2(1, 1);
		return max(uint2(bufferDimensions * saturate(resolutionScale)), minimumDimensions);
	}

	/** Restricts point depth loads to the active single-view depth extent. */
	int2 ClampContactShadowDepthPixel(int2 pixel, uint2 renderDimensions)
	{
		return clamp(pixel, int2(0, 0), int2(renderDimensions) - 1);
	}

	/** Evaluates view depth at a screen-linear ray position from positive endpoint inverse depths. */
	float GetContactShadowRayDepth(float2 inverseDepth, float t)
	{
		return rcp(lerp(inverseDepth.x, inverseDepth.y, t));
	}
}

#endif
