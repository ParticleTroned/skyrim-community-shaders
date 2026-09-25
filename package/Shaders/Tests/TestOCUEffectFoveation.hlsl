#include "/Shaders/Common/OCUEffectFoveation.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags foveation, ocu, vr
[numthreads(1, 1, 1)] void TestOCUEffectSeparateEyeCenters() {
	float4 centers = float4(.25, .5, .75, .5);
	float4 policy = float4(.25, .5, 1, 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.25, .5), 0, centers, policy) == 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.75, .5), 1, centers, policy) == 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.25, .5), 1, centers, policy) == 1);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.75, .5), 0, centers, policy) == 1);
}

	/// @tags foveation, ocu, vr
	[numthreads(1, 1, 1)] void TestOCUEffectInnerBoundaryAndEqualRadii()
{
	float4 centers = .5;
	float4 policy = float4(.5, .75, 1, 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.75, .5), 0, centers, policy) == 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.875, .5), 0, centers, policy) == 1);
	policy.y = policy.x;
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.75, .5), 0, centers, policy) == 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(.875, .5), 0, centers, policy) == 1);
}

/// @tags foveation, ocu, vr
[numthreads(1, 1, 1)] void TestOCUEffectDisabledAndInvalidPolicy() {
	float4 centers = .5;
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(0, 0), 0, centers, float4(.5, .75, 0, 0)) == 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(0, 0), 0, centers, float4(.75, .5, 1, 0)) == 0);
	ASSERT(IsTrue, OCUPeripheralEffectWeightForProfile(float2(0, 0), 0, centers, float4(0, .5, 1, 0)) == 0);
}
