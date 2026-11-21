// HLSL Unit Tests for Common/LightingCommon.hlsli
#include "/Shaders/Common/LightingCommon.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags lighting, tangent-basis
[numthreads(1, 1, 1)] void TestReconstructedBasisOrientation() {
	float3 normal = float3(0, 0, 1);
	for (int mirrored = 0; mirrored < 2; ++mirrored) {
		float sign = mirrored ? -1.0 : 1.0;
		float3x3 basis = ReconstructTBN(float3(2, 0, 1), float3(1, 3, -2), normal, float2(sign, 0), float2(0, 1));
		ASSERT(IsTrue, all(abs(basis[0] - float3(1, 0, 0)) < 1e-5));
		ASSERT(IsTrue, all(abs(basis[1] - normalize(float3(1, 3, 0)) * sign) < 1e-5));
		ASSERT(IsTrue, all(abs(basis[2] - normal) < 1e-5));
	}
	float3x3 small = ReconstructTBN(float3(0, 1e-4, 0), float3(-1e-4, 0, 0), normal, float2(1e-4, 0), float2(0, 1e-4));
	ASSERT(IsTrue, all(abs(small[0] - float3(0, 1, 0)) < 1e-5));
	ASSERT(IsTrue, all(abs(small[1] - float3(-1, 0, 0)) < 1e-5));
}

	/// @tags lighting, tangent-basis, robustness
	[numthreads(1, 1, 1)] void TestReconstructedBasisFallback()
{
	float infinity = asfloat(0x7f800000u);
	float nan = asfloat(0x7fc00000u);
	float3 badVectors[] = { float3(0, 0, 0), float3(0, 0, 1), float3(0, 1, 0), float3(infinity, 0, 0), float3(nan, 0, 0), float3(1e30, 0, 0) };
	for (int i = 0; i < 6; ++i) {
		float3x3 basis = ReconstructTBN(badVectors[i], float3(0, 1, 0), float3(0, 0, 1), float2(1, 0), float2(0, 1));
		ASSERT(IsTrue, all(isfinite(basis[0])) && all(isfinite(basis[1])) && all(isfinite(basis[2])));
		ASSERT(IsTrue, abs(dot(cross(basis[0], basis[1]), basis[2]) - 1) < 1e-5);
		ASSERT(IsTrue, abs(dot(basis[0], basis[0]) - 1) < 1e-5);
		ASSERT(IsTrue, abs(dot(basis[1], basis[1]) - 1) < 1e-5);

		basis = ReconstructTBN(float3(1, 0, 0), float3(0, 1, 0), badVectors[i], float2(0, 0), float2(0, 0));
		ASSERT(IsTrue, all(isfinite(basis[0])) && all(isfinite(basis[1])) && all(isfinite(basis[2])));
		ASSERT(IsTrue, abs(dot(cross(basis[0], basis[1]), basis[2]) - 1) < 1e-5);
	}
}

