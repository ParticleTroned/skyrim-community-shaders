#include "/Shaders/Common/SpecularAntialiasing.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags specular, antialiasing
[numthreads(1, 1, 1)] void TestFlatSurfacePreservesRoughness() {
	for (uint i = 0; i <= 20; ++i) {
		float roughness = float(i) / 20.0;
		float kernel = SpecularAntialiasing::GetKernelVariance(0.0.xxx, 0.0.xxx);
		float filtered = SpecularAntialiasing::FilterRoughness(roughness, kernel);
		ASSERT(IsTrue, abs(filtered - roughness) < 0.00001);
	}
}

	/// @tags specular, antialiasing
	[numthreads(1, 1, 1)] void TestNormalVariationBroadensHighlights()
{
	float roughness = 0.2;
	float lowKernel = SpecularAntialiasing::GetKernelVariance(float3(0.1, 0, 0), 0.0.xxx);
	float highKernel = SpecularAntialiasing::GetKernelVariance(float3(0.3, 0, 0), 0.0.xxx);
	float low = SpecularAntialiasing::FilterRoughness(roughness, lowKernel);
	float high = SpecularAntialiasing::FilterRoughness(roughness, highKernel);
	ASSERT(IsTrue, low > roughness && high > low);
	float transposedKernel = SpecularAntialiasing::GetKernelVariance(0.0.xxx, float3(0, -0.3, 0));
	ASSERT(IsTrue, abs(highKernel - transposedKernel) < 0.00001);
}

/// @tags specular, antialiasing
[numthreads(1, 1, 1)] void TestRoughnessFilteringIsBounded() {
	for (uint i = 0; i <= 20; ++i) {
		float roughness = float(i) / 20.0;
		float kernel = SpecularAntialiasing::GetKernelVariance(float3(2, 0, 0), float3(0, 2, 0));
		float filtered = SpecularAntialiasing::FilterRoughness(roughness, kernel);
		ASSERT(IsTrue, filtered >= roughness && filtered <= 1.0);
		ASSERT(IsTrue, isfinite(filtered));
	}
	float maxKernel = SpecularAntialiasing::GetKernelVariance(float3(2, 0, 0), float3(0, 2, 0));
	float smooth = SpecularAntialiasing::FilterRoughness(0, maxKernel);
	ASSERT(IsTrue, abs(smooth - sqrt(sqrt(SpecularAntialiasing::MaximumSlopeVariance))) < 0.00001);
}

	/// @tags specular, antialiasing, regression
	[numthreads(1, 1, 1)] void TestFilterUsesGGXSlopeRoughness()
{
	float kernel = SpecularAntialiasing::GetKernelVariance(float3(0.1, 0, 0), float3(0, 0.2, 0));
	float filtered = SpecularAntialiasing::FilterRoughness(0.2, kernel);
	ASSERT(IsTrue, abs(filtered - 0.315699397) < 0.00001);
}

/// @tags specular, antialiasing, regression
[numthreads(1, 1, 1)] void TestFilterClampsMaterialRoughness() {
	float kernel = SpecularAntialiasing::GetKernelVariance(float3(0.1, 0, 0), 0.0.xxx);
	ASSERT(IsTrue, SpecularAntialiasing::FilterRoughness(-1, 0) == 0);
	ASSERT(IsTrue, SpecularAntialiasing::FilterRoughness(2, kernel) == 1);
	ASSERT(IsTrue, SpecularAntialiasing::FilterRoughness(-1, kernel) == SpecularAntialiasing::FilterRoughness(0, kernel));
}
