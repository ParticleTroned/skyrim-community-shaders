#ifndef MESH_BLENDING_HLSLI
#define MESH_BLENDING_HLSLI

namespace MeshBlending
{
	static const uint kEnableMask = 1u << 23;

	bool ShouldApplyDescriptor(uint descriptor, uint inWorldMask, uint reflectionMask)
	{
		const uint required = inWorldMask | kEnableMask;
		return (descriptor & (required | reflectionMask)) == required;
	}

	static const float kMinimumBlendWidth = 1.0e-4f;
	static const float kMinimumNegativeGapTolerance = 1.0e-4f;
	static const float kInvalidDepthEpsilon = 1.0e-6f;

	// Pure part of the projected-snow edge filter. edgeDerivative is fwidth of
	// the projected material weight; keeping it explicit makes the policy
	// testable and lets Lighting skip derivative work when the setting is off.
	float ComputeProjectedSnowCoverage(float projectedWeight, float edgeDerivative, float edgeWidth)
	{
		const float halfWidth = max(0.5f * abs(edgeDerivative) * max(edgeWidth, 0.0f), kMinimumBlendWidth);
		return smoothstep(-halfWidth, halfWidth, projectedWeight);
	}

	// Computes the opacity multiplier from the signed separation between the
	// opaque receiver and transparent source. Negative separation means that the
	// sampled depth is in front of the source, so clearly negative values fail
	// open instead of fading against unrelated foreground geometry.
	float ComputeFadeFromGap(float gap, float blendWidth, float depthBias, float maximumGap)
	{
		const float safeBlendWidth = max(blendWidth, kMinimumBlendWidth);
		const float safeDepthBias = max(depthBias, 0.0f);
		const float safeMaximumGap = max(maximumGap, safeDepthBias + safeBlendWidth);
		const float negativeGapTolerance = max(safeDepthBias, kMinimumNegativeGapTolerance);

		if (!(gap == gap) || gap < -negativeGapTolerance || gap >= safeMaximumGap) {
			return 1.0f;
		}

		return smoothstep(safeDepthBias, safeDepthBias + safeBlendWidth, max(gap, 0.0f));
	}

	float ApplyBlendStrength(float fade, float blendStrength)
	{
		return lerp(1.0f, saturate(fade), saturate(blendStrength));
	}

#if !defined(MESH_BLENDING_MATH_ONLY)
	bool ShouldApply()
	{
		return ShouldApplyDescriptor(Permutation::ExtraShaderDescriptor,
			Permutation::ExtraFlags::InWorld, Permutation::ExtraFlags::InReflection);
	}

	// The depth fetch is intentionally isolated in this function. Lighting only
	// calls it from the uniform accepted-draw branch, leaving rejected draws with
	// no depth-texture traffic.
	float ComputeFade(float2 screenUV, float fragmentRawDepth, uint eyeIndex)
	{
		const float receiverRawDepth = SharedData::GetDepth(screenUV, eyeIndex);
		if (receiverRawDepth <= kInvalidDepthEpsilon || receiverRawDepth >= 1.0f - kInvalidDepthEpsilon) {
			return 1.0f;
		}

		const float receiverDepth = SharedData::GetScreenDepth(receiverRawDepth);
		const float fragmentDepth = SharedData::GetScreenDepth(fragmentRawDepth);
		const float gap = receiverDepth - fragmentDepth;

		const float fade = ComputeFadeFromGap(
			gap,
			SharedData::meshBlendingSettings.BlendWidth,
			SharedData::meshBlendingSettings.DepthBias,
			SharedData::meshBlendingSettings.MaximumGap);
		return ApplyBlendStrength(fade, SharedData::meshBlendingSettings.BlendStrength);
	}
#endif
}

#endif  // MESH_BLENDING_HLSLI
