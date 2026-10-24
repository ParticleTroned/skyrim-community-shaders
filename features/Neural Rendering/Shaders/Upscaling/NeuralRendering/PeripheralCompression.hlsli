#ifndef NR_PERIPHERAL_COMPRESSION_HLSLI
#define NR_PERIPHERAL_COMPRESSION_HLSLI

// Analytic mapping adapted from Yuri Grib (BeliyG3), optimizer-fps-dlss5, MIT.
// See SKSE/Plugins/CommunityShaders/Licenses/NeuralRendering/PeripheralCompression.txt.
cbuffer PeripheralMappingConstants : register(b1)
{
	float2 PeripheralCentre, PeripheralWork;
	float2 PeripheralCompression, PeripheralEdgeSlope;
};

float PeripheralForwardAxis(float source, uint axis)
{
	const float w = PeripheralWork[axis], c = PeripheralCentre[axis], a = PeripheralCompression[axis];
	if (w == 1.0)
		return source;
	if (source < 0.0)
		return source * (PeripheralEdgeSlope[axis] / w);
	if (source > 1.0)
		return 1.0 + (source - 1.0) * (PeripheralEdgeSlope[axis] / w);
	const float delta = source * 2.0 - 1.0;
	float radius = abs(delta);
	if (radius > c) {
		const float t = (radius - c) / (1.0 - c);
		radius = c + (w - c) * t / (a + (1.0 - a) * t);
	}
	return 0.5 + (delta < 0.0 ? -radius : radius) / (2.0 * w);
}

float PeripheralInverseAxis(float target, uint axis)
{
	const float w = PeripheralWork[axis], c = PeripheralCentre[axis], a = PeripheralCompression[axis];
	if (w == 1.0)
		return target;
	if (target < 0.0)
		return target * (w / PeripheralEdgeSlope[axis]);
	if (target > 1.0)
		return 1.0 + (target - 1.0) * (w / PeripheralEdgeSlope[axis]);
	const float delta = (target * 2.0 - 1.0) * w;
	float radius = abs(delta);
	if (radius > c) {
		const float y = (radius - c) / (w - c);
		const float t = a * y / (1.0 - (1.0 - a) * y);
		radius = c + (1.0 - c) * t;
	}
	return 0.5 + (delta < 0.0 ? -radius : radius) * 0.5;
}

float2 PeripheralForward(float2 source)
{
	return float2(PeripheralForwardAxis(source.x, 0u), PeripheralForwardAxis(source.y, 1u));
}

float2 PeripheralInverse(float2 target)
{
	return float2(PeripheralInverseAxis(target.x, 0u), PeripheralInverseAxis(target.y, 1u));
}
#endif
