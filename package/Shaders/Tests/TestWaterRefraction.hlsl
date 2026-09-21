#define COMPUTESHADER
#include "/Shaders/Common/WaterRefraction.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

static const float kEps = 0.000001;

/// @tags water, refraction, flat
/// Both textures constrain the footprint, even when the limiting axis differs.
[numthreads(1, 1, 1)] void TestRefractionTextureIntersection() {
	float2 minUV;
	float2 maxUV;
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(128, 128), float2(256, 64), minUV, maxUV));
	ASSERT(IsTrue, all(abs(minUV - float2(0.5 / 128.0, 0.5 / 64.0)) < kEps));
	ASSERT(IsTrue, all(abs(maxUV - float2(127.5 / 128.0, 63.5 / 64.0)) < kEps));
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(-1, 2), 0.5, minUV, maxUV), float2(minUV.x, maxUV.y));
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(0.3, 0.7), 0.5, minUV, maxUV), float2(0.3, 0.7));
}

	/// @tags water, refraction, flat
	/// Fractional render scales must stop at the last complete pixel's centre.
	[numthreads(1, 1, 1)] void TestRefractionFractionalExtent()
{
	float2 minUV;
	float2 maxUV;
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(160.6, 60.3), float2(321.2, 120.6), minUV, maxUV));
	ASSERT(IsTrue, all(abs(maxUV * float2(160.6, 60.3) - float2(159.5, 59.5)) < 0.00002));
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(2, 2), 0.5, minUV, maxUV), maxUV);
}

/// @tags water, refraction, flat
/// Invalid projections use the undistorted position, with a finite last resort.
[numthreads(1, 1, 1)] void TestRefractionInvalidProjection() {
	float invalid = asfloat(0x7fc00000u);
	float2 minUV = float2(0.1, 0.2);
	float2 maxUV = float2(0.9, 0.8);
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(invalid, 0), float2(0.3, 0.4), minUV, maxUV), float2(0.3, 0.4));
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(invalid, 0), float2(-1, 2), minUV, maxUV), float2(0.1, 0.8));
	ASSERT(AreEqual, WaterRefraction::ClampUV(invalid, invalid, minUV, maxUV), float2(0.5, 0.5));
}

	/// @tags water, refraction, flat
	/// A one-pixel footprint is valid; missing or non-finite extents are rejected.
	[numthreads(1, 1, 1)] void TestRefractionInvalidExtents()
{
	float2 minUV;
	float2 maxUV;
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(1, 1), float2(2, 2), minUV, maxUV));
	ASSERT(AreEqual, minUV, float2(0.5, 0.5));
	ASSERT(AreEqual, minUV, maxUV);
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(0, 1), float2(2, 2), minUV, maxUV));
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(2, 2), float2(2, 0.5), minUV, maxUV));
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(asfloat(0x7f800000u), 2), float2(2, 2), minUV, maxUV));
}
