#ifndef GRASS_OCCLUSION_HLSLI
#define GRASS_OCCLUSION_HLSLI

#include "Common/DepthOrder.hlsli"

// Move the nearest bound toward the camera by a fraction of its view distance.
float GrassOcclusionMargin(float asymptote, uint projectionMode, float nearest, float farthest, float tolerance, float viewportSlack)
{
	const float floatEpsilon = 1.192092896e-7;
	if (projectionMode == 0 || !isfinite(tolerance) || tolerance < 0 || tolerance >= 1 || !isfinite(viewportSlack) || viewportSlack < 0)
		return 1;
	// Orthographic passes retain an absolute depth margin without perspective distance.
	float margin = tolerance;
	if (projectionMode == 2)
		margin = abs(nearest - asymptote) * tolerance / (1 - tolerance);
	// Forward D24 quantization and reversed float arithmetic require different floors.
	float precision = 8 * floatEpsilon * (DepthOrder::Reversed ? max(abs(asymptote), max(nearest, farthest)) : 1);
	return margin + precision + viewportSlack;
}

#endif
