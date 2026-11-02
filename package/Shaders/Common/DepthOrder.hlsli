#if !(defined(CSX_DEPTH_ORDER_HLSLI))
#define CSX_DEPTH_ORDER_HLSLI

#	ifdef REVERSE_Z
#		include "Common/FrameBuffer.hlsli"
#	endif

namespace DepthOrder
{
	// Float storage can be active before SE's first world camera reverses. The actual
	// uploaded projection selects depth order; test-only defines cover standalone math.
	bool IsReversed()
	{
#	if defined(CSX_DEPTH_ORDER_TEST_REVERSED)
		return true;
#	elif defined(CSX_DEPTH_ORDER_TEST_STANDARD)
		return false;
#	elif defined(REVERSE_Z)
		return FrameBuffer::CameraProj[2][2] * FrameBuffer::CameraProj[3][2] < 0.0;
#	else
		return false;
#	endif
	}

	float Near() { return IsReversed() ? 1.0 : 0.0; }
	float Far() { return IsReversed() ? 0.0 : 1.0; }
	bool IsInteriorDepth(float value, float nearMargin)
	{
		return isfinite(value) && value >= 0.0 && value <= 1.0 && abs(value - Near()) > nearMargin && value != Far();
	}
	float Nearest(float first, float second) { return IsReversed() ? max(first, second) : min(first, second); }
	bool IsNearer(float first, float second) { return IsReversed() ? first > second : first < second; }
	float Farthest(float first, float second) { return IsReversed() ? min(first, second) : max(first, second); }

	// A positive bias moves the occluder away; equality never proves occlusion.
	bool IsBehindWithBias(float nearest, float farthest, float bias)
	{
		return IsReversed() ? nearest < farthest - bias : nearest > farthest + bias;
	}
}
#endif
