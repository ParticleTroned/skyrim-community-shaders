#ifndef CS_CHARACTER_CATEGORY_MASK_HLSLI
#define CS_CHARACTER_CATEGORY_MASK_HLSLI

namespace CharacterCategoryMask
{
	float2 EncodeCategory(uint category)
	{
		category = category <= 3u ? category : 0u;
		return float2(category & 1u, (category >> 1u) & 1u);
	}

	float4 Encode(float inverseVertexAo, uint category, float opacity)
	{
		return float4(
			saturate(inverseVertexAo),
			EncodeCategory(category),
			saturate(opacity));
	}

	uint DecodeCategory(float4 encodedValue)
	{
		const uint2 code = uint2(round(saturate(encodedValue.yz) * 65535.0));
		const bool exactX = code.x == 0u || code.x == 65535u;
		const bool exactY = code.y == 0u || code.y == 65535u;
		return exactX && exactY ?
		           (code.x == 65535u ? 1u : 0u) |
		               (code.y == 65535u ? 2u : 0u) :
		           0u;
	}

	float DecodeInverseVertexAo(float4 encodedValue)
	{
		return encodedValue.x;
	}
}

#endif
