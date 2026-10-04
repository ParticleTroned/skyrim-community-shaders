#ifndef CSX_HYBRID_PROJECTED_BOUNDS_HLSLI
#define CSX_HYBRID_PROJECTED_BOUNDS_HLSLI

#include "VRHybridCulling/TraversalDiagnostics.hlsli"

namespace ProjectedBounds
{
	// Invocation-private storage avoids SM5 array-parameter copies; initialize all eight vertices per eye.
	static float3 Vertices[8];
	static const uint4 Faces[6] = {
		uint4(0, 1, 3, 2), uint4(4, 6, 7, 5), uint4(0, 4, 5, 1),
		uint4(2, 3, 7, 6), uint4(0, 2, 6, 4), uint4(1, 5, 7, 3)
	};

	// Indexed invocation-private metadata avoids repeated SM5 struct selection in each region.
	static float4 FaceRectangles[6];
	static float FaceNearestDepth[6];
	static uint PlaneKeys[4];
	static float3 PlaneNormals[4];
	static float3 PlaneMagnitudes[4];
#ifdef CSX_HIZ_POLYGON_BASELINE
	static const bool DirectIntersectionEnabled = false;
#else
	static const bool DirectIntersectionEnabled = true;
#endif

	/// Requires all eight vertices to be initialized for the current eye.
	void PrepareFaces()
	{
		[unroll] for (uint slot = 0; slot < 4; ++slot) PlaneKeys[slot] = 0xffffffff;
		[unroll] for (uint face = 0; face < 6; ++face)
		{
			uint4 corners = Faces[face];
			float3 a = Vertices[corners.x], b = Vertices[corners.y];
			float3 c = Vertices[corners.z], d = Vertices[corners.w];
			FaceRectangles[face] = float4(min(min(a.xy, b.xy), min(c.xy, d.xy)), max(max(a.xy, b.xy), max(c.xy, d.xy)));
			FaceNearestDepth[face] = DepthOrder::Nearest(DepthOrder::Nearest(a.z, b.z), DepthOrder::Nearest(c.z, d.z));
		}
	}

	/// Keeps inclusive boundary vertices that fail the existing guarded depth comparison.
	bool IsUnresolvedVertex(float3 vertex, float2 minimumPixel, float2 maximumPixel, float depth, float guardedBias)
	{
		return all(vertex.xy >= minimumPixel) && all(vertex.xy <= maximumPixel) &&
		       !DepthOrder::IsBehindWithBias(vertex.z, depth, guardedBias);
	}

	/// Finds an original vertex whose retained depth prevents a complete region proof.
	bool HasUnresolvedVertex(float3 a, float3 b, float3 c, float2 minimumPixel, float2 maximumPixel, float depth, float guardedBias)
	{
		return IsUnresolvedVertex(a, minimumPixel, maximumPixel, depth, guardedBias) ||
		       IsUnresolvedVertex(b, minimumPixel, maximumPixel, depth, guardedBias) ||
		       IsUnresolvedVertex(c, minimumPixel, maximumPixel, depth, guardedBias);
	}

	/// Proves a triangle's bounding-rectangle overlap hidden; uncertainty needs exact clipping.
	/// Requires finite vertices in [0,16384] pixels/[0,1] depth and validated region/depth/bias inputs.
	void BuildPlane(float3 a, float3 b, float3 c, out float3 normal, out float3 crossMagnitude)
	{
		precise float3 firstEdge = b - a;
		precise float3 secondEdge = c - a;
		precise float3 firstProducts = firstEdge.yzx * secondEdge.zxy;
		precise float3 secondProducts = firstEdge.zxy * secondEdge.yzx;
		precise float3 computedNormal = firstProducts - secondProducts;
		precise float3 computedMagnitude = abs(firstProducts) + abs(secondProducts);
		normal = computedNormal;
		crossMagnitude = computedMagnitude;
	}

