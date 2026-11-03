#ifndef CSX_DEPTH_ORDER_HLSLI
#define CSX_DEPTH_ORDER_HLSLI

namespace DepthOrder
{
	// Production encoding is selected by the committed renderer generation.
#if defined(REVERSE_Z) || defined(CSX_DEPTH_ORDER_TEST_REVERSED)
	static const bool Reversed = true;
#else
	static const bool Reversed = false;
#endif

	float Near() { return Reversed ? 1.0 : 0.0; }
	float Far() { return Reversed ? 0.0 : 1.0; }
	bool IsNormalizedDepth(float value)
	{
		return isfinite(value) && value >= 0.0 && value <= 1.0;
	}
	/** Finite scene depth includes the near endpoint and excludes the cleared far endpoint. */
	bool IsSceneDepth(float value)
	{
		return IsNormalizedDepth(value) && value != Far();
	}
	bool IsInteriorDepth(float value, float nearMargin)
	{
		return IsNormalizedDepth(value) && abs(value - Near()) > nearMargin && value != Far();
	}
	float Nearest(float first, float second) { return Reversed ? max(first, second) : min(first, second); }
	bool IsNearer(float first, float second) { return Reversed ? first > second : first < second; }
	float Farthest(float first, float second) { return Reversed ? min(first, second) : max(first, second); }

	// A positive bias moves the occluder away; equality never proves occlusion.
	bool IsBehindWithBias(float nearest, float farthest, float bias)
	{
		return Reversed ? nearest < farthest - bias : nearest > farthest + bias;
	}
}
#endif
