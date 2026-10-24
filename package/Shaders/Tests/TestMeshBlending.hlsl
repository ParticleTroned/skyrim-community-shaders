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

bool NearlyEqualLandscapeWeights(float4 lhs, float4 rhs)
{
	return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y) &&
	       NearlyEqual(lhs.z, rhs.z) && NearlyEqual(lhs.w, rhs.w);
}

/// @tags mesh-blending, fade
[numthreads(1, 1, 1)] void TestMeshBlendingFadeEndpoints() {
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
[numthreads(1, 1, 1)] void TestMeshBlendingSanitizesFadeRange() {
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
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeDescriptorDecode() {
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 4u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 5u, MeshBlending::LandscapeClass::Soft);

	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, 0u), MeshBlending::LandscapeClass::Unknown);
	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, 4u), MeshBlending::LandscapeClass::Hard);
	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, 5u), MeshBlending::LandscapeClass::Soft);
	ASSERT(IsTrue, (descriptor & ~MeshBlending::kLandscapeClassMask) == 0u);
}

	/// @tags mesh-blending, landscape, descriptor, terrain-variation
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapePreservesTerrainVariation()
{
	const uint terrainVariation = 1u << 10;
	uint descriptor = terrainVariation;
	for (uint layer = 0u; layer < 6u; ++layer) {
		descriptor = PackLandscapeClass(descriptor, layer, MeshBlending::LandscapeClass::Soft);
		ASSERT(AreEqual, MeshBlending::GetLandscapeClass(descriptor, layer), MeshBlending::LandscapeClass::Soft);
	}
	ASSERT(AreEqual, descriptor & terrainVariation, terrainVariation);
	ASSERT(AreEqual, descriptor & ~MeshBlending::kLandscapeClassMask, terrainVariation);
	ASSERT(AreEqual, MeshBlending::GetLandscapeClass(terrainVariation, 0u), MeshBlending::LandscapeClass::Unknown);
}

/// @tags mesh-blending, landscape, layers-five-six
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeRemapsLayersFiveAndSix() {
	float4 weights1 = 0.0f;
	float2 weights2 = float2(0.5f, 0.5f);
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 4u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 5u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, changed);
	ASSERT(IsTrue, NearlyEqual(weights2.x, 0.255f));
	ASSERT(IsTrue, NearlyEqual(weights2.y, 0.745f));
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
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.255f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.745f));
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, soft-hard
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftTransferIsProportional() {
	float4 weights1 = float4(0.8f, 0.2f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.6468367f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.3531633f));
}

	/// @tags mesh-blending, landscape, soft-soft
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftMaterialsPreserveAuthoredMix()
{
	float4 weights1 = float4(0.7f, 0.3f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);

	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsFalse, changed);
	ASSERT(AreEqual, weights1.x, 0.7f);
	ASSERT(AreEqual, weights1.y, 0.3f);
}

