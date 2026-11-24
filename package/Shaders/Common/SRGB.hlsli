#ifndef CSX_SRGB
#define CSX_SRGB

namespace SRGB
{
	/** Decodes the piecewise sRGB transfer function used by sRGB texture views. */
	float Decode(float x)
	{
		return x <= 0.04045 ? x / 12.92 : pow(max((x + 0.055) / 1.055, 0.0), 2.4);
	}

	float3 Decode(float3 x)
	{
		return float3(Decode(x.r), Decode(x.g), Decode(x.b));
	}

	/** Encodes linear color with the piecewise sRGB transfer function. */
	float Encode(float x)
	{
		return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(max(x, 0.0), 1.0 / 2.4) - 0.055;
	}

	float3 Encode(float3 x)
	{
		return float3(Encode(x.r), Encode(x.g), Encode(x.b));
	}
}

#endif
