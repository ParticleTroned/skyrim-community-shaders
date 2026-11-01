#include "/Shaders/Common/WaterRefraction.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

[numthreads(1, 1, 1)]
void TestWaterRefractionRejectsEmptyFootprint()
{
	float2 lo, hi;
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(0, 100), float2(100, 100), 0, lo, hi));
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(100, 100), float2(100, 0), 0, lo, hi));
}

[numthreads(1, 1, 1)]
void TestWaterRefractionFiniteFallback()
{
	float2 value = WaterRefraction::ClampUV(float2(asfloat(0x7fc00000), 0.5), float2(0.25, 0.75), float2(0.1, 0.1), float2(0.9, 0.9));
	ASSERT(IsTrue, all(value == float2(0.25, 0.75)));
}
