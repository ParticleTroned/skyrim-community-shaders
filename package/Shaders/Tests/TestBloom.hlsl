// HLSL unit tests for the Bloom contribution applied during display mapping.

// Stubs for dependencies from ISHDR.hlsl (not exercised by these tests).
float3 GetTonemapFactorHejlBurgessDawson(float3 x) { return x; }
static const float4 Param = { 0, 0, 0, 0 };

#include "/Shaders/Common/DisplayMapping.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags bloom, adaptive-balance
/// A disabled enhancement must retain the exact native Bloom expression.
[numthreads(1, 1, 1)] void TestBloomDisabledPreservesVanilla()
{
	float3 mappedColor = float3(0.8, 0.4, 0.1);
	float3 vanillaBloom = float3(0.2, 0.3, 0.4);
	float3 enhancedBloom = float3(3.0, 2.0, 1.0);
	float nativeMaskLimit = 0.5;

	float3 expected = mappedColor + saturate(nativeMaskLimit - mappedColor) * vanillaBloom;
	float3 actual = DisplayMapping::ApplyBloom(mappedColor, vanillaBloom, enhancedBloom, nativeMaskLimit, 0.0);

	ASSERT(IsTrue, all(actual == expected));
}

/// @tags bloom, adaptive-balance, vanilla
/// Enhancement obeys Skyrim's native mask, including when it is fully closed.
[numthreads(1, 1, 1)] void TestBloomEnhancementUsesNativeMask()
{
	float3 mappedColor = 0.8.xxx;
	float3 bloom = 0.5.xxx;
	float nativeMaskLimit = 0.3;

	float3 vanillaOnly = DisplayMapping::ApplyBloom(mappedColor, bloom, bloom, nativeMaskLimit, 0.0);
	float3 enhanced = DisplayMapping::ApplyBloom(mappedColor, bloom, bloom, nativeMaskLimit, 1.0);

	ASSERT(IsTrue, all(vanillaOnly == mappedColor));
	ASSERT(IsTrue, all(enhanced == vanillaOnly));
}

/// @tags bloom, adaptive-balance
/// Profile transitions interpolate complete native/enhanced contributions.
[numthreads(1, 1, 1)] void TestBloomProfileBlendIsLinear()
{
	float3 mappedColor = float3(0.7, 0.5, 0.2);
	float3 vanillaBloom = float3(0.1, 0.2, 0.3);
	float3 enhancedBloom = float3(0.8, 0.6, 0.4);
	float nativeMaskLimit = 0.4;

	float3 vanillaResult = DisplayMapping::ApplyBloom(mappedColor, vanillaBloom, enhancedBloom, nativeMaskLimit, 0.0);
	float3 enhancedResult = DisplayMapping::ApplyBloom(mappedColor, vanillaBloom, enhancedBloom, nativeMaskLimit, 1.0);
	float3 blendedResult = DisplayMapping::ApplyBloom(mappedColor, vanillaBloom, enhancedBloom, nativeMaskLimit, 0.25);
	float3 expected = lerp(vanillaResult, enhancedResult, 0.25);

	ASSERT(IsTrue, all(abs(blendedResult - expected) < 0.000001));
}

/// @tags bloom, adaptive-balance
/// The enhancement path cannot add light without an input Bloom signal.
[numthreads(1, 1, 1)] void TestBloomZeroSignalRemainsZero()
{
	float3 mappedColor = float3(0.9, 0.6, 0.3);
	float3 actual = DisplayMapping::ApplyBloom(mappedColor, 0.0.xxx, 0.0.xxx, 0.2, 1.0);

	ASSERT(IsTrue, all(actual == mappedColor));
}

/// @tags bloom, adaptive-balance
/// Enhancement must preserve the native mask limit supplied by the weather.
[numthreads(1, 1, 1)] void TestBloomPreservesHigherNativeMaskLimit()
{
	float3 mappedColor = 0.25.xxx;
	float3 bloom = float3(0.2, 0.4, 0.6);
	float nativeMaskLimit = 1.5;

	float3 vanillaResult = DisplayMapping::ApplyBloom(mappedColor, bloom, bloom, nativeMaskLimit, 0.0);
	float3 enhancedResult = DisplayMapping::ApplyBloom(mappedColor, bloom, bloom, nativeMaskLimit, 1.0);

	ASSERT(IsTrue, all(enhancedResult == vanillaResult));
}
