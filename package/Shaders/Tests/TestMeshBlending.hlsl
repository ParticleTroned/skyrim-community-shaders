// HLSL unit tests for Mesh Blending's pure fade and LAND/LTEX policy.
#define MESH_BLENDING_MATH_ONLY
#include "/Shaders/MeshBlending/MeshBlending.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

uint PackLandscapeClass(uint descriptor, uint layerIndex, uint materialClass)
{
	const uint shift = MeshBlending::kLandscapeClassShift + layerIndex * MeshBlending::kLandscapeClassBits;
	return descriptor | ((materialClass & 0x3u) << shift);
}

float LandscapeWeightTotal(float4 weights1, float2 weights2)
{
	return weights1.x + weights1.y + weights1.z + weights1.w + weights2.x + weights2.y;
}

bool NearlyEqual(float lhs, float rhs)
{
	return abs(lhs - rhs) < 1.0e-5f;
}

/// @tags mesh-blending, fade
[numthreads(1, 1, 1)] void TestMeshBlendingFadeEndpoints()
{
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(0.0f, 12.0f, 0.25f, 64.0f), 0.0f);
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(0.25f, 12.0f, 0.25f, 64.0f), 0.0f);
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(6.25f, 12.0f, 0.25f, 64.0f), 0.5f);
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(12.25f, 12.0f, 0.25f, 64.0f), 1.0f);
}

/// @tags mesh-blending, safety
[numthreads(1, 1, 1)] void TestMeshBlendingGapSafety()
{
	// A tiny negative separation is tolerated as depth precision noise.
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(-0.1f, 12.0f, 0.25f, 64.0f), 0.0f);
	// A receiver clearly in front and an implausibly distant receiver both fail open.
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(-0.5f, 12.0f, 0.25f, 64.0f), 1.0f);
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(80.0f, 12.0f, 0.25f, 64.0f), 1.0f);
}

/// @tags mesh-blending, settings
[numthreads(1, 1, 1)] void TestMeshBlendingSanitizesFadeRange()
{
	// MaximumGap is raised to at least bias + width. This avoids a jump from a
	// partial fade directly to fail-open opacity when settings are malformed.
	const float partialFade = MeshBlending::ComputeFadeFromGap(2.0f, 4.0f, 1.0f, 2.0f);
	ASSERT(IsTrue, partialFade > 0.0f);
	ASSERT(IsTrue, partialFade < 1.0f);
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(5.0f, 4.0f, 1.0f, 2.0f), 1.0f);

	// Zero/negative widths are clamped to a small positive interval.
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(1.0f, 0.0f, 1.0f, 8.0f), 0.0f);
	ASSERT(AreEqual, MeshBlending::ComputeFadeFromGap(2.0f, -1.0f, 1.0f, 8.0f), 1.0f);
}

/// @tags mesh-blending, strength
[numthreads(1, 1, 1)] void TestMeshBlendingBlendStrength()
{
	ASSERT(AreEqual, MeshBlending::ApplyBlendStrength(0.0f, 0.0f), 1.0f);
	ASSERT(AreEqual, MeshBlending::ApplyBlendStrength(0.0f, 0.5f), 0.5f);
	ASSERT(AreEqual, MeshBlending::ApplyBlendStrength(0.25f, 1.0f), 0.25f);
}

/// @tags mesh-blending, landscape, descriptor
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeDescriptorDecode()
{
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 4u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 5u, MeshBlending::LandscapeClass::Soft);

	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, 0u), MeshBlending::LandscapeClass::Unknown);
	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, 4u), MeshBlending::LandscapeClass::Hard);
	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, 5u), MeshBlending::LandscapeClass::Soft);
	ASSERT(IsTrue, (descriptor & ~MeshBlending::kLandscapeClassMask) == 0u);
}

/// @tags mesh-blending, landscape, layers-five-six
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeRemapsLayersFiveAndSix()
{
	float4 weights1 = 0.0f;
	float2 weights2 = float2(0.5f, 0.5f);
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 4u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 5u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, changed);
	ASSERT(IsTrue, NearlyEqual(weights2.x, 0.25f));
	ASSERT(IsTrue, NearlyEqual(weights2.y, 0.75f));
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, soft-hard
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftOverHard()
{
	float4 weights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, changed);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.25f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.75f));
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, soft-hard
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftTransferIsProportional()
{
	float4 weights1 = float4(0.8f, 0.2f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.64f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.36f));
}

/// @tags mesh-blending, landscape, soft-soft
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftMaterialsEqualize()
{
	float4 weights1 = float4(0.7f, 0.3f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, changed);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.5f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.5f));
}

