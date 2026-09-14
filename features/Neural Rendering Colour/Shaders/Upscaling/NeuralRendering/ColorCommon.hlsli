#ifndef CSX_NR_COLOR_COMMON
#define CSX_NR_COLOR_COMMON

cbuffer NRColorCB : register(b0)
{
	uint2 RegionOffset;
	uint2 RegionSize;
	uint ColorMode;
	uint ColorDomain;
	uint ColorTransform;
	// 1: transport copy, 2: hide edit, 4: use captured exposure, 8: observe capture.
	uint ControlFlags;
	float ExposureMultiplier;
	float DetailStrength;
	float AppearanceMix;
	float MaximumDetailStops;
};
Texture2D<float4> ExposureSnapshot : register(t3);

float DecodeSRGB(float x) { return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4); }
float EncodeSRGB(float x) { return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055; }
float3 DecodeSRGB3(float3 x) { return float3(DecodeSRGB(x.r), DecodeSRGB(x.g), DecodeSRGB(x.b)); }
float3 EncodeSRGB3(float3 x) { return float3(EncodeSRGB(x.r), EncodeSRGB(x.g), EncodeSRGB(x.b)); }
float Maximum3(float3 x) { return max(x.r, max(x.g, x.b)); }

bool EffectiveExposure(out float exposure)
{
	exposure = ExposureMultiplier;
	if ((ControlFlags & 4u) != 0u) {
		float4 captured = ExposureSnapshot.Load(int3(0, 0, 0));
		// A GPU snapshot must contain a valid ratio, not the zero-input unit
		// fallback. Missing/stale capture is explicitly invalid, never "1 known".
		if (!all(isfinite(captured)) || captured.w != 1.0)
			return false;
		exposure *= captured.z;
	}
	return isfinite(exposure) && exposure >= (1.0 / 256.0) && exposure <= 256.0;
}

bool ForwardColor(float3 source, out float3 prepared)
{
	prepared = source;
	if (!all(isfinite(source))) return false;
	if (ColorTransform == 0) return true;
	float exposure;
	if (any(source < 0.0) || !EffectiveExposure(exposure)) return false;
	prepared = source * exposure;
	if (!all(isfinite(prepared))) return false;
	float maximum = Maximum3(prepared);
	if (maximum > 32.0) return false;
	if (ColorTransform == 2) prepared /= 1.0 + maximum;
	prepared = EncodeSRGB3(prepared);
	return all(isfinite(prepared));
}

bool InverseColor(float3 model, out float3 source)
{
	source = model;
	if (!all(isfinite(model))) return false;
	if (ColorTransform == 0) return true;
	float exposure;
	if (any(model < 0.0) || !EffectiveExposure(exposure)) return false;
	source = DecodeSRGB3(model);
	if (ColorTransform == 2) {
		float denominator = 1.0 - Maximum3(source);
		if (denominator < (1.0 / 64.0)) return false;
		source /= denominator;
	}
	source /= exposure;
	return all(isfinite(source));
}
float3 ToWorking(float3 value) { return ColorDomain == 2 ? DecodeSRGB3(value) : value; }
float3 FromWorking(float3 value) { return ColorDomain == 2 ? EncodeSRGB3(value) : value; }
float Luminance(float3 value) { return dot(value, float3(0.2126, 0.7152, 0.0722)); }
#endif
