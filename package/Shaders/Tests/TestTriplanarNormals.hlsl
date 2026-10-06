#include "/Shaders/Common/Math.hlsli"
#include "/Shaders/Common/Triplanar.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags triplanar, snow, normals
[numthreads(1, 1, 1)] void TestNeutralProjectionPreservesSurface() {
	float3 surfaces[] = {
		float3(1, 0, 0), float3(-1, 0, 0),
		float3(0, 1, 0), float3(0, -1, 0),
		float3(0, 0, 1), float3(0, 0, -1),
		normalize(float3(1, 2, 3)), normalize(float3(-3, 2, -1))
	};
	float3 weights = float3(0.25, 0.25, 0.5);
	float selectors[] = { 0.125, 0.375, 0.75 };
	for (uint surface = 0; surface < 8; ++surface) {
		for (uint plane = 0; plane < 3; ++plane) {
			float3 actual = Triplanar::TransformStochasticNormal(float3(0, 0, 1), surfaces[surface], weights, selectors[plane]);
			ASSERT(IsTrue, all(abs(actual - surfaces[surface]) < 1e-5));
		}
	}
}

	/// @tags triplanar, snow, normals
	[numthreads(1, 1, 1)] void TestProjectionNormalFollowsTextureAxes()
{
	float3 surfaces[] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
	float3 uAxes[] = { float3(0, 1, 0), float3(1, 0, 0), float3(1, 0, 0) };
	float3 vAxes[] = { float3(0, 0, 1), float3(0, 0, 1), float3(0, 1, 0) };
	float3 weights = float3(0.25, 0.25, 0.5);
	float selectors[] = { 0.125, 0.375, 0.75 };
	for (uint plane = 0; plane < 3; ++plane) {
		for (int direction = -1; direction <= 1; direction += 2) {
			float3 surface = surfaces[plane] * direction;
			float3 uNormal = Triplanar::TransformStochasticNormal(float3(0.6, 0, 0.8), surface, weights, selectors[plane]);
			float3 vNormal = Triplanar::TransformStochasticNormal(float3(0, -0.6, 0.8), surface, weights, selectors[plane]);
			ASSERT(IsTrue, abs(dot(uNormal, uAxes[plane]) - 0.6) < 1e-5);
			ASSERT(IsTrue, abs(dot(vNormal, vAxes[plane]) + 0.6) < 1e-5);
			ASSERT(IsTrue, abs(dot(uNormal, surface) - 0.8) < 1e-5);
			ASSERT(IsTrue, abs(dot(vNormal, surface) - 0.8) < 1e-5);
		}
	}
}

/// @tags triplanar, snow, normals
[numthreads(1, 1, 1)] void TestProjectionNormalSelectionBoundaries() {
	float3 surface = normalize(float3(1, 1, 1));
	float3 weights = float3(0.25, 0.25, 0.5);
	float3 normal = float3(0.6, 0, 0.8);
	float3 xPlane = Triplanar::TransformStochasticNormal(normal, surface, weights, 0.249);
	float3 yPlane = Triplanar::TransformStochasticNormal(normal, surface, weights, 0.25);
	float3 zPlane = Triplanar::TransformStochasticNormal(normal, surface, weights, 0.5);
	ASSERT(IsTrue, xPlane.y > xPlane.z && xPlane.z > xPlane.x);
	ASSERT(IsTrue, yPlane.x > yPlane.z && yPlane.z > yPlane.y);
	ASSERT(IsTrue, zPlane.x > zPlane.y && zPlane.y > zPlane.z);
}

	/// @tags triplanar, snow, normals
	[numthreads(1, 1, 1)] void TestProjectionPlaneSelectionBoundaries()
{
	float3 weights = float3(0.25, 0.25, 0.5);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(weights, 0) == 0);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(weights, 0.249) == 0);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(weights, 0.25) == 1);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(weights, 0.499) == 1);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(weights, 0.5) == 2);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(weights, 1) == 2);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(float3(0, 1, 0), 0) == 1);
	ASSERT(IsTrue, Triplanar::SelectProjectionPlane(float3(0, 0, 1), 0) == 2);
}

/// @tags triplanar, snow, normals
[numthreads(1, 1, 1)] void TestMirroredProjectionNormalFollowsTextureAxes() {
	float3 surfaces[] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
	float3 uAxes[] = { float3(0, 1, 0), float3(1, 0, 0), float3(1, 0, 0) };
	float3 vAxes[] = { float3(0, 0, 1), float3(0, 0, 1), float3(0, 1, 0) };
	float3 weights = float3(0.25, 0.25, 0.5);
	float selectors[] = { 0.125, 0.375, 0.75 };
	float3 normal = normalize(float3(0.3, -0.4, 0.8));
	for (uint plane = 0; plane < 3; ++plane) {
		for (int facing = -1; facing <= 1; facing += 2) {
			float3 surface = surfaces[plane] * facing;
			for (int scaleSign = -1; scaleSign <= 1; scaleSign += 2) {
				float3 oriented = Triplanar::OrientNormalForScale(normal, 2 * scaleSign);
				float3 actual = Triplanar::TransformStochasticNormal(oriented, surface, weights, selectors[plane]);
				ASSERT(IsTrue, abs(dot(actual, uAxes[plane]) - normal.x * scaleSign) < 1e-5);
				ASSERT(IsTrue, abs(dot(actual, vAxes[plane]) - normal.y * scaleSign) < 1e-5);
				ASSERT(IsTrue, abs(dot(actual, surface) - normal.z) < 1e-5);
				ASSERT(IsTrue, abs(dot(actual, actual) - 1) < 1e-5);
			}
		}
	}
	ASSERT(IsTrue, all(Triplanar::OrientNormalForScale(float3(0, 0, 1), -2) == float3(0, 0, 1)));
	ASSERT(IsTrue, all(Triplanar::OrientNormalForScale(normal, 0) == normal));
}
