#define COMPUTESHADER
#include "/Shaders/LightLimitFix/EffectSpotlight.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

LightLimitFix::EffectSpotlight TestProjection()
{
	LightLimitFix::EffectSpotlight projection;
	projection.clipX = float4(1, 0, 0, 0);
	projection.clipY = float4(0, 1, 0, 0);
	projection.clipW = float4(0, 0, 1, 0);
	return projection;
}

/// @tags light-limit-fix, effects, spotlight
/// A spotlight illuminates its axis and rejects the cone edge, exterior and rear hemisphere.
[numthreads(1, 1, 1)] void TestEffectSpotlightBounds() {
	LightLimitFix::EffectSpotlight projection = TestProjection();
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(0, 0, 10)), 1.0);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(10, 0, 10)), 0.0);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(0, 12, 10)), 0.0);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(0, 0, -10)), 0.0);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(0, 0, 0)), 0.0);
}

	/// @tags light-limit-fix, effects, spotlight
	/// Authored falloff agrees with the native radius-based expression, including the quadratic shortcut.
	[numthreads(1, 1, 1)] void TestEffectSpotlightFalloff()
{
	LightLimitFix::EffectSpotlight projection = TestProjection();
	float3 position = float3(3, 4, 10);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, position), 0.75);
	ASSERT(IsTrue, abs(LightLimitFix::GetEffectSpotlightFootprint(projection, 2, position) - 0.9375) < 0.00001);
	ASSERT(IsTrue, abs(LightLimitFix::GetEffectSpotlightFootprint(projection, 0.5, position) - 0.5) < 0.00001);
}

/// @tags light-limit-fix, effects, spotlight, vr
/// Each eye recovers the same absolute point for a translated, rotated and asymmetric cone.
[numthreads(1, 1, 1)] void TestEffectSpotlightStereoOrigin() {
	LightLimitFix::EffectSpotlight projection;
	projection.clipX = float4(0, 2, 0, -40);
	projection.clipY = float4(0, 0, 3, -90);
	projection.clipW = float4(1, 0, 0, -10);
	float3 cameraLeft = float3(1024, 2048, 512);
	float3 cameraRight = float3(1028, 2048, 512);
	float3 worldPosition = float3(20, 22, 31);
	float3 relativeLeft = worldPosition - cameraLeft;
	float3 relativeRight = worldPosition - cameraRight;
	float left = LightLimitFix::GetEffectSpotlightFootprint(projection, 1, relativeLeft + cameraLeft);
	float right = LightLimitFix::GetEffectSpotlightFootprint(projection, 1, relativeRight + cameraRight);
	ASSERT(IsTrue, abs(left - 0.75) < 0.00001);
	ASSERT(AreEqual, left, right);
}

	/// @tags light-limit-fix, effects, spotlight
	/// Non-finite or overflowing projected coordinates cannot create light outside a valid cone.
	[numthreads(1, 1, 1)] void TestEffectSpotlightInvalidProjection()
{
	LightLimitFix::EffectSpotlight projection = TestProjection();
	projection.clipW.w = asfloat(0x7F800000u);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(0, 0, 10)), 0.0);
	projection.clipW.w = 0;
	projection.clipX.w = asfloat(0x7FC00000u);
	ASSERT(AreEqual, LightLimitFix::GetEffectSpotlightFootprint(projection, 1, float3(0, 0, 10)), 0.0);
}
