// HLSL unit tests for Mesh Blending's NIF depth-fade policy.
#define MESH_BLENDING_MATH_ONLY
#include "/Shaders/MeshBlending/MeshBlending.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

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

/// @tags mesh-blending, descriptor-isolation
[numthreads(1, 1, 1)] void TestMeshBlendingDescriptorIsolation() {
	ASSERT(AreEqual, MeshBlending::kEnableMask, 0x00800000u);
	ASSERT(AreEqual, MeshBlending::kEnableMask & 0x007FFFFFu, 0u);
	ASSERT(IsFalse, MeshBlending::ShouldApplyDescriptor(1u | (1u << 9), 1u, 2u));
	ASSERT(IsFalse, MeshBlending::ShouldApplyDescriptor(MeshBlending::kEnableMask, 1u, 2u));
	ASSERT(IsTrue, MeshBlending::ShouldApplyDescriptor(1u | MeshBlending::kEnableMask | 0x007FFFFCu, 1u, 2u));
	ASSERT(IsFalse, MeshBlending::ShouldApplyDescriptor(3u | MeshBlending::kEnableMask, 1u, 2u));
	ASSERT(AreEqual, (0x007FFFFFu | MeshBlending::kEnableMask) & ~MeshBlending::kEnableMask, 0x007FFFFFu);
}
