#ifndef CSX_HYBRID_PROJECTED_BOUNDS_HLSLI
#define CSX_HYBRID_PROJECTED_BOUNDS_HLSLI

namespace ProjectedBounds
{

	static const uint3 Triangles[12] = {
		uint3(0, 1, 3), uint3(0, 3, 2), uint3(4, 6, 7), uint3(4, 7, 5),
		uint3(0, 4, 5), uint3(0, 5, 1), uint3(2, 3, 7), uint3(2, 7, 6),
		uint3(0, 2, 6), uint3(0, 6, 4), uint3(1, 5, 7), uint3(1, 7, 3)
	};

	// Four clips grow a triangle to at most seven vertices; spare capacity fails safely.
	bool ClipPlane(inout float3 polygon[8], inout uint count, uint axis, float boundary, float sign)
	{
		float3 output[8] = (float3[8])0;
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
		[loop] for (uint copyIndex = 0; copyIndex < count; ++copyIndex)
			polygon[copyIndex] = output[copyIndex];
		return true;
	}

	bool OccludedInRegion(float3 vertices[8], float2 minimumPixel, float2 maximumPixel, float depth, float bias)
	{
		// Allow for four rounds of interpolation in addition to the configured depth bias.
		const float interpolationBias = 64.0 / 16777216.0;
		[loop] for (uint faceTriangle = 0; faceTriangle < 12; ++faceTriangle)
		{
			float3 polygon[8] = (float3[8])0;
			polygon[0] = vertices[Triangles[faceTriangle].x];
			polygon[1] = vertices[Triangles[faceTriangle].y];
			polygon[2] = vertices[Triangles[faceTriangle].z];
			float2 minimumTriangle = min(polygon[0].xy, min(polygon[1].xy, polygon[2].xy));
			float2 maximumTriangle = max(polygon[0].xy, max(polygon[1].xy, polygon[2].xy));
			if (any(maximumTriangle < minimumPixel) || any(minimumTriangle > maximumPixel))
				continue;
			uint count = 3;
			[loop] for (uint plane = 0; plane < 4; ++plane)
			{
				uint axis = plane >> 1;
				bool lower = (plane & 1) == 0;
				if (!ClipPlane(polygon, count, axis, lower ? minimumPixel[axis] : maximumPixel[axis], lower ? 1.0 : -1.0))
					return false;
				if (count == 0)
					break;
			}
			// Projected z/w is affine on each face triangle, so extrema are at clipped vertices.
			float nearest = DepthOrder::Far();
			[loop] for (uint index = 0; index < count; ++index)
				nearest = DepthOrder::Nearest(nearest, polygon[index].z);
			if (count != 0 && !DepthOrder::IsBehindWithBias(nearest, depth, bias + interpolationBias))
				return false;
		}
		return true;
	}

}

#endif
