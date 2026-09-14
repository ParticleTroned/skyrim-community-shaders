#ifndef CSX_NR_COLOR_COMMON
#define CSX_NR_COLOR_COMMON

cbuffer NRColorCB : register(b0)
{
	uint2 RegionOffset;
	uint2 RegionSize;
	uint ColorMode;
	uint ColorDomain;
	uint ColorTransform;
	uint TransportBypass;
	float ExposureMultiplier;
	float DetailStrength;
	float AppearanceMix;
	float MaximumDetailStops;
};

float DecodeSRGB(float x)
{
	return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}
float EncodeSRGB(float x)
{
	return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
float3 DecodeSRGB3(float3 x) { return float3(DecodeSRGB(x.r), DecodeSRGB(x.g), DecodeSRGB(x.b)); }
float3 EncodeSRGB3(float3 x) { return float3(EncodeSRGB(x.r), EncodeSRGB(x.g), EncodeSRGB(x.b)); }
float Maximum3(float3 x) { return max(x.r, max(x.g, x.b)); }

bool ForwardColor(float3 source, out float3 prepared)
{
	prepared = source;
	if (!all(isfinite(source)))
		return false;
	if (ColorTransform == 0)
		return true;
	if (any(source < 0.0))
		return false;
	prepared = source * ExposureMultiplier;
	if (!all(isfinite(prepared)))
		return false;
	float maximum = Maximum3(prepared);
	if (maximum > 32.0)
		return false;
	if (ColorTransform == 2) {
		prepared /= 1.0 + maximum;
	}
	prepared = EncodeSRGB3(prepared);
	return all(isfinite(prepared));
}

bool InverseColor(float3 model, out float3 source)
{
	source = model;
	if (!all(isfinite(model)))
		return false;
	if (ColorTransform == 0)
		return true;
	if (any(model < 0.0))
		return false;
	source = DecodeSRGB3(model);
	if (ColorTransform == 2) {
		float denominator = 1.0 - Maximum3(source);
		if (denominator < (1.0 / 64.0))
			return false;
		source /= denominator;
	}
	source /= ExposureMultiplier;
	return all(isfinite(source));
}

float3 ToWorking(float3 value)
{
	return ColorDomain == 2 ? DecodeSRGB3(value) : value;
}
float3 FromWorking(float3 value)
{
	return ColorDomain == 2 ? EncodeSRGB3(value) : value;
}
float Luminance(float3 value) { return dot(value, float3(0.2126, 0.7152, 0.0722)); }
#endif
