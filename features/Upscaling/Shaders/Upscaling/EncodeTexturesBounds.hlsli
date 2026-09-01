#ifndef ENCODE_TEXTURES_BOUNDS_HLSLI
#define ENCODE_TEXTURES_BOUNDS_HLSLI

/// @brief Test full-eye source bounds, including valid donors outside a foveated crop.
/// @pre sourceSamplingXBounds is a non-empty [minX, maxX) interval inside trueSamplingDim.
bool IsEncodeTextureSourceSampleInBounds(
	int2 samplePos,
	int2 trueSamplingDim,
	float2 sourceSamplingXBounds)
{
	// Unsigned deltas fold each lower/upper pair into one comparison.
	int2 samplingXBounds = int2(sourceSamplingXBounds);
	return uint(samplePos.y) < uint(trueSamplingDim.y) &&
	       uint(samplePos.x - samplingXBounds.x) < uint(samplingXBounds.y - samplingXBounds.x);
}

#endif  // ENCODE_TEXTURES_BOUNDS_HLSLI