/// @tags lighting, foliage, transmission
[numthreads(1, 1, 1)] void TestFoliageTransmission() {
	float frontLit = GetFoliageTransmission(1.0f, -1.0f);
	float grazingBacklight = GetFoliageTransmission(0.0f, -1.0f);
	float backLit = GetFoliageTransmission(-1.0f, -1.0f);

	ASSERT(IsTrue, abs(frontLit) < 0.0001f);
	ASSERT(IsTrue, grazingBacklight > frontLit);
	ASSERT(IsTrue, backLit > grazingBacklight);

	float forwardAligned = GetFoliageTransmission(-1.0f, -1.0f);
	float perpendicular = GetFoliageTransmission(-1.0f, 0.0f);
	float sameDirection = GetFoliageTransmission(-1.0f, 1.0f);

	ASSERT(IsTrue, forwardAligned > perpendicular);
	ASSERT(IsTrue, abs(perpendicular - sameDirection) < 0.0001f);
	ASSERT(IsTrue, forwardAligned >= 0.0f && forwardAligned <= 1.0f);
	ASSERT(IsTrue, !isnan(forwardAligned) && !isinf(forwardAligned));
}

	/// @tags lighting, material
	[numthreads(1, 1, 1)] void TestShininessToRoughness()
{
	// Test 1: Known conversions
	// Formula: roughness = (2/(shininess+2))^0.25
	// Shininess = 2: roughness = (2/4)^0.25 = 0.5^0.25 ≈ 0.841
	float roughness_low_shininess = ShininessToRoughness(2.0f);
	ASSERT(IsTrue, abs(roughness_low_shininess - 0.841f) < 0.01f);

	// Test 2: Higher shininess = lower roughness
	float shininess_low = 10.0f;
	float shininess_high = 100.0f;

	float roughness_low = ShininessToRoughness(shininess_low);
	float roughness_high = ShininessToRoughness(shininess_high);

	ASSERT(IsTrue, roughness_low > roughness_high);

	// Test 3: Result should be in valid range [0, 1]
	float testShininess[5] = { 2.0f, 10.0f, 50.0f, 200.0f, 1000.0f };

	for (int i = 0; i < 5; i++) {
		float r = ShininessToRoughness(testShininess[i]);
		ASSERT(IsTrue, r >= 0.0f);
		ASSERT(IsTrue, r <= 1.0f);
	}

	// Test 4: Very high shininess (mirror-like) should give low roughness
	// shininess = 10000: roughness = (2/10002)^0.25 ≈ 0.376
	float roughness_mirror = ShininessToRoughness(10000.0f);
	ASSERT(IsTrue, roughness_mirror < 0.4f);

	// Test 5: Monotonicity - increasing shininess should decrease roughness
	float r1 = ShininessToRoughness(10.0f);
	float r2 = ShininessToRoughness(20.0f);
	float r3 = ShininessToRoughness(40.0f);

	ASSERT(IsTrue, r1 > r2);
	ASSERT(IsTrue, r2 > r3);

	// Test 6: Formula verification - roughness = (2/(shininess+2))^0.25
	float shininess = 50.0f;
	float expected = pow(2.0f / (shininess + 2.0f), 0.25f);
	float actual = ShininessToRoughness(shininess);
	ASSERT(IsTrue, abs(actual - expected) < 0.0001f);
}

/// @tags lighting, material, edge-cases
[numthreads(1, 1, 1)] void TestShininessToRoughnessEdgeCases() {
	// Test near-zero shininess
	float roughness_zero = ShininessToRoughness(0.0f);
	ASSERT(IsTrue, !isnan(roughness_zero));
	ASSERT(IsTrue, !isinf(roughness_zero));
	ASSERT(IsTrue, roughness_zero >= 0.0f && roughness_zero <= 1.0f);

	// Zero shininess: (2/2)^0.25 = 1.0
	ASSERT(IsTrue, abs(roughness_zero - 1.0f) < 0.01f);

	// Test very small shininess
	float roughness_small = ShininessToRoughness(0.1f);
	ASSERT(IsTrue, !isnan(roughness_small));
	ASSERT(IsTrue, roughness_small > 0.9f);  // Should be very rough

	// Test very large shininess
	float roughness_large = ShininessToRoughness(100000.0f);
	ASSERT(IsTrue, !isnan(roughness_large));
	ASSERT(IsTrue, roughness_large < 0.4f);  // Should be very smooth

	// Test negative shininess (using abs in formula)
	float roughness_neg = ShininessToRoughness(-10.0f);
	ASSERT(IsTrue, !isnan(roughness_neg));
	ASSERT(IsTrue, !isinf(roughness_neg));
}

	/// @tags lighting, material, properties
	[numthreads(1, 1, 1)] void TestShininessToRoughnessProperties()
{
	// Property: Continuous and smooth
	float prev = ShininessToRoughness(1.0f);

	for (float s = 2.0f; s < 100.0f; s += 10.0f) {
		float curr = ShininessToRoughness(s);

		// Should be monotonically decreasing
		ASSERT(IsTrue, curr < prev);

		// Should not have huge jumps (smoothness)
		float diff = abs(curr - prev);
		ASSERT(IsTrue, diff < 0.5f);

		prev = curr;
	}

	// Property: Bounded output
	for (float s = 0.1f; s < 1000.0f; s *= 2.0f) {
		float r = ShininessToRoughness(s);
		ASSERT(IsTrue, r >= 0.0f);
		ASSERT(IsTrue, r <= 1.0f);
	}
}
