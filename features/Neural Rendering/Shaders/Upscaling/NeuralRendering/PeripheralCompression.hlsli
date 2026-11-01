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
};

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
	return (PeripheralCenter + delta * scale - PeripheralOrigin) / PeripheralWork;
}

float2 PeripheralInverse(float2 target)
{
	float2 delta = PeripheralOrigin + target * PeripheralWork - PeripheralCenter;
	return PeripheralCenter + delta * PeripheralInverseScale(PeripheralDistance(delta));
}

void PeripheralSourceBounds(float2 lower, float2 upper, out float2 sourceLower, out float2 sourceUpper)
{
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