	bool PreparedPlaneProvesOccluded(float3 a, float3 normal, float3 crossMagnitude, float2 minimumRegion, float2 maximumRegion, float depth, float guardedBias)
	{
		if (any(minimumRegion > maximumRegion))
			return false;
		// 64 float unit roundoffs cover the cross/residual chain.
		// The floor covers subnormals amplified by the admitted 16384-pixel bounds.
		const float roundoffFactor = 64.0 / 16777216.0;
		const float roundoffFloor = 1e-20;
		precise float areaError = roundoffFactor * crossMagnitude.z + roundoffFloor;
		if (!all(isfinite(normal)) || !all(isfinite(crossMagnitude)) || abs(normal.z) <= areaError)
			return false;
		normal = normal.z < 0.0 ? -normal : normal;

		precise float guardedDepth = DepthOrder::Reversed ? depth - guardedBias : depth + guardedBias;
		precise float depthDifference = DepthOrder::Reversed ? guardedDepth - a.z : a.z - guardedDepth;
		float2 gradient = DepthOrder::Reversed ? -normal.xy : normal.xy;
		float2 limitingCorner = float2(gradient.x >= 0.0 ? maximumRegion.x : minimumRegion.x,
			gradient.y >= 0.0 ? maximumRegion.y : minimumRegion.y);
		precise float2 displacement = limitingCorner - a.xy;
		precise float residual = normal.z * depthDifference - gradient.x * displacement.x - gradient.y * displacement.y;
		precise float2 maximumDisplacement = max(abs(minimumRegion - a.xy), abs(maximumRegion - a.xy));
		precise float roundoffScale = dot(crossMagnitude, float3(maximumDisplacement, abs(depthDifference)));
		precise float residualError = roundoffFactor * roundoffScale + roundoffFloor;
		// The rectangle contains every covered triangle point, so its minimum is a conservative bound.
		return isfinite(residual) && isfinite(residualError) && residual > residualError;
	}

	/// A strict separating edge excludes the complete rectangle; uncertain orientation needs clipping.
	bool TriangleOutsideRegion(float3 a, float3 b, float3 c, float3 normal, float3 magnitude, float2 minimumPixel, float2 maximumPixel,
		out float3 firstEdge, out float3 secondEdge, out float3 closingEdge)
	{
		firstEdge = secondEdge = closingEdge = 0;
		const float roundoffFactor = 64.0 / 16777216.0;
		const float roundoffFloor = 1e-20;
		if (!all(isfinite(normal)) || !all(isfinite(magnitude)) || abs(normal.z) <= roundoffFactor * magnitude.z + roundoffFloor)
			return false;
		float orientation = normal.z < 0.0 ? -1.0 : 1.0;
		[unroll] for (uint index = 0; index < 3; ++index)
		{
			float2 start = index == 0 ? a.xy : (index == 1 ? b.xy : c.xy);
			precise float3 edge3 = (index == 0 ? b : (index == 1 ? c : a)) - (index == 0 ? a : (index == 1 ? b : c));
			if (index == 0)
				firstEdge = edge3;
			else if (index == 1)
				secondEdge = edge3;
			else
				closingEdge = edge3;
			float2 edge = edge3.xy;
			float2 gradient = orientation * float2(-edge.y, edge.x);
			float2 corner = float2(gradient.x >= 0.0 ? maximumPixel.x : minimumPixel.x,
				gradient.y >= 0.0 ? maximumPixel.y : minimumPixel.y);
			precise float2 displacement = corner - start;
			precise float residual = orientation * (edge.x * displacement.y - edge.y * displacement.x);
			precise float2 maximumDisplacement = max(abs(minimumPixel - start), abs(maximumPixel - start));
			precise float error = roundoffFactor * dot(abs(edge), maximumDisplacement.yx) + roundoffFloor;
			if (isfinite(residual) && isfinite(error) && residual < -error)
				return true;
		}
		return false;
	}

	bool TriangleOutsideRegion(float3 a, float3 b, float3 c, float3 normal, float3 magnitude, float2 minimumPixel, float2 maximumPixel)
	{
		float3 firstEdge, secondEdge, closingEdge;
		return TriangleOutsideRegion(a, b, c, normal, magnitude, minimumPixel, maximumPixel, firstEdge, secondEdge, closingEdge);
	}

