#include "/Shaders/Common/WaterParallaxMath.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

[numthreads(1, 1, 1)] void TestWaterParallaxNoHitAndZeroDenominator()
{
	ASSERT(AreEqual, WaterParallaxMath::Intersect(0.5, 1.0, 1.0, 0.1), 0.5);
	ASSERT(AreEqual, WaterParallaxMath::Intersect(0.5, 0.5, 0.4, 0.1), 0.5);
}

[numthreads(1, 1, 1)] void TestWaterParallaxBudgetNeverExceeds64()
{
	ASSERT(AreEqual, WaterParallaxMath::Budget(1.0), 64u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(2.0), 64u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(0.0), 8u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(-1.0), 8u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(0.5), 36u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(0x7fc00000)), 64u);
}

[numthreads(1, 1, 1)]
void TestWaterParallaxNonFiniteBudgetUsesFullCap()
{
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(0x7f800000u)), 64u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(((1u << 31) | 0x7f800000u))), 64u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(((1u << 31) | 0x7fc00001u))), 64u);
}
