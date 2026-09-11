#define COMPUTESHADER
#include "/Shaders/Common/VR.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags flat, stereo-effects
/// The texture-aware overload is an exact identity outside VR.
[numthreads(1, 1, 1)] void TestFlatTexelClampIdentity() {
	float2 uv = float2(-0.5, 1.5);
	ASSERT(AreEqual, Stereo::ClampToEyeUV(uv, 0, uint2(200, 100)), uv);
	ASSERT(AreEqual, Stereo::ClampToEyeUV(uv, 1, uint2(201, 100)), uv);
}
