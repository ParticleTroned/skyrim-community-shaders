#include "/Shaders/Common/WaterContactCandidates.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

[numthreads(1, 1, 1)] void TestWaterCandidateFailureFallsBack()
{
	ASSERT(IsTrue, WaterContactCandidates::Contains(uint2(0, 0), 7, 8));
	ASSERT(IsTrue, WaterContactCandidates::Contains(WaterContactCandidates::Empty(), 7, 0));
	ASSERT(IsFalse, WaterContactCandidates::Contains(WaterContactCandidates::Empty(), 7, 8));
}

[numthreads(1, 1, 1)] void TestWaterCandidateSlotsAreIndependent()
{
	uint2 packed = WaterContactCandidates::Empty();
	[unroll] for (uint slot = 0; slot < 8; ++slot)
		WaterContactCandidates::Store(packed, slot, slot * 17);
	[unroll] for (uint slot = 0; slot < 8; ++slot) {
		ASSERT(AreEqual, WaterContactCandidates::Load(packed, slot), slot * 17);
		ASSERT(IsTrue, WaterContactCandidates::Contains(packed, slot * 17, 8));
	}
	ASSERT(IsFalse, WaterContactCandidates::Contains(packed, 119, 7));
	ASSERT(IsFalse, WaterContactCandidates::Contains(packed, 127, 8));
}

[numthreads(1, 1, 1)]
void TestWaterCandidateEmptySentinelIsUnsigned()
{
	uint2 packed = WaterContactCandidates::Empty();
	ASSERT(IsTrue, all(packed == uint2(~0u, ~0u)));
	[unroll] for (uint slot = 0; slot < WaterContactCandidates::MaxCandidates; ++slot)
		ASSERT(AreEqual, WaterContactCandidates::Load(packed, slot), WaterContactCandidates::EmptyIndex);
}
