#ifndef CS_CHARACTER_CATEGORY_MASK_HLSLI
#define CS_CHARACTER_CATEGORY_MASK_HLSLI

namespace CharacterCategoryMask
{
	static const uint Excluded = 4u;

	float EncodeCategory(uint category)
	{
		// Explicitly excluded opaque NPC material is not background: prevent
		// spatial coverage/feathering from painting an adjacent selected actor on it.
		if (category == Excluded)
			return 1.0 / 255.0;
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
		if (code == 1u)
			return Excluded;
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
