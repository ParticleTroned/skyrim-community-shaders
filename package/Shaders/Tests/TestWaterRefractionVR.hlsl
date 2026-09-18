#define VR
#define COMPUTESHADER
#include "/Shaders/Common/WaterRefraction.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

static const float kEps = 0.000001;

/// @tags water, refraction, vr
/// Distortion cannot cross either eye's seam or the active viewport edges.
[numthreads(1, 1, 1)] void TestRefractionEyeBounds() {
	float2 minUV;
	float2 maxUV;
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(160, 60), float2(320, 120), 0, minUV, maxUV));
	ASSERT(IsTrue, abs(maxUV.x - 79.5 / 160.0) < kEps);
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(0.51, 1.1), float2(0.25, 0.5), minUV, maxUV), maxUV);
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(160, 60), float2(320, 120), 1, minUV, maxUV));
	ASSERT(IsTrue, abs(minUV.x - 80.5 / 160.0) < kEps);
	ASSERT(AreEqual, WaterRefraction::ClampUV(float2(0.49, -0.1), float2(0.75, 0.5), minUV, maxUV), minUV);
}

	/// @tags water, refraction, vr
	/// Physical eye ownership can differ from an x >= 0.5 test at fractional widths.
	[numthreads(1, 1, 1)] void TestRefractionFractionalEyeBoundary()
{
	float2 minUV;
	float2 maxUV;
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(201.6, 90), float2(403.2, 180), 1, minUV, maxUV));
	ASSERT(IsTrue, minUV.x < 0.5);
	ASSERT(IsTrue, abs(minUV.x - 201.5 / 403.2) < kEps);
	ASSERT(IsTrue, abs(maxUV.x - 200.5 / 201.6) < kEps);
	ASSERT(IsTrue, abs(WaterRefraction::ClampUV(float2(0.49, 0.4), float2(0.75, 0.4), minUV, maxUV).x - minUV.x) < kEps);
}

/// @tags water, refraction, vr
/// Single-pixel eyes stay valid, but disjoint footprints cannot share a sample.
[numthreads(1, 1, 1)] void TestRefractionNarrowEyes() {
	float2 minUV;
	float2 maxUV;
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(2, 1), float2(4, 2), 0, minUV, maxUV));
	ASSERT(AreEqual, minUV, float2(0.25, 0.5));
	ASSERT(AreEqual, minUV, maxUV);
	ASSERT(IsTrue, WaterRefraction::TryGetUVBounds(float2(2, 1), float2(4, 2), 1, minUV, maxUV));
	ASSERT(AreEqual, minUV, float2(0.75, 0.5));
	ASSERT(AreEqual, minUV, maxUV);
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(2, 1), float2(3, 1), 0, minUV, maxUV));
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(1, 1), float2(4, 2), 1, minUV, maxUV));
}
