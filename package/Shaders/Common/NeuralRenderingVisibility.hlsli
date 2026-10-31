#ifndef NR_VISIBILITY_HLSLI
#define NR_VISIBILITY_HLSLI

namespace NeuralRenderingVisibility
{
	float LinearDepth(float rawDepth, float4 linearization)
	{
		return linearization.w / max(-rawDepth * linearization.z + linearization.x, 1.0e-6);
	}

	bool Visible(float currentDepth, float authoredDepth, float threshold)
	{
		const float tolerance = max(1.0, max(abs(authoredDepth), abs(currentDepth)) * threshold);
		return currentDepth + tolerance >= authoredDepth;
	}
}

#endif
