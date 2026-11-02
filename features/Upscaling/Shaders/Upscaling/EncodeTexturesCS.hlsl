#include "Common/ReverseZ.hlsli"
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
#if defined(DEPTH_OUTPUT)
RWTexture2D<float> DepthOutput : register(u3);
#endif

#if defined(DLSS)
// One 8x8 output group shares its 5x5 depth neighborhoods, including a two-pixel halo.
static const uint kEncodeDepthTileDim = 12;
groupshared float DepthTile[kEncodeDepthTileDim * kEncodeDepthTileDim];
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID
#if defined(DLSS)
	,
	uint3 groupThreadID : SV_GroupThreadID
#endif
) {
#if defined(DLSS)
	const int2 tileOrigin = int2(dispatchID.xy - groupThreadID.xy) - 2;
	const uint lane = groupThreadID.y * 8 + groupThreadID.x;
	for (uint index = lane; index < kEncodeDepthTileDim * kEncodeDepthTileDim; index += 64) {
		const int2 samplePos = tileOrigin + int2(index % kEncodeDepthTileDim, index / kEncodeDepthTileDim);
		float sampleDepth = 0.0;
		if (all(samplePos >= 0) && all(samplePos < int2(TrueSamplingDim)))
			sampleDepth = DepthMask[samplePos];
		DepthTile[index] = sampleDepth;
	}
	// Partial groups must also initialize the halo and reach this barrier.
	GroupMemoryBarrierWithGroupSync();
#endif

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
	const uint2 tileCenter = groupThreadID.xy + 2;
	float depth = DepthTile[tileCenter.y * kEncodeDepthTileDim + tileCenter.x];
	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, SharedData::GetScreenDepth(depth));

	// Find longest motion vector in 5x5 neighborhood.
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll] for (int y = -2; y <= 2; y++)
	{
		[unroll] for (int x = -2; x <= 2; x++)
		{
			int2 samplePos = int2(srcCoord) + int2(x, y);
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			const uint2 tilePos = uint2(int2(tileCenter) + int2(x, y));
			float neighborDepth = DepthTile[tilePos.y * kEncodeDepthTileDim + tilePos.x];
			if (FrameBuffer::IsNearerDepth(neighborDepth, depth)) {
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

#if defined(DEPTH_OUTPUT)
	// Both FidelityFX paths consume typed standard depth, independent of scene convention.
	DepthOutput[srcCoord] = FrameBuffer::ToStandardDepth(DepthMask[srcCoord]);
#endif

	ReactiveMask[srcCoord] = reactiveMask;
	TransparencyCompositionMask[srcCoord] = transparencyCompositionMask;
}
