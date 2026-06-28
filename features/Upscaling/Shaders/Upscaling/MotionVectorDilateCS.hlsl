// Restores the dev-branch motion-vector dilation (removed with EncodeTexturesCS in 13257ec1).
//
// Skyrim only writes motion vectors from opaque geometry passes, so alpha-tested foliage/trees, the sky,
// and the character render with ZERO motion vectors (depth is written by every pass, which is why depth
// "fills the screen" but MV does not). Feeding those zero-MV pixels straight to DLSS/FSR/XeSS ghosts them
// under camera motion. This pass fills each zero-MV pixel with the longest motion vector in its 5x5
// neighbourhood (the dev branch's approach), spreading the nearest valid motion into the gaps. Pixels that
// already carry a motion vector are passed through unchanged, so valid motion is never altered.

cbuffer MVDilateData : register(b0)
{
	float2 RenderDim;  // dynamic render resolution (sub-rect actually written by the engine)
	float2 pad0;
};

Texture2D<float2> MotionVectorInput : register(t0);
RWTexture2D<float2> MotionVectorOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	if (any(dispatchID.xy >= uint2(RenderDim)))
		return;

	const float2 mv = MotionVectorInput[dispatchID.xy];
	if (dot(mv, mv) > 1e-12) {
		MotionVectorOutput[dispatchID.xy] = mv;  // already has motion — keep it exactly
		return;
	}

	// Zero-MV gap: take the longest motion vector in the 5x5 neighbourhood (spreads foreground motion
	// into thin-geometry / alpha-test holes the same way the dev EncodeTexturesCS dilation did).
	float2 best = mv;
	float bestLenSq = 0.0;
	[unroll] for (int y = -2; y <= 2; ++y)
	{
		[unroll] for (int x = -2; x <= 2; ++x)
		{
			const int2 p = int2(dispatchID.xy) + int2(x, y);
			if (any(p < 0) || any(p >= int2(RenderDim)))
				continue;
			const float2 n = MotionVectorInput[p];
			const float lenSq = dot(n, n);
			if (lenSq > bestLenSq) {
				bestLenSq = lenSq;
				best = n;
			}
		}
	}
	MotionVectorOutput[dispatchID.xy] = best;
}
