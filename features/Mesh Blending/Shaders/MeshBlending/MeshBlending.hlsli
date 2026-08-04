#ifndef MESH_BLENDING_HLSLI
#define MESH_BLENDING_HLSLI

namespace MeshBlending
{
	static const float kMinimumBlendWidth = 1.0e-4f;
	static const float kMinimumNegativeGapTolerance = 1.0e-4f;
	static const float kInvalidDepthEpsilon = 1.0e-6f;

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
		const uint requiredFlags = Permutation::ExtraFlags::InWorld | Permutation::ExtraFlags::MeshBlending;
		const uint relevantFlags = requiredFlags | Permutation::ExtraFlags::InReflection;
		const uint descriptor = Permutation::ExtraShaderDescriptor;
		return (descriptor & relevantFlags) == requiredFlags;
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
