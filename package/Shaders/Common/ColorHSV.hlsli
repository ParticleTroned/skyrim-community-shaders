#ifndef COLOR_HSV_INCLUDED
#define COLOR_HSV_INCLUDED

namespace Color
{
	static const float HSVSectorCount = 6.0;
	static const float3 HSVHueOffsets = float3(0.0, 2.0 / 3.0, 1.0 / 3.0);

	float3 RGBtoHSV(float3 color)
	{
		color = max(color, 0.0);
		float value = max(color.r, max(color.g, color.b));
		float chroma = value - min(color.r, min(color.g, color.b));
		float hue = 0.0;
		float saturation = 0.0;
		if (chroma > 0.0) {
			saturation = chroma / value;
			float3 delta = (value - color) / chroma;
			delta.rgb -= delta.brg;
			delta.rg += HSVSectorCount * float2(1.0 / 3.0, 2.0 / 3.0);
			hue = color.r >= value ? delta.b : (color.g >= value ? delta.r : delta.g);
			hue = frac(hue / HSVSectorCount);
		}
		return float3(hue, saturation, value);
	}

	float3 HSVtoRGB(float3 color)
	{
		float3 hue = saturate(abs(frac(color.xxx + HSVHueOffsets) * HSVSectorCount - HSVSectorCount * 0.5) - 1.0);
		return lerp(1.0, hue, color.y) * color.z;
	}
}

#endif
