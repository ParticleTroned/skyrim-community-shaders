#ifndef CSX_DEPTH_ORDER_HLSLI
#define CSX_DEPTH_ORDER_HLSLI

namespace DepthOrder
{

// Reversed ordering is exercised only by standalone shader tests.
#ifdef CSX_DEPTH_ORDER_TEST_REVERSED
	static const bool Reversed = true;
#else
	static const bool Reversed = false;
#endif

	float Near()
	{
		return Reversed ? 1.0 : 0.0;
	}

	float Far()
	{
		return Reversed ? 0.0 : 1.0;
	}

	float Nearest(float first, float second)
	{
		return Reversed ? max(first, second) : min(first, second);
	}

	float Farthest(float first, float second)
	{
		return Reversed ? min(first, second) : max(first, second);
	}

	// Move the occluder away from the camera so equality never proves occlusion.
	bool IsBehindWithBias(float nearest, float farthest, float bias)
	{
		return Reversed ? nearest < farthest - bias : nearest > farthest + bias;
	}

}

#endif