/// @tags mesh-blending, landscape, soft-soft, threshold
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftActivationPreservesAuthoredMix() {
	uint descriptor = 0u;
	for (uint layer = 0u; layer < 6u; ++layer) {
		descriptor = PackLandscapeClass(descriptor, layer, MeshBlending::LandscapeClass::Soft);
	}

	for (uint strengthStep = 1u; strengthStep <= 4u; ++strengthStep) {
		for (uint boundaryStep = 0u; boundaryStep < 3u; ++boundaryStep) {
			const float enteringWeight = asfloat(asuint(MeshBlending::kLandscapeActiveWeightThreshold) + boundaryStep - 1u);
			float4 weights1 = float4(1.0f - enteringWeight, enteringWeight, 0.0f, 0.0f);
			float2 weights2 = 0.0f;
			const float4 originalTwo = weights1;
			const bool changedTwo = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, float(strengthStep) * 0.25f);
			ASSERT(IsFalse, changedTwo);
			ASSERT(IsTrue, all(weights1 == originalTwo));

			weights1 = float4(0.7f - enteringWeight, 0.3f, enteringWeight, 0.0f);
			const float4 originalThree = weights1;
			const bool changedThree = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, float(strengthStep) * 0.25f);
			ASSERT(IsFalse, changedThree);
			ASSERT(IsTrue, all(weights1 == originalThree));

			weights1 = float4(0.2f, 0.2f, 0.2f, 0.2f);
			weights2 = float2(0.2f - enteringWeight, enteringWeight);
			const float2 originalLastTwo = weights2;
			const bool changedSix = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, float(strengthStep) * 0.25f);
			ASSERT(IsFalse, changedSix);
			ASSERT(IsTrue, all(weights1 == float4(0.2f, 0.2f, 0.2f, 0.2f)));
			ASSERT(IsTrue, all(weights2 == originalLastTwo));
		}
	}
}

	/// @tags mesh-blending, landscape, hierarchy
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftLayersRetainAuthoredOrdering()
{
	float4 weights1 = float4(0.5f, 0.3f, 0.2f, 0.0f);
	float2 weights2 = 0.0f;
	uint descriptor = 0u;
	descriptor = PackLandscapeClass(descriptor, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 2u, MeshBlending::LandscapeClass::Soft);

	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.2575258f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.4464948f));
	ASSERT(IsTrue, NearlyEqual(weights1.z, 0.2959794f));
	ASSERT(IsTrue, weights1.y > weights1.z);
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, soft-hard, threshold, strength
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeFirstSoftLayerEntersContinuously() {
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);
	for (uint strengthStep = 1u; strengthStep <= 4u; ++strengthStep) {
		for (uint boundaryStep = 0u; boundaryStep < 3u; ++boundaryStep) {
			const float enteringWeight = asfloat(asuint(MeshBlending::kLandscapeActiveWeightThreshold) + boundaryStep - 1u);
			float4 weights1 = float4(1.0f - enteringWeight, enteringWeight, 0.0f, 0.0f);
			float2 weights2 = 0.0f;
			const float4 original = weights1;
			MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, float(strengthStep) * 0.25f);
			ASSERT(IsTrue, NearlyEqualLandscapeWeights(weights1, original));
			ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
		}
	}
}

	/// @tags mesh-blending, landscape, hierarchy, threshold, strength
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeThirdSoftLayerKeepsExistingTransfer()
{
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	for (uint layer = 1u; layer < 4u; ++layer) {
		descriptor = PackLandscapeClass(descriptor, layer, MeshBlending::LandscapeClass::Soft);
	}
	for (uint strengthStep = 1u; strengthStep <= 4u; ++strengthStep) {
		const float strength = float(strengthStep) * 0.25f;
		float4 reference1 = float4(0.5f, 0.3f, 0.19f, 0.01f);
		float2 reference2 = 0.0f;
		const bool referenceChanged = MeshBlending::RemapLandscapeWeights(reference1, reference2, descriptor, strength);
		ASSERT(IsTrue, referenceChanged);
		ASSERT(IsTrue, reference1.x < 0.5f);
		for (uint boundaryStep = 0u; boundaryStep < 3u; ++boundaryStep) {
			const float enteringWeight = asfloat(asuint(MeshBlending::kLandscapeActiveWeightThreshold) + boundaryStep - 1u);
			float4 weights1 = float4(0.5f, 0.3f, 0.2f - enteringWeight, enteringWeight);
			float2 weights2 = 0.0f;
			MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, strength);
			ASSERT(IsTrue, NearlyEqualLandscapeWeights(weights1, reference1));
			ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
		}
	}
}

/// @tags mesh-blending, landscape, soft-hard, strength
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapePartialStrengthScalesTransfer() {
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);
	float4 weights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	const bool changed = MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 0.5f);
	ASSERT(IsTrue, changed);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.3775f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.6225f));
}

	/// @tags mesh-blending, landscape, hierarchy, threshold
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeHardActivationKeepsExistingSoftTransfer()
{
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 2u, MeshBlending::LandscapeClass::Soft);
	descriptor = PackLandscapeClass(descriptor, 3u, MeshBlending::LandscapeClass::Hard);
	for (uint strengthStep = 1u; strengthStep <= 4u; ++strengthStep) {
		const float strength = float(strengthStep) * 0.25f;
		float4 reference1 = float4(0.5f, 0.3f, 0.19f, 0.01f);
		float2 reference2 = 0.0f;
		MeshBlending::RemapLandscapeWeights(reference1, reference2, descriptor, strength);
		const float enteringWeight = asfloat(asuint(MeshBlending::kLandscapeActiveWeightThreshold) + 1u);
		float4 weights1 = float4(0.5f, 0.3f, 0.2f - enteringWeight, enteringWeight);
		float2 weights2 = 0.0f;
		MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, strength);
		ASSERT(IsTrue, NearlyEqual(weights1.y, reference1.y));
		ASSERT(IsTrue, NearlyEqual(weights1.z, reference1.z));
		ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
	}
}

