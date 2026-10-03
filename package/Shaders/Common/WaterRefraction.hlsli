#ifndef __WATER_REFRACTION_HLSLI__
#define __WATER_REFRACTION_HLSLI__

namespace WaterRefraction
{

	/** Finds UV bounds whose depth and colour footprints stay on rendered texel centres. */
	bool TryGetUVBounds(float2 refractionRenderSize, float2 depthRenderSize, out float2 minUV, out float2 maxUV)
	{
		minUV = maxUV = 0.0;
		if (!all(isfinite(refractionRenderSize)) || !all(isfinite(depthRenderSize)) ||
			any(refractionRenderSize < 1.0) || any(depthRenderSize < 1.0))
			return false;

		minUV = max(0.5 / refractionRenderSize, 0.5 / depthRenderSize);
		// Flooring preserves physical texel centres at fractional render scales.
		maxUV = min(
			(floor(refractionRenderSize) - 0.5) / refractionRenderSize,
			(floor(depthRenderSize) - 0.5) / depthRenderSize);
		return all(minUV <= maxUV);
	}

	/** Uses the undistorted position when projection fails, then bounds the sample footprint. */
	float2 ClampUV(float2 uv, float2 fallbackUV, float2 minUV, float2 maxUV)
	{
		if (!all(isfinite(uv)))
			uv = all(isfinite(fallbackUV)) ? fallbackUV : (minUV + maxUV) * 0.5;
		return clamp(uv, minUV, maxUV);
	}
}

#endif