	/// Bounds the depth-unresolved triangle portion; uncertain intersections retain polygon clipping.
	bool DirectIntersectionProvesOccluded(float3 a, float3 b, float3 c, float3 firstEdge, float3 secondEdge, float3 closingEdge,
		float3 normal, float3 magnitude, float2 minimumPixel, float2 maximumPixel, float depth, float guardedBias)
	{
		const float roundoffFactor = 64.0 / 16777216.0;
		const float roundoffFloor = 1e-20;
		if (!all(isfinite(normal)) || !all(isfinite(magnitude)) || abs(normal.z) <= roundoffFactor * magnitude.z + roundoffFloor)
			return false;
		precise float guardedDepth = DepthOrder::Reversed ? depth - guardedBias : depth + guardedBias;
		precise float depthError = roundoffFactor * (abs(guardedDepth) + max(abs(a.z), max(abs(b.z), abs(c.z)))) + roundoffFloor;
		precise float threshold = DepthOrder::Reversed ? guardedDepth - depthError : guardedDepth + depthError;
		float2 minimumFront = 3.402823466e+38;
		float2 maximumFront = -3.402823466e+38;
		bool hasFront = false;
		[unroll] for (uint index = 0; index < 3; ++index)
		{
			float3 start = index == 0 ? a : (index == 1 ? b : c);
			float3 end = index == 0 ? b : (index == 1 ? c : a);
			float3 edge = index == 0 ? firstEdge : (index == 1 ? secondEdge : closingEdge);
			bool startFront = DepthOrder::Reversed ? start.z >= threshold : start.z <= threshold;
			bool endFront = DepthOrder::Reversed ? end.z >= threshold : end.z <= threshold;
			if (startFront) {
				minimumFront = min(minimumFront, start.xy);
				maximumFront = max(maximumFront, start.xy);
				hasFront = true;
			}
			if (startFront != endFront) {
				precise float weight = (threshold - start.z) / edge.z;
				precise float weightError = roundoffFactor * (1.0 + abs(weight));
				precise float2 intersection = start.xy + weight * edge.xy;
				precise float2 coordinateError = abs(edge.xy) * weightError +
				                                 roundoffFactor * (abs(start.xy) + abs(edge.xy)) + roundoffFloor;
				if (!isfinite(weight) || weight < 0.0 || weight > 1.0 || !all(isfinite(intersection)) || !all(isfinite(coordinateError)))
					return false;
				minimumFront = min(minimumFront, intersection - coordinateError);
				maximumFront = max(maximumFront, intersection + coordinateError);
				hasFront = true;
			}
		}
		// Any covered point that fails depth is inside these outward-rounded front bounds.
		return !hasFront || any(maximumFront < minimumPixel) || any(minimumFront > maximumPixel);
	}

	bool PlaneProvesOccluded(float3 a, float3 b, float3 c, float2 minimumPixel, float2 maximumPixel, float depth, float guardedBias)
	{
		float3 normal, magnitude;
		BuildPlane(a, b, c, normal, magnitude);
		return PreparedPlaneProvesOccluded(a, normal, magnitude, max(minimumPixel, min(a.xy, min(b.xy, c.xy))),
			min(maximumPixel, max(a.xy, max(b.xy, c.xy))), depth, guardedBias);
	}

