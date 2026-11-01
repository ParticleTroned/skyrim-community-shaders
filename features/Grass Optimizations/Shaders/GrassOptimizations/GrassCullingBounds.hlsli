#ifndef GRASS_CULLING_BOUNDS_HLSLI
#define GRASS_CULLING_BOUNDS_HLSLI

float GrassBasisNorm(float3x3 basis)
{
	float3 diagonal = float3(dot(basis[0], basis[0]), dot(basis[1], basis[1]), dot(basis[2], basis[2]));
	float3 crossTerms = abs(float3(dot(basis[0], basis[1]), dot(basis[0], basis[2]), dot(basis[1], basis[2])));
	float3 sums = diagonal + float3(crossTerms.x + crossTerms.y, crossTerms.x + crossTerms.z, crossTerms.y + crossTerms.z);
	return sqrt(min(diagonal.x + diagonal.y + diagonal.z, max(sums.x, max(sums.y, sums.z))) * 1.00001);
}

// Fractional plane extrema bound the entire sphere with one center projection.
bool GrassProjectSphere(row_major float4x4 projection, float3 center, float radius, out float2 lo, out float2 hi, out float nearDepth)
{
	lo = 0;
	hi = 0;
	nearDepth = 0;
	float4 clip = mul(projection, float4(center, 1));
	float radiusSquared = radius * radius;
	float wNorm = dot(projection[3].xyz, projection[3].xyz);
	float denominator = clip.w * clip.w - radiusSquared * wNorm;
	if (!all(isfinite(clip)) || !isfinite(radiusSquared) || radius <= 0 ||
		clip.w <= 1e-5 || denominator <= 1e-4 * clip.w * clip.w ||
		clip.z <= radius * length(projection[2].xyz))
		return false;
	float3 low = 0, high = 0;
	[unroll] for (uint axis = 0; axis < 3; ++axis)
	{
		float3 row = projection[axis].xyz;
		float product = clip[axis] * clip.w - radiusSquared * dot(row, projection[3].xyz);
		float3 tangent = clip.w * row - clip[axis] * projection[3].xyz;
		float3 crossRow = cross(row, projection[3].xyz);
		float positive = dot(tangent, tangent), negative = radiusSquared * dot(crossRow, crossRow);
		float discriminant = radiusSquared * (positive - negative + 1e-5 * (positive + negative));
		if (!isfinite(discriminant) || discriminant < 0)
			return false;
		float root = sqrt(discriminant);
		low[axis] = (product - root) / denominator;
		high[axis] = (product + root) / denominator;
		float slack = 1e-5 * (1 + abs(low[axis]) + abs(high[axis])) *
		              (clip.w * clip.w + radiusSquared * wNorm) / denominator;
		low[axis] -= slack;
		high[axis] += slack;
	}
	lo = low.xy;
	hi = high.xy;
	nearDepth = low.z;
	return all(isfinite(low)) && all(isfinite(high)) && nearDepth > 0;
}

#endif
