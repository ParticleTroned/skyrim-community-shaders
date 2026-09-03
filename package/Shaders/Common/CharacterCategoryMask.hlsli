#ifndef CS_CHARACTER_CATEGORY_MASK_HLSLI
#define CS_CHARACTER_CATEGORY_MASK_HLSLI

namespace CharacterCategoryMask
{
	float EncodeCategory(uint category)
	{
		category = category <= 3u ? category : 0u;
		return float(category) / 3.0;
	}

	float4 Encode(float inverseVertexAo, uint category, float opacity)
	{
		return float4(
			saturate(inverseVertexAo),
			EncodeCategory(category),
			0.0,
			saturate(opacity));
	}

	uint DecodeCategory(float2 encodedValue)
	{
		const uint code = uint(round(saturate(encodedValue.y) * 255.0));
		if (code == 85u)
			return 1u;
		if (code == 170u)
			return 2u;
		return code == 255u ? 3u : 0u;
	}

	float DecodeInverseVertexAo(float2 encodedValue)
	{
		return encodedValue.x;
	}
}

#endif
