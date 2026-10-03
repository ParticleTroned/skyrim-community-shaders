// HLSL unit tests for final volumetric-lighting composition controls.
#include "/Shaders/Common/VolumetricLighting.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags volumetric-lighting, opacity
[numthreads(1, 1, 1)] void TestVolumetricLightingOpacity() {
	float input = 0.5;
	float invalid = asfloat(0x7fc00000);

	ASSERT(AreEqual, VolumetricLighting::ApplyOpacity(input, 0.0), 0.0);
	ASSERT(AreEqual, VolumetricLighting::ApplyOpacity(input, 1.0), input);
	ASSERT(IsTrue, VolumetricLighting::ApplyOpacity(input, 0.5) < input);
	ASSERT(IsTrue, VolumetricLighting::ApplyOpacity(input, 2.0) > input);
	ASSERT(AreEqual, VolumetricLighting::ApplyOpacity(input, invalid), input);
	ASSERT(AreEqual, VolumetricLighting::ApplyOpacity(invalid, 2.0), 0.0);
}

	/// @tags volumetric-lighting, color
	[numthreads(1, 1, 1)] void TestVolumetricLightingColor()
{
	float3 authored = float3(2.0, 0.6, 0.2);
	float3 neutral = VolumetricLighting::ApplyColor(authored, 1.0, float4(1.0, 1.0, 1.0, 0.0));
	float3 grayscale = VolumetricLighting::ApplyColor(authored, 0.0, float4(1.0, 1.0, 1.0, 0.0));
	float3 custom = VolumetricLighting::ApplyColor(authored, 1.0, float4(0.2, 0.4, 0.8, 1.0));
	float luminance = Color::RGBToLuminance(authored);

	ASSERT(IsTrue, all(abs(neutral - authored) < 0.0001));
	ASSERT(IsTrue, all(abs(grayscale - luminance.xxx) < 0.0001));
	ASSERT(IsTrue, all(abs(custom - float3(0.2, 0.4, 0.8)) < 0.0001));
}

/// @tags volumetric-lighting, color, finite
[numthreads(1, 1, 1)] void TestVolumetricLightingInvalidColorInputs() {
	float invalid = asfloat(0x7fc00000);
	float3 authored = float3(0.8, 0.4, 0.2);
	float3 result = VolumetricLighting::ApplyColor(
		authored,
		invalid,
		float4(invalid, 0.4, 0.8, invalid));

	ASSERT(IsTrue, all(abs(result - authored) < 0.0001));
}
