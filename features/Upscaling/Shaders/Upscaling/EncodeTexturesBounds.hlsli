#ifndef ENCODE_TEXTURES_BOUNDS_HLSLI
#define ENCODE_TEXTURES_BOUNDS_HLSLI

bool IsEncodeTextureSourceSampleInBounds(
	int2 samplePos,
	int2 trueSamplingDim,
	float2 sourceSamplingXBounds)
{
	// These are full-eye bounds, not foveated dispatch bounds: same-eye
	// donors outside a cropped encode region remain valid source samples.
	int2 samplingXBounds = int2(sourceSamplingXBounds);
	return !any(samplePos < 0) &&
	       !any(samplePos >= trueSamplingDim) &&
	       samplePos.x >= samplingXBounds.x &&
	       samplePos.x < samplingXBounds.y;
}

#endif  // ENCODE_TEXTURES_BOUNDS_HLSLI
