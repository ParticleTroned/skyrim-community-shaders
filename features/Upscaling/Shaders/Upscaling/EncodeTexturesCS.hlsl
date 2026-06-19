#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);
RWTexture2D<float2> MotionVectorOutput : register(u2);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint2 srcCoord = dispatchID.xy;
	if (any(srcCoord >= uint2(TrueSamplingDim)))
		return;

	float2 taaMask = TAAMask[srcCoord];
	float transparencyCompositionMask = NormalsWaterMask[srcCoord].z;
	float reactiveMask = taaMask.x * 0.1 + taaMask.y;

#if defined(DLSS) || defined(FSR)
	float2 motionVector = MotionVectorMask[srcCoord];
	float2 outputMotionVector = motionVector;
#endif

#if defined(DLSS)
	float depth = DepthMask[srcCoord];
	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, SharedData::GetScreenDepth(depth));

	// Find longest motion vector in 5x5 neighborhood.
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll]
	for (int y = -2; y <= 2; y++) {
		[unroll]
		for (int x = -2; x <= 2; x++) {
			int2 samplePos = int2(srcCoord) + int2(x, y);
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			float neighborDepth = DepthMask[samplePos];
			if (neighborDepth < depth) {
				float2 neighborMotionVector = MotionVectorMask[samplePos];
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq) {
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
			}
		}
	}

	outputMotionVector = lerp(longestMotionVector, motionVector, nearFactor);
#endif

#if defined(DLSS) || defined(FSR)
	MotionVectorOutput[srcCoord] = outputMotionVector;
#endif

	ReactiveMask[srcCoord] = reactiveMask;
	TransparencyCompositionMask[srcCoord] = transparencyCompositionMask;
}