	bool OccludedInRegion(float2 minimumPixel, float2 maximumPixel, float depth, float bias HIZ_DIAGNOSTIC_PARAMETERS)
	{
		// Allow for four rounds of interpolation in addition to the configured depth bias.
		const float interpolationBias = 64.0 / 16777216.0;
		const float guardedBias = bias + interpolationBias;
		[loop] for (uint face = 0; face < 6; ++face)
		{
			if (any(FaceRectangles[face].zw < minimumPixel) || any(FaceRectangles[face].xy > maximumPixel) ||
				DepthOrder::IsBehindWithBias(FaceNearestDepth[face], depth, guardedBias))
				continue;
			uint4 corners = Faces[face];
			[loop] for (uint triangleIndex = 0; triangleIndex < 2; ++triangleIndex)
			{
				HIZ_COUNT_TRIANGLE;
				float3 a = Vertices[corners.x];
				float3 b = Vertices[triangleIndex == 0 ? corners.y : corners.z];
				float3 c = Vertices[triangleIndex == 0 ? corners.z : corners.w];
				float nearest = DepthOrder::Nearest(a.z, DepthOrder::Nearest(b.z, c.z));
				if (DepthOrder::IsBehindWithBias(nearest, depth, guardedBias))
					continue;
				float2 minimumTriangle = min(a.xy, min(b.xy, c.xy));
				float2 maximumTriangle = max(a.xy, max(b.xy, c.xy));
				if (any(maximumTriangle < minimumPixel) || any(minimumTriangle > maximumPixel))
					continue;
				if (HasUnresolvedVertex(a, b, c, minimumPixel, maximumPixel, depth, guardedBias))
					return false;
				uint planeKey = face * 2 + triangleIndex;
				uint planeSlot = planeKey & 3;
				if (PlaneKeys[planeSlot] != planeKey) {
					float3 normal, magnitude;
					BuildPlane(a, b, c, normal, magnitude);
					PlaneNormals[planeSlot] = normal;
					PlaneMagnitudes[planeSlot] = magnitude;
					PlaneKeys[planeSlot] = planeKey;
					HIZ_COUNT_PLANE_BUILD;
				} else {
					HIZ_COUNT_PLANE_REUSE;
				}
				if (PreparedPlaneProvesOccluded(a, PlaneNormals[planeSlot], PlaneMagnitudes[planeSlot],
						max(minimumPixel, minimumTriangle), min(maximumPixel, maximumTriangle), depth, guardedBias)) {
					HIZ_COUNT_PLANE_PROOF;
					continue;
				}

				HIZ_COUNT_TRIANGLE_REGION;
				float3 firstEdge, secondEdge, closingEdge;
				if (TriangleOutsideRegion(a, b, c, PlaneNormals[planeSlot], PlaneMagnitudes[planeSlot], minimumPixel, maximumPixel,
						firstEdge, secondEdge, closingEdge)) {
					HIZ_COUNT_DISJOINT_TRIANGLE;
					continue;
				}
				if (DirectIntersectionEnabled) {
					HIZ_COUNT_DIRECT_TEST;
					if (DirectIntersectionProvesOccluded(a, b, c, firstEdge, secondEdge, closingEdge,
							PlaneNormals[planeSlot], PlaneMagnitudes[planeSlot], minimumPixel, maximumPixel, depth, guardedBias)) {
						HIZ_COUNT_DIRECT_PROOF;
						continue;
					}
					HIZ_COUNT_DIRECT_FALLBACK;
				}

				HIZ_COUNT_POLYGON_CLIP;
				// Alternate eight-vertex banks so survivors need no copy; read only initialized entries.
				float3 polygon[16];
				polygon[0] = a;
				polygon[1] = b;
				polygon[2] = c;
				uint count = 3;
				uint polygonBase = 0;
				[loop] for (uint plane = 0; plane < 4; ++plane)
				{
					uint axis = plane >> 1;
					bool lower = (plane & 1) == 0;
					float boundary = lower ? minimumPixel[axis] : maximumPixel[axis];
					float sign = lower ? 1.0 : -1.0;
					// Check interpolated survivors too: their rounded coordinates can leave the original extent.
					if (lower ? minimumTriangle[axis] >= boundary : maximumTriangle[axis] <= boundary) {
						bool contained = true;
						[loop] for (uint index = 0; index < count; ++index)
						{
							HIZ_COUNT_CLIP_VERTEX;
							contained = contained && sign * (polygon[polygonBase + index][axis] - boundary) >= 0.0;
						}
						if (contained) {
							HIZ_COUNT_CLIP_SKIP;
							continue;
						}
					}
					HIZ_COUNT_CLIP_PLANE;
					uint outputBase = polygonBase ^ 8;
					uint outputCount = 0;
					float3 previous = polygon[polygonBase + count - 1];
					float previousDistance = sign * ((axis == 0 ? previous.x : previous.y) - boundary);
					[loop] for (uint index = 0; index < count; ++index)
					{
						HIZ_COUNT_CLIP_VERTEX;
						float3 current = polygon[polygonBase + index];
						float distance = sign * ((axis == 0 ? current.x : current.y) - boundary);
						if ((previousDistance >= 0.0) != (distance >= 0.0)) {
							float weight = previousDistance / (previousDistance - distance);
							precise float3 intersection = previous + weight * (current - previous);
							if (!isfinite(weight) || weight < 0.0 || weight > 1.0 || !all(isfinite(intersection)) || outputCount >= 8)
								return false;
							intersection.x = axis == 0 ? boundary : intersection.x;
							intersection.y = axis == 1 ? boundary : intersection.y;
							polygon[outputBase + outputCount++] = intersection;
						}
						if (distance >= 0.0) {
							if (outputCount >= 8)
								return false;
							polygon[outputBase + outputCount++] = current;
						}
						previous = current;
						previousDistance = distance;
					}
					count = outputCount;
					polygonBase = outputBase;
					if (count == 0)
						break;
				}
				// Projected depth is affine on each triangle, including reflected boxes.
				nearest = DepthOrder::Far();
				if (count == 0) {
					HIZ_COUNT_EMPTY_CLIP;
				}
				[loop] for (uint index = 0; index < count; ++index)
				{
					HIZ_COUNT_CLIP_VERTEX;
					nearest = DepthOrder::Nearest(nearest, polygon[polygonBase + index].z);
				}
				if (count != 0 && !DepthOrder::IsBehindWithBias(nearest, depth, guardedBias))
					return false;
			}
		}
		return true;
	}
}

#endif
