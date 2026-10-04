#ifndef CSX_HYBRID_PROJECTED_BOUNDS_HLSLI
#define CSX_HYBRID_PROJECTED_BOUNDS_HLSLI

#include "VRHybridCulling/TraversalDiagnostics.hlsli"

namespace ProjectedBounds
{
	static const uint4 Faces[6] = {
		uint4(0, 1, 3, 2), uint4(4, 6, 7, 5), uint4(0, 4, 5, 1),
		uint4(2, 3, 7, 6), uint4(0, 2, 6, 4), uint4(1, 5, 7, 3)
	};

	struct PreparedFaces
	{
		float4 rectangle[6];
		float4 nearestDepth[2];
	};

	void PrepareFaces(float3 vertices[8], out PreparedFaces bounds)
	{
		bounds.nearestDepth[0] = bounds.nearestDepth[1] = DepthOrder::Far();
		[unroll] for (uint face = 0; face < 6; ++face)
		{
			uint4 corners = Faces[face];
			float3 a = vertices[corners.x], b = vertices[corners.y];
			float3 c = vertices[corners.z], d = vertices[corners.w];
			bounds.rectangle[face] = float4(min(min(a.xy, b.xy), min(c.xy, d.xy)), max(max(a.xy, b.xy), max(c.xy, d.xy)));
			bounds.nearestDepth[face >> 2][face & 3] = DepthOrder::Nearest(DepthOrder::Nearest(a.z, b.z), DepthOrder::Nearest(c.z, d.z));
		}
	}

	bool OccludedInRegion(float3 vertices[8], PreparedFaces bounds, float2 minimumPixel, float2 maximumPixel, float depth, float bias HIZ_DIAGNOSTIC_PARAMETERS)
	{
		// Allow for four rounds of interpolation in addition to the configured depth bias.
		const float interpolationBias = 64.0 / 16777216.0;
		const float guardedBias = bias + interpolationBias;
		[loop] for (uint face = 0; face < 6; ++face)
		{
			if (any(bounds.rectangle[face].zw < minimumPixel) || any(bounds.rectangle[face].xy > maximumPixel) ||
				DepthOrder::IsBehindWithBias(bounds.nearestDepth[face >> 2][face & 3], depth, guardedBias))
				continue;
			uint4 corners = Faces[face];
			[loop] for (uint triangleIndex = 0; triangleIndex < 2; ++triangleIndex)
			{
				HIZ_COUNT_TRIANGLE;
				float3 a = vertices[corners.x];
				float3 b = vertices[triangleIndex == 0 ? corners.y : corners.z];
				float3 c = vertices[triangleIndex == 0 ? corners.z : corners.w];
				float nearest = DepthOrder::Nearest(a.z, DepthOrder::Nearest(b.z, c.z));
				if (DepthOrder::IsBehindWithBias(nearest, depth, guardedBias))
					continue;
				float2 minimumTriangle = min(a.xy, min(b.xy, c.xy));
				float2 maximumTriangle = max(a.xy, max(b.xy, c.xy));
				if (any(maximumTriangle < minimumPixel) || any(minimumTriangle > maximumPixel))
					continue;

				// Only initialized vertices below count are read; four clips need at most seven.
				float3 polygon[8];
				polygon[0] = a;
				polygon[1] = b;
				polygon[2] = c;
				uint count = 3;
				[loop] for (uint plane = 0; plane < 4; ++plane)
				{
					uint axis = plane >> 1;
					bool lower = (plane & 1) == 0;
					float boundary = lower ? minimumPixel[axis] : maximumPixel[axis];
					float sign = lower ? 1.0 : -1.0;
					float3 output[8];
					uint outputCount = 0;
					float3 previous = polygon[count - 1];
					float previousDistance = sign * ((axis == 0 ? previous.x : previous.y) - boundary);
					[loop] for (uint index = 0; index < count; ++index)
					{
						float3 current = polygon[index];
						float distance = sign * ((axis == 0 ? current.x : current.y) - boundary);
						if ((previousDistance >= 0.0) != (distance >= 0.0)) {
							float weight = previousDistance / (previousDistance - distance);
							precise float3 intersection = previous + weight * (current - previous);
							if (!isfinite(weight) || weight < 0.0 || weight > 1.0 || !all(isfinite(intersection)) || outputCount >= 8)
								return false;
							intersection.x = axis == 0 ? boundary : intersection.x;
							intersection.y = axis == 1 ? boundary : intersection.y;
							output[outputCount++] = intersection;
						}
						if (distance >= 0.0) {
							if (outputCount >= 8)
								return false;
							output[outputCount++] = current;
						}
						previous = current;
						previousDistance = distance;
					}
					count = outputCount;
					if (count == 0)
						break;
					[loop] for (uint copyIndex = 0; copyIndex < count; ++copyIndex)
						polygon[copyIndex] = output[copyIndex];
				}
				// Projected depth is affine on each triangle, including reflected boxes.
				nearest = DepthOrder::Far();
				[loop] for (uint index = 0; index < count; ++index)
					nearest = DepthOrder::Nearest(nearest, polygon[index].z);
				if (count != 0 && !DepthOrder::IsBehindWithBias(nearest, depth, guardedBias))
					return false;
			}
		}
		return true;
	}
}

#endif
