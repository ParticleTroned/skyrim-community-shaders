#ifndef NR_VISIBILITY_HLSLI
#define NR_VISIBILITY_HLSLI

#include "Common/DepthOrder.hlsli"

namespace NeuralRenderingVisibility
{
	float LinearDepth(float rawDepth, float4 linearization)
	{
		return linearization.w / max(DepthOrder::Reversed ? linearization.y + rawDepth * linearization.z : linearization.x - rawDepth * linearization.z, 1.0e-6);
	}

	bool Visible(float currentDepth, float authoredDepth, float threshold)
	{
		const float tolerance = max(1.0, max(abs(authoredDepth), abs(currentDepth)) * threshold);
		return currentDepth + tolerance >= authoredDepth;
	}
}

#endif
