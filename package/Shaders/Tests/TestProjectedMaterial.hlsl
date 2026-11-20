#include "/Shaders/Common/ProjectedMaterial.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

void CheckProjectedReceiver(float2 uv, float2 hit, float2 uvDx, float2 uvDy, float3 axisU, float3 axisV)
{
	float3 position = axisU * uv.x + axisV * uv.y;
	float3 expected = axisU * hit.x + axisV * hit.y;
	float3 positionDx = axisU * uvDx.x + axisV * uvDx.y;
	float3 positionDy = axisU * uvDy.x + axisV * uvDy.y;
	float3 actual = position + ProjectedMaterial::GetParallaxPositionOffset(hit - uv, uvDx, uvDy, positionDx, positionDy);
	ASSERT(IsTrue, all(abs(actual - expected) < 1e-4));
}

/// @tags parallax, projected, materials
[numthreads(1, 1, 1)] void TestProjectedMaterialUvScaleAndOrientation() {
	float2 uv = float2(0.4, 0.7);
	float2 hit = float2(0.375, 0.71);
	CheckProjectedReceiver(uv, hit, float2(0.01, 0), float2(0, 0.01), float3(200, 0, 0), float3(0, 50, 0));
	CheckProjectedReceiver(uv, hit, float2(0, -0.02), float2(0.01, 0), float3(0, 200, 0), float3(0, 0, 50));
	CheckProjectedReceiver(uv, hit, float2(-0.01, 0.005), float2(0.003, 0.02), float3(-160, 80, 0), float3(10, 20, 50));
}

	/// @tags parallax, projected, vr
	[numthreads(1, 1, 1)] void TestProjectedMaterialStereoReceiver()
{
	float2 hit = float2(0.5, 0.5);
	float3 axisU = float3(120, 0, 40);
	float3 axisV = float3(0, 80, 20);
	CheckProjectedReceiver(float2(0.48, 0.51), hit, float2(0.004, 0.001), float2(-0.002, 0.007), axisU, axisV);
	CheckProjectedReceiver(float2(0.52, 0.49), hit, float2(0.006, -0.001), float2(0.003, 0.005), axisU, axisV);
}

/// @tags parallax, projected, vr
[numthreads(1, 1, 1)] void TestProjectedMaterialResolutionAndGrazing() {
	float2 uv = float2(0.4, 0.7);
	float2 hit = float2(0.375, 0.71);
	float3 axisU = float3(200, 0, 0);
	float3 axisV = float3(0, 50, 0);
	CheckProjectedReceiver(uv, hit, float2(0.002, 0.001), float2(0.003, 0.004), axisU, axisV);
	CheckProjectedReceiver(uv, hit, float2(0.004, 0.002), float2(0.006, 0.008), axisU, axisV);
	CheckProjectedReceiver(uv, hit, float2(0.1, 0.01), float2(0, 0.0001), axisU, axisV);
	CheckProjectedReceiver(uv, hit, float2(1e-10, 0), float2(0, 2e-10), axisU, axisV);
}

	/// @tags parallax, projected, materials
	[numthreads(1, 1, 1)] void TestProjectedMaterialZeroAndDegenerate()
{
	float3 dx = float3(2, 0, 0);
	float3 dy = float3(0, 2, 0);
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(0, float2(0.01, 0), float2(0, 0.02), dx, dy) == 0));
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(0.05, 0, 0, dx, dy) == 0));
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(0.05, float2(1, 1), float2(2, 2), dx, dy) == 0));
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(0.05, float2(1, 1), float2(1, 1.000001), dx, dy) == 0));
}

/// @tags parallax, projected, materials
[numthreads(1, 1, 1)] void TestProjectedMaterialInvalidInputs() {
	float invalid = asfloat(0x7fc00000u);
	float infinite = asfloat(0x7f800000u);
	float2 uvDx = float2(0.01, 0);
	float2 uvDy = float2(0, 0.01);
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(invalid, uvDx, uvDy, float3(1, 0, 0), float3(0, 1, 0)) == 0));
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(0.05, infinite, uvDy, float3(1, 0, 0), float3(0, 1, 0)) == 0));
	ASSERT(IsTrue, all(ProjectedMaterial::GetParallaxPositionOffset(0.05, uvDx, uvDy, infinite, float3(0, 1, 0)) == 0));
}
