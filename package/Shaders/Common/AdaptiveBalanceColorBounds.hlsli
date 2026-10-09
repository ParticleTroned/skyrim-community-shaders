#ifndef __ADAPTIVE_BALANCE_COLOR_BOUNDS_HLSLI__
#define __ADAPTIVE_BALANCE_COLOR_BOUNDS_HLSLI__

namespace AdaptiveBalanceColorBounds
{
	static const float kOutputMax = 65504.0;
	static const float kLinearIntermediateMax = 1e30;
	static const float kGammaIntermediateMax = pow(kLinearIntermediateMax, 1.0 / 2.2);

	/// Leave headroom for saturation and intensity before encoding to the scene's half-float targets.
	float3 Intermediate(float3 color, bool linearSpace)
	{
		const float limit = linearSpace ? kLinearIntermediateMax : kGammaIntermediateMax;
		return clamp(color, -limit, limit);
	}

	/// Retain HDR and signed channels while preventing infinities in half-float scene storage.
	float3 Output(float3 color)
	{
		return clamp(color, -kOutputMax, kOutputMax);
	}
}

#endif
