#ifndef FOVEATED_MASK_VISUALIZATION_HLSLI
#define FOVEATED_MASK_VISUALIZATION_HLSLI

uint FoveatedPreviewGlyphStrokes(uint character)
{
	switch (character) {
	case 49:
		return 0x6008;  // 1
	case 50:
		return 0x5b;  // 2
	case 51:
		return 0x4f;  // 3
	case 52:
		return 0x66;  // 4
	case 53:
		return 0x6d;  // 5
	case 54:
		return 0x7d;  // 6
	case 55:
		return 0x07;  // 7
	case 56:
		return 0x7f;  // 8
	case 57:
		return 0x6f;  // 9
	case 70:
		return 0x71;  // F
	case 86:
		return 0x180;  // V
	case 65:
		return 0xe00;  // A
	case 82:
		return 0x1073;  // R
	case 69:
		return 0x79;  // E
	case 83:
		return 0x6d;  // S
	default:
		return 0;
	}
}

float FoveatedPreviewStrokeDistance(float2 position, float4 stroke)
{
	const float2 direction = stroke.zw - stroke.xy;
	const float2 relative = position - stroke.xy;
	return length(relative - direction * saturate(dot(relative, direction) / dot(direction, direction)));
}

float FoveatedPreviewGlyphDistance(uint character, float2 position)
{
	if (character == 48 || character == 79) {
		const float2 corner = abs(position - float2(1.5, 2.5)) - float2(0.25, 1.25);
		return abs(length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - 0.85);
	}
	if (character == 68) {
		const float2 arc = (position - float2(0.4, 2.5)) / float2(2.2, 2.1);
		return min(max(abs(length(arc) - 1.0) * 2.1, 0.4 - position.x),
			FoveatedPreviewStrokeDistance(position, float4(0.4, 0.4, 0.4, 4.6)));
	}
	if (character == 46)
		return length(position - float2(1.5, 4.5));
	if (character == 37) {
		const float rings = min(abs(length(position - float2(0.65, 1.0)) - 0.4),
			abs(length(position - float2(2.35, 4.0)) - 0.4));
		return min(rings, FoveatedPreviewStrokeDistance(position, float4(0.4, 4.6, 2.6, 0.4)));
	}

	static const float4 strokes[15] = {
		float4(0.4, 0.4, 2.6, 0.4), float4(2.6, 0.4, 2.6, 2.5),
		float4(2.6, 2.5, 2.6, 4.6), float4(0.4, 4.6, 2.6, 4.6),
		float4(0.4, 2.5, 0.4, 4.6), float4(0.4, 0.4, 0.4, 2.5),
		float4(0.4, 2.5, 2.6, 2.5), float4(0.4, 0.4, 1.5, 4.6),
		float4(2.6, 0.4, 1.5, 4.6), float4(0.4, 4.6, 1.5, 0.4),
		float4(2.6, 4.6, 1.5, 0.4), float4(0.95, 2.5, 2.05, 2.5),
		float4(0.4, 2.5, 2.6, 4.6), float4(1.5, 0.4, 1.5, 4.6),
		float4(0.4, 1.5, 1.5, 0.4)
	};
	const uint mask = FoveatedPreviewGlyphStrokes(character);
	float distance = 10.0;
	[unroll] for (uint i = 0; i < 15; ++i)
	{
		if ((mask & (1u << i)) != 0)
			distance = min(distance, FoveatedPreviewStrokeDistance(position, strokes[i]));
	}
	return distance;
}

float FoveatedPreviewGlyphInk(uint character, float2 position, float pixelsPerUnit)
{
	// Analytic strokes keep a one-output-pixel antialiasing edge at any HMD resolution.
	const float edge = (FoveatedPreviewGlyphDistance(character, position) - 0.2) * pixelsPerUnit;
	return 1.0 - smoothstep(-0.5, 0.5, edge);
}

// The readout is restricted to covered pixels by the caller, so it cannot hide a yellow gap.
float3 FoveatedPreviewAreaLabel(float3 color, float2 pixel, float2 dimensions, float2 center, float savedPercent)
{
	const float cellSize = max(1.0, min(dimensions.x, dimensions.y) / 440.0);
	const float2 origin = floor(center * dimensions - float2(28.0, 9.0) * cellSize);
	const float2 position = (pixel - origin) / cellSize;
	if (any(position < -2.0) || position.x >= 58.0 || position.y >= 21.0)
		return color;

	float ink = 0.0;
	if (all(position >= 0.0) && position.x < 56.0 && position.y < 5.0) {
		static const uint title[14] = { 70, 79, 86, 32, 65, 82, 69, 65, 32, 83, 65, 86, 69, 68 };
		uint column = (uint)position.x / 4;
		ink = FoveatedPreviewGlyphInk(title[column], float2(position.x - column * 4.0, position.y), cellSize);
	}
	const float2 numberPos = (position - float2(4.0, 8.0)) / 2.0;
	if (all(numberPos >= 0.0) && numberPos.x < 24.0 && numberPos.y < 5.0) {
		const uint tenths = (uint)round(clamp(savedPercent, 0.0, 100.0) * 10.0);
		const uint digits[6] = { tenths >= 1000 ? 49 : 32, tenths >= 100 ? 48 + (tenths / 100) % 10 : 32,
			48 + (tenths / 10) % 10, 46, 48 + tenths % 10, 37 };
		const uint column = (uint)numberPos.x / 4;
		ink = FoveatedPreviewGlyphInk(digits[column], float2(numberPos.x - column * 4.0, numberPos.y), cellSize * 2.0);
	}
	return lerp(color * 0.15, float3(1.0, 1.0, 1.0), ink);
}

#endif