/// @tags mesh-blending, landscape, hard-hard
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeHardMaterialsStayCrisp() {
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

	/// @tags mesh-blending, landscape, hard-hard, tie
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeHardTiesChooseLowestIndex()
{
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Hard);
	float4 weights1 = float4(0.5f, 0.5f, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 0.5f);
	ASSERT(IsTrue, NearlyEqual(weights1.x, 0.75f));
	ASSERT(IsTrue, NearlyEqual(weights1.y, 0.25f));

	descriptor = PackLandscapeClass(descriptor, 2u, MeshBlending::LandscapeClass::Soft);
	weights1 = float4(0.4f, 0.4f, 0.2f, 0.0f);
	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, weights1.x > 0.0f);
	ASSERT(AreEqual, weights1.y, 0.0f);
	ASSERT(IsTrue, weights1.z > 0.2f);
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, fail-open
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeUnknownAndReservedFailOpen() {
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
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeZeroStrengthIsExactNoOp() {
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

/// @tags mesh-blending, landscape, threshold, conservation
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeSoftActivationDoesNotInheritPrunedHardMass() {
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 2u, MeshBlending::LandscapeClass::Soft);
	for (uint boundaryStep = 0u; boundaryStep < 3u; ++boundaryStep) {
		const float enteringWeight = asfloat(asuint(MeshBlending::kLandscapeActiveWeightThreshold) + boundaryStep - 1u);
		float4 weights1 = float4(0.989f - enteringWeight, 0.011f, enteringWeight, 0.0f);
		float2 weights2 = 0.0f;
		MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 0.5f);
		ASSERT(IsTrue, NearlyEqual(weights1.x, 0.99f));
		ASSERT(AreEqual, weights1.y, 0.0f);
		ASSERT(IsTrue, NearlyEqual(weights1.z, enteringWeight));
		ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
	}
}

	/// @tags mesh-blending, landscape, threshold, conservation
	[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeBarelyActiveHardLayerRemainsSampled()
{
	uint descriptor = PackLandscapeClass(0u, 0u, MeshBlending::LandscapeClass::Hard);
	descriptor = PackLandscapeClass(descriptor, 1u, MeshBlending::LandscapeClass::Soft);
	const float barelyActive = asfloat(asuint(MeshBlending::kLandscapeActiveWeightThreshold) + 1u);
	float4 weights1 = float4(barelyActive, 1.0f - barelyActive, 0.0f, 0.0f);
	float2 weights2 = 0.0f;
	MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, 1.0f);
	ASSERT(IsTrue, MeshBlending::IsLandscapeLayerActive(weights1.x));
	ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
}

/// @tags mesh-blending, landscape, strength, conservation, inactive
[numthreads(1, 1, 1)] void TestMeshBlendingLandscapeConservesSixLayerSampledMass() {
	uint descriptor = 0u;
	for (uint layer = 0u; layer < 6u; ++layer) {
		descriptor = PackLandscapeClass(descriptor, layer,
			(layer & 1u) != 0u ? MeshBlending::LandscapeClass::Soft : MeshBlending::LandscapeClass::Hard);
	}
	for (uint strengthStep = 1u; strengthStep <= 4u; ++strengthStep) {
		float4 weights1 = float4(0.001f, 0.34f, 0.18f, 0.17f);
		float2 weights2 = float2(0.289f, 0.02f);
		MeshBlending::RemapLandscapeWeights(weights1, weights2, descriptor, float(strengthStep) * 0.25f);
		ASSERT(AreEqual, weights1.x, 0.001f);
		ASSERT(IsTrue, NearlyEqual(LandscapeWeightTotal(weights1, weights2), 1.0f));
		float weights[MeshBlending::kLandscapeLayerCount];
		MeshBlending::LoadLandscapeWeights(weights1, weights2, weights);
		float sampledMass = 0.0f;
		for (uint layer = 1u; layer < 6u; ++layer) {
			ASSERT(IsTrue, weights[layer] == 0.0f || MeshBlending::IsLandscapeLayerActive(weights[layer]));
			if (MeshBlending::IsLandscapeLayerActive(weights[layer])) {
				sampledMass += weights[layer];
			}
		}
		ASSERT(IsTrue, NearlyEqual(sampledMass, 0.999f));
	}
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
[numthreads(1, 1, 1)] void TestMeshBlendingProjectedSnowCoverage() {
	ASSERT(AreEqual, MeshBlending::ComputeProjectedSnowCoverage(-0.1f, 0.1f, 2.0f), 0.0f);
	ASSERT(AreEqual, MeshBlending::ComputeProjectedSnowCoverage(0.0f, 0.1f, 2.0f), 0.5f);
	ASSERT(AreEqual, MeshBlending::ComputeProjectedSnowCoverage(0.1f, 0.1f, 2.0f), 1.0f);
	const float widerCoverage = MeshBlending::ComputeProjectedSnowCoverage(0.1f, 0.1f, 4.0f);
	ASSERT(IsTrue, widerCoverage > 0.5f);
	ASSERT(IsTrue, widerCoverage < 1.0f);
}

	/// @tags mesh-blending, descriptor-isolation
	[numthreads(1, 1, 1)] void TestMeshBlendingDescriptorIsolation()
{
	ASSERT(AreEqual, MeshBlending::kEnableMask, 0x00800000u);
	ASSERT(AreEqual, MeshBlending::kEnableMask & 0x007FFFFFu, 0u);
	ASSERT(AreEqual, MeshBlending::kLandscapeClassMask & 0x000007FFu, 0u);
	ASSERT(IsFalse, MeshBlending::ShouldApplyDescriptor(1u | (1u << 9), 1u, 2u));
	ASSERT(IsFalse, MeshBlending::ShouldApplyDescriptor(MeshBlending::kEnableMask, 1u, 2u));
	ASSERT(IsTrue, MeshBlending::ShouldApplyDescriptor(1u | MeshBlending::kEnableMask | 0x007FFFFCu, 1u, 2u));
	ASSERT(IsFalse, MeshBlending::ShouldApplyDescriptor(3u | MeshBlending::kEnableMask, 1u, 2u));
	ASSERT(AreEqual, (0x007FFFFFu | MeshBlending::kEnableMask) & ~MeshBlending::kEnableMask, 0x007FFFFFu);
}
