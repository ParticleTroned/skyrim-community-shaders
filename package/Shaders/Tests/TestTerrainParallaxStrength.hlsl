#include "/Test/STF/ShaderTestFramework.hlsli"

#define LANDSCAPE
#define TERRAIN_VARIATION
#define TERRAIN_VARIATION_HLSLI
struct StochasticOffsets
{
	float unused;
};
struct PS_INPUT
{
	float4 LandBlendWeights1;
	float4 LandBlendWeights2;
};
static float fixtureHeight = 0.25;
static float fixtureSlope = 0.0;
// Analytic samples exercise production terrain blending without texture bindings.
float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mip, StochasticOffsets offsets)
{
	return fixtureHeight + fixtureSlope * uv.x;
}
namespace SharedData
{
	struct Settings
	{
		float ParallaxStrength;
		bool EnableHeightBlending;
		bool EnableTerrainParallax;
	};
	static Settings extendedMaterialSettings;
	static const float MipBias = 0.0;
}
namespace Permutation
{
	static const uint ExtraFeatureDescriptor = 0;
	namespace ExtraFeatureFlags
	{
		static const uint THLand0HasDisplacement = 1;
		static const uint THLand1HasDisplacement = 2;
		static const uint THLand2HasDisplacement = 4;
		static const uint THLand3HasDisplacement = 8;
		static const uint THLand4HasDisplacement = 16;
		static const uint THLand5HasDisplacement = 32;
		static const uint THLandHasDisplacement = 63;
	}
}
Texture2D TexColorSampler, TexLandColor2Sampler, TexLandColor3Sampler, TexLandColor4Sampler, TexLandColor5Sampler, TexLandColor6Sampler;
Texture2D TexLandTHDisp0Sampler, TexLandTHDisp1Sampler, TexLandTHDisp2Sampler, TexLandTHDisp3Sampler, TexLandTHDisp4Sampler, TexLandTHDisp5Sampler;
SamplerState SampTerrainParallaxSampler;
#include "/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli"

void InitializeTerrainFixture(out PS_INPUT input, out DisplacementParams params[6], out float mips[6])
{
	input.LandBlendWeights1 = float4(0.6, 0.4, 0, 0);
	input.LandBlendWeights2 = 0;
	[unroll] for (uint i = 0; i < 6; ++i)
	{
		params[i].DisplacementScale = 1.0;
		params[i].DisplacementOffset = 0.0;
		params[i].HeightScale = i == 0 ? 1.0 : 0.5;
		params[i].FlattenAmount = 0.0;
		mips[i] = 0.0;
	}
	SharedData::extendedMaterialSettings.EnableHeightBlending = true;
	SharedData::extendedMaterialSettings.EnableTerrainParallax = true;
	fixtureHeight = 0.25;
	fixtureSlope = 0.0;
}

float2 SampleTerrainFixture(float strength, out float weights[6])
{
	PS_INPUT input;
	DisplacementParams params[6];
	float mips[6];
	InitializeTerrainFixture(input, params, mips);
	SharedData::extendedMaterialSettings.ParallaxStrength = strength;
	StochasticOffsets offsets = { 0.0 };
	float pixelOffset;
	return ExtendedMaterials::GetParallaxCoords(input, 100.0, float2(0.5, 0.5), mips, 1024.0,
		float3(0.6, 0.0, 0.8), float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), 0.0, params, offsets, pixelOffset, weights);
}

/// @tags parallax, terrain
[numthreads(1, 1, 1)] void TestTerrainDepthAndBlending() {
	float neutralWeights[6], halfWeights[6], doubleWeights[6];
	float2 neutral = SampleTerrainFixture(1.0, neutralWeights) - 0.5;
	float2 halfDepth = SampleTerrainFixture(0.5, halfWeights) - 0.5;
	float2 doubleDepth = SampleTerrainFixture(2.0, doubleWeights) - 0.5;
	ASSERT(IsTrue, abs(neutral.x) > 1e-3);
	ASSERT(IsTrue, all(abs(halfDepth - neutral * 0.5) < 1e-5));
	ASSERT(IsTrue, all(abs(doubleDepth - neutral * 2.0) < 1e-5));
	[unroll] for (uint i = 0; i < 6; ++i)
	{
		ASSERT(IsTrue, abs(neutralWeights[i] - halfWeights[i]) < 1e-5);
		ASSERT(IsTrue, abs(neutralWeights[i] - doubleWeights[i]) < 1e-5);
	}
	PS_INPUT input;
	DisplacementParams params[6];
	float mips[6], offWeights[6];
	InitializeTerrainFixture(input, params, mips);
	SharedData::extendedMaterialSettings.ParallaxStrength = 0.0;
	StochasticOffsets offsets = { 0.0 };
	float height = ExtendedMaterials::GetTerrainHeight(0.0, input, float2(0.5, 0.5), mips, params, 1.0,
		input.LandBlendWeights1, input.LandBlendWeights2.xy, offsets, offWeights);
	ASSERT(IsTrue, height < 0.0 && abs(offWeights[0] - input.LandBlendWeights1.x) > 1e-3);
	ASSERT(IsTrue, abs(offWeights[0] + offWeights[1] - 1.0) < 1e-5);
}

	/// @tags parallax, terrain, shadows
	[numthreads(1, 1, 1)] void TestTerrainZeroShadows()
{
	PS_INPUT input;
	DisplacementParams params[6];
	float mips[6];
	InitializeTerrainFixture(input, params, mips);
	SharedData::extendedMaterialSettings.ParallaxStrength = 0.0;
	StochasticOffsets offsets = { 0.0 };
	float baseHeight;
	bool hasShadow = ExtendedMaterials::ComputeTerrainParallaxShadowBaseHeight(input, float2(0.5, 0.5), mips, 1.0, 0.0, params, offsets, baseHeight);
	ASSERT(IsTrue, !hasShadow && baseHeight == 0.0);
	ASSERT(AreEqual, ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, float2(0.5, 0.5), mips, float3(0.6, 0.0, 0.8), -1.0, 1.0, 0.0, params, offsets), 1.0);
	ASSERT(AreEqual, ExtendedMaterials::EvaluateTerrainDirectionalParallaxShadowMultiplier(input, float2(0.5, 0.5), mips, float3(0.6, 0.0, 0.8), 1.0, 0.0, params, offsets, -1.0), 1.0);
}
