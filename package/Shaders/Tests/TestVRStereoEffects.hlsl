#define VR
#define COMPUTESHADER
#include "/Shaders/Common/VRStereoEffects.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

static const float kEps = 0.000001;

/// @tags vr, stereo-effects
/// Bilinear footprints stay within the selected eye, including odd widths.
[numthreads(1, 1, 1)] void TestEyeTexelCenters() {
	float2 left = Stereo::ClampToEyeUV(float2(1, -0.5), 0, uint2(200, 100));
	float2 right = Stereo::ClampToEyeUV(float2(0, 1.5), 1, uint2(200, 100));
	ASSERT(IsTrue, abs(left.x - 99.5 / 200.0) < kEps);
	ASSERT(IsTrue, abs(right.x - 100.5 / 200.0) < kEps);
	ASSERT(AreEqual, left.y, -0.5);
	ASSERT(AreEqual, right.y, 1.5);
	left = Stereo::ClampToEyeUV(float2(1, 0), 0, uint2(201, 100));
	right = Stereo::ClampToEyeUV(float2(0, 0), 1, uint2(201, 100));
	ASSERT(IsTrue, abs(left.x - 99.5 / 201.0) < kEps);
	ASSERT(IsTrue, abs(right.x - 100.5 / 201.0) < kEps);
}

	/// @tags vr, stereo-effects
	/// A one-texel eye clamps both ends to the same valid center.
	[numthreads(1, 1, 1)] void TestSingleTexelEye()
{
	ASSERT(AreEqual, Stereo::ClampToEyeUV(float2(-1, 0), 0, uint2(2, 1)).x, 0.25);
	ASSERT(AreEqual, Stereo::ClampToEyeUV(float2(2, 0), 0, uint2(2, 1)).x, 0.25);
	ASSERT(AreEqual, Stereo::ClampToEyeUV(float2(-1, 0), 1, uint2(2, 1)).x, 0.75);
	ASSERT(AreEqual, Stereo::ClampToEyeUV(float2(2, 0), 1, uint2(2, 1)).x, 0.75);
}

/// @tags vr, stereo-effects
/// Current and previous frames use their own active extent and texture width.
[numthreads(1, 1, 1)] void TestDynamicEyeSeams() {
	float2 left = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(0.45, 0.3), 0, uint2(200, 100), float2(0.8, 0.6));
	float2 right = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(0.35, 0.3), 1, uint2(200, 100), float2(0.8, 0.6));
	ASSERT(IsTrue, abs(left.x - 0.3975) < kEps);
	ASSERT(IsTrue, abs(right.x - 0.4025) < kEps);
	ASSERT(IsTrue, abs(left.y - 0.3) < kEps);
	ASSERT(IsTrue, abs(right.y - 0.3) < kEps);
	left = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(1, 0.3), 0, uint2(100, 50), float2(0.6, 0.8));
	right = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(0, 0.3), 1, uint2(100, 50), float2(0.6, 0.8));
	ASSERT(IsTrue, abs(left.x - 0.295) < kEps);
	ASSERT(IsTrue, abs(right.x - 0.305) < kEps);
}

	/// @tags vr, stereo-effects
	/// Fractional scaled extents must still clamp to the physical texture's centers.
	[numthreads(1, 1, 1)] void TestFractionalDynamicExtent()
{
	float2 left = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(1, 0.3), 0, uint2(200, 100), float2(0.803, 0.6));
	float2 right = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(0, 0.3), 1, uint2(200, 100), float2(0.803, 0.6));
	ASSERT(IsTrue, abs(left.x - 79.5 / 200.0) < kEps);
	ASSERT(IsTrue, abs(right.x - 80.5 / 200.0) < kEps);
	float2 outer = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(float2(1, 0.3), 1, uint2(200, 100), float2(0.803, 0.6));
	ASSERT(IsTrue, abs(outer.x - 159.5 / 200.0) < kEps);
}

/// @tags vr, stereo-effects
/// Existing coordinates remain unchanged when their footprint is inside one eye.
[numthreads(1, 1, 1)] void TestInteriorDynamicUVUnchanged() {
	float2 uv = float2(0.2, 0.3);
	float2 result = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(uv, 0, uint2(200, 100), float2(0.803, 0.6));
	ASSERT(IsTrue, all(abs(result - uv) < kEps));
}
