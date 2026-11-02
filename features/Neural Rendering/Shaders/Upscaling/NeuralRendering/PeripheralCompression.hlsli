#ifndef NR_PERIPHERAL_COMPRESSION_HLSLI
#define NR_PERIPHERAL_COMPRESSION_HLSLI

// Analytic curve adapted from Yuri Grib (BeliyG3), optimizer-fps-dlss5, MIT.
// See SKSE/Plugins/CommunityShaders/Licenses/NeuralRendering/PeripheralCompression.txt.
cbuffer PeripheralMappingConstants : register(b3)
{
	float2 PeripheralCenter, PeripheralRadii;
	float2 PeripheralOrigin, PeripheralWork;
	float PeripheralProtected, PeripheralBoundaryScale, PeripheralEdgeSlope;
	uint PeripheralReserved;
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_FACE_DETAIL_PROTECTION)
	float4 FaceDetailKnots[8];
	uint FaceDetailEnabled;
	uint3 FaceDetailPadding;
#endif
};

#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_FACE_DETAIL_PROTECTION)
float FaceDetailKnot(uint axis, uint index)
{
	uint bounded = min(index, 15u);
	return index == 16u ? 1.0 : FaceDetailKnots[axis * 4u + bounded / 4u][bounded % 4u];
}

float FaceDetailForward(float value, uint axis)
{
	if (FaceDetailEnabled == 0u)
		return value;
	uint index = uint(clamp(value * 16.0, 0.0, 15.0));
	return lerp(FaceDetailKnot(axis, index), FaceDetailKnot(axis, index + 1u), value * 16.0 - index);
}

float FaceDetailInverse(float value, uint axis)
{
	if (FaceDetailEnabled == 0u)
		return value;
	uint low = 0u, high = 16u;
	[unroll] for (uint step = 0u; step < 4u; ++step)
	{
		uint middle = (low + high) / 2u;
		if (FaceDetailKnot(axis, middle) <= value)
			low = middle;
		else
			high = middle;
	}
	return (low + (value - FaceDetailKnot(axis, low)) / (FaceDetailKnot(axis, low + 1u) - FaceDetailKnot(axis, low))) / 16.0;
}
#endif

float PeripheralDistance(float2 delta)
{
	float2 normalized = abs(delta / PeripheralRadii);
	return max(normalized.x, normalized.y);
}

float PeripheralForwardRadius(float radius)
{
	if (radius <= PeripheralProtected)
		return radius;
	if (radius >= 1.0)
		return PeripheralBoundaryScale + (radius - 1.0) * PeripheralEdgeSlope;
	float phase = (radius - PeripheralProtected) / (1.0 - PeripheralProtected);
	return PeripheralProtected + (PeripheralBoundaryScale - PeripheralProtected) * (2.0 * phase / (1.0 + phase));
}

float PeripheralInverseScale(float radius)
{
	if (radius <= PeripheralProtected)
		return 1.0;
	if (radius >= PeripheralBoundaryScale)
		return (1.0 + (radius - PeripheralBoundaryScale) / PeripheralEdgeSlope) / radius;
	float phase = (radius - PeripheralProtected) / (PeripheralBoundaryScale - PeripheralProtected);
	return (PeripheralProtected + (1.0 - PeripheralProtected) * phase / (2.0 - phase)) / radius;
}

float2 PeripheralForward(float2 source)
{
	float2 delta = source - PeripheralCenter;
	float radius = PeripheralDistance(delta);
	float scale = radius > PeripheralProtected ? PeripheralForwardRadius(radius) / radius : 1.0;
	float2 result = (PeripheralCenter + delta * scale - PeripheralOrigin) / PeripheralWork;
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_FACE_DETAIL_PROTECTION)
	result = float2(FaceDetailForward(result.x, 0u), FaceDetailForward(result.y, 1u));
#endif
	return result;
}

float2 PeripheralInverse(float2 target)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_FACE_DETAIL_PROTECTION)
	target = float2(FaceDetailInverse(target.x, 0u), FaceDetailInverse(target.y, 1u));
#endif
	float2 delta = PeripheralOrigin + target * PeripheralWork - PeripheralCenter;
	return PeripheralCenter + delta * PeripheralInverseScale(PeripheralDistance(delta));
}

void PeripheralSourceBounds(float2 lower, float2 upper, out float2 sourceLower, out float2 sourceUpper)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_FACE_DETAIL_PROTECTION)
	lower = float2(FaceDetailInverse(lower.x, 0u), FaceDetailInverse(lower.y, 1u));
	upper = float2(FaceDetailInverse(upper.x, 0u), FaceDetailInverse(upper.y, 1u));
#endif
	float2 low = PeripheralOrigin + lower * PeripheralWork - PeripheralCenter;
	float2 high = PeripheralOrigin + upper * PeripheralWork - PeripheralCenter;
	float2 nearest = float2(low.x <= 0.0 && high.x >= 0.0 ? 0.0 : min(abs(low.x), abs(high.x)),
		low.y <= 0.0 && high.y >= 0.0 ? 0.0 : min(abs(low.y), abs(high.y)));
	float2 farthest = max(abs(low), abs(high));
	float nearScale = PeripheralInverseScale(PeripheralDistance(nearest));
	float farScale = PeripheralInverseScale(PeripheralDistance(farthest));
	sourceLower = PeripheralCenter + min(low * nearScale, low * farScale);
	sourceUpper = PeripheralCenter + max(high * nearScale, high * farScale);
}
#endif
