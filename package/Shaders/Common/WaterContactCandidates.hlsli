#ifndef WATER_CONTACT_CANDIDATES_HLSLI
#define WATER_CONTACT_CANDIDATES_HLSLI

namespace WaterContactCandidates
{
	static const uint MaxCandidates = 8;
	static const uint EmptyIndex = 255;

	uint2 Empty()
	{
		return uint2(0xffffffff, 0xffffffff);
	}

	void Store(inout uint2 packed, uint slot, uint index)
	{
		uint word = slot / 4;
		uint shift = (slot % 4) * 8;
		packed[word] = (packed[word] & ~(255u << shift)) | ((index & 255u) << shift);
	}

	uint Load(uint2 packed, uint slot)
	{
		return (packed[slot / 4] >> ((slot % 4) * 8)) & 255u;
	}

	bool Contains(uint2 packed, uint localIndex, uint budget)
	{
		// The unmodified culler writes zero padding. A missing candidate dispatch
		// therefore falls back to the native loop rather than dropping shadows.
		if (budget == 0 || all(packed == 0))
			return true;
		[unroll] for (uint slot = 0; slot < MaxCandidates; ++slot) {
			if (slot < min(budget, MaxCandidates) && Load(packed, slot) == localIndex)
				return true;
		}
		return false;
	}
}
#endif