/// @tags mesh-blending, landscape, soft-soft, threshold
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftEqualizesEveryActiveLayer()
{
	float4 weights1 = float4(0.989f, 0.011f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.5f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.5f));
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, hierarchy
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftLayersEqualizeOverHard()
{
	float4 weights1 = float4(0.5f, 0.3f, 0.2f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 2u, MeshBlending::LandscapeClass::Soft);

	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.25f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.375f));
	ASSERT(IsTrue, NearlyEqual(weights1.z, 0.375f));
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, hard-hard
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeHardMaterialsStayCrisp()
{
	float4 weights1 = float4(0.4f, 0.6f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Hard);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, changed);
	ASSERT(AreEqual, weights1.x, 0.0f);
	ASSERT(IsTrue, NearlyEqual(weights1.y, 1.0f));
}

/// @tags mesh-blending, landscape, fail-open
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeUnknownAndReservedFailOpen()
{
	float4 unknownWeights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 unknownWeights2 = 0.0f;
	uint unknownDescriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	const bool unknownChanged = MeshBlending::RemapLandscapeWeights(unknownWeights1, unknownWeights2, unknownDescriptor, 1.0f);
	ASSERT(IsFalse, unknownChanged);
	ASSERT(IsTrue, all(unknownWeights1 == float4(0.5f, 0.5f, 0.0f, 0.0f)));

	float4 reservedWeights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 reservedWeights2 = 0.0f;
	uint reservedDescriptor = 0u;
	reservedDescriptor = PackLandscapeClass(reservedDescriptor, 0u, MeshBlending::LandscapeClass::Hard);
	reservedDescriptor = PackLandscapeClass(reservedDescriptor, 1u, MeshBlending::LandscapeClass::Reserved);
	const bool reservedChanged = MeshBlending::RemapLandscapeWeights(reservedWeights1, reservedWeights2, reservedDescriptor, 1.0f);
	ASSERT(IsFalse, reservedChanged);
	ASSERT(IsTrue, all(reservedWeights1 == float4(0.5f, 0.5f, 0.0f, 0.0f)));
}

/// @tags mesh-blending, landscape, inactive
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeNeverActivatesInactiveLayers()
{
	float4 weights1 = float4(0.99f, 0.01f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsFalse, changed);
	ASSERT(AreEqual, weights1.x, 0.99f);
	ASSERT(AreEqual, weights1.y, 0.01f);
}

/// @tags mesh-blending, landscape, strength
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeZeroStrengthIsExactNoOp()
{
	float4 weights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 0.0f);
	ASSERT(IsFalse, changed);
	ASSERT(IsTrue, all(weights1 == float4(0.5f, 0.5f, 0.0f, 0.0f)));
}

/// @tags mesh-blending, landscape, threshold, conservation
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeThresholdMassIsRenormalized()
{
	float4 weights1 = float4(0.989f, 0.011f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Hard);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 0.5f);
	ASSERT(IsTrue, changed);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 1.0f));
	ASSERT(AreEqual, weights1.y, 0.0f);
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
	ASSERT(IsTrue, weights1.x == 0.0f || weights1.x > MeshBlending::kLandscapeActiveWeightThreshold);
	ASSERT(IsTrue, weights1.y == 0.0f || weights1.y > MeshBlending::kLandscapeActiveWeightThreshold);
}

/// @tags mesh-blending, landscape, descriptor, packed-zero
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapePackedZeroIsNoOp()
{
	float4 weights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, 0u, 1.0f);
	ASSERT(IsFalse, changed);
	ASSERT(IsTrue, all(weights1 == float4(0.5f, 0.5f, 0.0f, 0.0f)));
}

/// @tags mesh-blending, projected-snow
[numthreads(1, 1, 1)] void TestMeshBlendingProjectedSnowCoverage()
{
	ASSERT(AreEqual, MeshBlending::ComputeProjectedSnowCoverage(-0.1f, 0.1f, 2.0f), 0.0f);
	ASSERT(AreEqual, MeshBlending::ComputeProjectedSnowCoverage(0.0f, 0.1f, 2.0f), 0.5f);
	ASSERT(AreEqual, MeshBlending::ComputeProjectedSnowCoverage(0.1f, 0.1f, 2.0f), 1.0f);
	const float widerCoverage = MeshBlending::ComputeProjectedSnowCoverage(0.1f, 0.1f, 4.0f);
	ASSERT(IsTrue, widerCoverage > 0.5f);
	ASSERT(IsTrue, widerCoverage < 1.0f);
}
