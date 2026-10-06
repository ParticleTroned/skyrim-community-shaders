#ifndef CS_CHARACTER_CATEGORY_MASK_HLSLI
#define CS_CHARACTER_CATEGORY_MASK_HLSLI

namespace CharacterCategoryMask
{
	static const uint CategoryCount = 5u;
	static const uint Excluded = 255u;

	float EncodeCategory(uint category, uint focusFade)
	{
		const uint code = category == Excluded ? 1u :
		                                         (category <= 3u ? category * 85u : (category == 4u ? 42u : (category == 5u ? 127u : 0u)));
		// The high byte owns category; the low byte carries whole-actor fade.
		// Zero fade preserves the original UNORM category values exactly.
		return float((code << 8u) | ((code - min(focusFade, 255u)) & 255u)) / 65535.0;
	}

	float4 Encode(float inverseVertexAo, uint category, float opacity, uint focusFade)
	{
		return float4(saturate(inverseVertexAo), EncodeCategory(category, focusFade), 0.0, saturate(opacity));
	}

	float4 Encode(float inverseVertexAo, uint category, float opacity)
	{
		return Encode(inverseVertexAo, category, opacity, 0u);
	}

	uint DecodeCategory(float2 encodedValue)
	{
		const uint code = uint(round(saturate(encodedValue.y) * 65535.0)) >> 8u;
		if (code == 1u)
			return Excluded;
		if (code == 85u)
			return 1u;
		if (code == 170u)
			return 2u;
		if (code == 42u)
			return 4u;
		if (code == 127u)
			return 5u;
		return code == 255u ? 3u : 0u;
	}

	float DecodeFocusWeight(float2 encodedValue)
	{
		const uint code = uint(round(saturate(encodedValue.y) * 65535.0));
		return 1.0 - float(((code >> 8u) - (code & 255u)) & 255u) / 255.0;
	}

	float DecodeInverseVertexAo(float2 encodedValue)
	{
		return encodedValue.x;
	}
}

#endif
