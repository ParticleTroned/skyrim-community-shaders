#ifndef ENCODE_TEXTURES_BOUNDS_HLSLI
#define ENCODE_TEXTURES_BOUNDS_HLSLI

bool IsEncodeTextureSourceSampleInBounds(
	int2 samplePos,
	int2 trueSamplingDim,
	float2 sourceSamplingXBounds)
{
	// These are full-eye bounds, not foveated dispatch bounds: same-eye
	// donors outside a cropped encode region remain valid source samples.
	// The host guarantees a non-empty X interval inside trueSamplingDim;
	// unsigned deltas fold each lower/upper pair into one comparison.
	int2 samplingXBounds = int2(sourceSamplingXBounds);
	return uint(samplePos.y) < uint(trueSamplingDim.y) &&
	       uint(samplePos.x - samplingXBounds.x) < uint(samplingXBounds.y - samplingXBounds.x);
}

#endif  // ENCODE_TEXTURES_BOUNDS_HLSLI
