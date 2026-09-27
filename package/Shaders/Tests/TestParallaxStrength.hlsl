#include "/Test/STF/ShaderTestFramework.hlsli"

// Analytic heights isolate ray travel from texture filtering and stochastic sampling.
#define TERRAIN_VARIATION
#define TERRAIN_VARIATION_HLSLI
struct StochasticOffsets
{
	float unused;
};
static float fixtureHeight = 0.25;
static float fixtureSlope = 0.0;
float StochasticHeightChannel(Texture2D<float4> tex, SamplerState samp, float2 uv, float mip, uint channel, StochasticOffsets offsets)
{
	return fixtureHeight + fixtureSlope * uv.x;
}
namespace SharedData
{
	struct Settings
	{
		float ParallaxStrength;
	};
	static Settings extendedMaterialSettings;
	static const float MipBias = 0.0;
}
#include "/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli"

Texture2D<float4> fixtureTexture;
SamplerState fixtureSampler;

float2 SampleParallaxFixture(float strength, float materialScale, float3 view)
{
	SharedData::extendedMaterialSettings.ParallaxStrength = strength;
	DisplacementParams params = { 1.0, 0.0, materialScale, 0.0 };
	StochasticOffsets offsets = { 0.0 };
	float pixelOffset;
	return ExtendedMaterials::GetParallaxCoords(100.0, float2(0.5, 0.5), 0.0, view,
		float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), 0.0,
		fixtureTexture, fixtureSampler, 0, params, true, offsets, pixelOffset);
}

/// @tags parallax, materials
[numthreads(1, 1, 1)] void TestParallaxDepthMultiplier() {
	fixtureHeight = 0.25;
	fixtureSlope = 0.0;
	float3 view = float3(0.6, 0.0, 0.8);
	float2 neutral = SampleParallaxFixture(1.0, 1.0, view) - 0.5;
	float2 halfDepth = SampleParallaxFixture(0.5, 1.0, view) - 0.5;
	float2 doubleDepth = SampleParallaxFixture(2.0, 1.0, view) - 0.5;
	ASSERT(IsTrue, abs(neutral.x + 0.025 * 0.6 / 0.86) < 1e-5);
	ASSERT(IsTrue, all(abs(halfDepth - neutral * 0.5) < 1e-5));
	ASSERT(IsTrue, all(abs(doubleDepth - neutral * 2.0) < 1e-5));
	ASSERT(IsTrue, all(abs(SampleParallaxFixture(2.0, 0.5, view) - 0.5 - neutral) < 1e-5));
}

	/// @tags parallax, materials, vr
	[numthreads(1, 1, 1)] void TestParallaxZeroAndEyeSymmetry()
{
	fixtureHeight = 0.25;
	fixtureSlope = 0.0;
	ASSERT(IsTrue, all(SampleParallaxFixture(0.0, 2.0, float3(0.6, 0.0, 0.8)) == 0.5));
	float2 left = SampleParallaxFixture(2.0, 1.0, float3(-0.6, 0.0, 0.8)) - 0.5;
	float2 right = SampleParallaxFixture(2.0, 1.0, float3(0.6, 0.0, 0.8)) - 0.5;
	ASSERT(IsTrue, all(abs(left + right) < 1e-5));
}

float SampleParallaxShadowFixture(float strength)
{
	SharedData::extendedMaterialSettings.ParallaxStrength = strength;
	DisplacementParams params = { 1.0, 0.0, 1.0, 0.0 };
	StochasticOffsets offsets = { 0.0 };
	return ExtendedMaterials::GetParallaxSoftShadowMultiplier(float2(0.5, 0.5), 0.0,
		float3(0.6, 0.0, 0.8), 0.25, fixtureTexture, fixtureSampler, 0, 1.0, 0.0, params, true, offsets);
}

/// @tags parallax, materials, shadows
[numthreads(1, 1, 1)] void TestParallaxShadowStrength() {
	fixtureSlope = 0.25;
	fixtureHeight = 0.125;
	ASSERT(AreEqual, SampleParallaxShadowFixture(0.0), 1.0);
	float neutral = 1.0 - SampleParallaxShadowFixture(1.0);
	ASSERT(IsTrue, neutral > 0.0);
	ASSERT(IsTrue, abs(1.0 - SampleParallaxShadowFixture(0.5) - neutral * 0.5) < 1e-5);
	ASSERT(IsTrue, abs(1.0 - SampleParallaxShadowFixture(2.0) - neutral * 2.0) < 1e-5);
}
