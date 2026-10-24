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
	static const float kLandscapeActiveWeightThreshold = 0.01f;
	static const uint kLandscapeLayerCount = 6u;
	static const uint kLandscapeClassShift = 11u;
	static const uint kLandscapeClassBits = 2u;
	static const uint kLandscapeClassMask = 0x007FF800u;

	namespace LandscapeClass
	{
		static const uint Unknown = 0u;
		static const uint Hard = 1u;
		static const uint Soft = 2u;
		// Value 3 is deliberately reserved. Treat it like Unknown so a corrupt or
		// newer classification cannot alter an authored landscape blend.
		static const uint Reserved = 3u;
	}

	uint GetLandscapeClass(uint descriptor, uint layerIndex)
	{
		return (descriptor >> (kLandscapeClassShift + layerIndex * kLandscapeClassBits)) & 0x3u;
	}

	bool IsLandscapeLayerActive(float weight)
	{
		// Match Lighting's six texture-sampling predicates exactly. In particular,
		// a weight of exactly 0.01 is inactive and must never be promoted.
		return weight > kLandscapeActiveWeightThreshold;
	}

	void LoadLandscapeWeights(float4 weights1, float2 weights2, out float weights[kLandscapeLayerCount])
	{
		weights[0] = weights1.x;
		weights[1] = weights1.y;
		weights[2] = weights1.z;
		weights[3] = weights1.w;
		weights[4] = weights2.x;
		weights[5] = weights2.y;
	}

	void StoreLandscapeWeights(float weights[kLandscapeLayerCount], out float4 weights1, out float2 weights2)
	{
		weights1 = float4(weights[0], weights[1], weights[2], weights[3]);
		weights2 = float2(weights[4], weights[5]);
	}

	// Rebalances only sampled layers before POM and height blending. Unknown or
	// reserved active classes leave the whole pixel's authored blend unchanged.
	bool RemapLandscapeWeights(
		inout float4 weights1,
		inout float2 weights2,
		uint descriptor,
		float blendStrength)
	{
		const float strength = saturate(blendStrength);
		if (strength <= 0.0f || (descriptor & kLandscapeClassMask) == 0u) {
			return false;
		}

		float original[kLandscapeLayerCount];
		float remapped[kLandscapeLayerCount];
		bool active[kLandscapeLayerCount];
		uint classes[kLandscapeLayerCount];
		LoadLandscapeWeights(weights1, weights2, original);

		uint activeCount = 0u;
		float softSupport = 0.0f;
		float hardSupport = 0.0f;
		float hardTotal = 0.0f;
		uint dominantHardLayer = 0u;
		float dominantHardWeight = -1.0f;

		[unroll] for (uint i = 0u; i < kLandscapeLayerCount; ++i)
		{
			active[i] = IsLandscapeLayerActive(original[i]);
			classes[i] = GetLandscapeClass(descriptor, i);
			remapped[i] = original[i];

			if (!active[i]) {
				continue;
			}

			// All active layers must have a class understood by this shader.
			if (classes[i] != LandscapeClass::Hard && classes[i] != LandscapeClass::Soft) {
				return false;
			}

			++activeCount;
			if (classes[i] == LandscapeClass::Soft) {
				softSupport += original[i] - kLandscapeActiveWeightThreshold;
			} else {
				hardSupport += original[i] - kLandscapeActiveWeightThreshold;
				hardTotal += original[i];
				if (original[i] > dominantHardWeight) {
					dominantHardWeight = original[i];
					dominantHardLayer = i;
				}
			}
		}

		// Soft-only blends retain their authored ratios, including at activation.
		if (activeCount < 2u || hardTotal <= 0.0f) {
			return false;
		}

		// Each layer's support starts at zero at the sampling threshold, so a new
		// soft layer cannot reset the existing mixture or claim an equal share.
		float transfer = 0.0f;
		if (softSupport > 0.0f) {
			transfer = strength * softSupport * hardSupport / (softSupport + hardSupport);
		}
		// Keep the remaining receiver sampled even if subtraction rounds to 0.01.
		const float minimumActiveWeight = asfloat(asuint(kLandscapeActiveWeightThreshold) + 1u);
		const float targetHardTotal = max(hardTotal - transfer, minimumActiveWeight);
		transfer = hardTotal - targetHardTotal;
		const float hardScale = targetHardTotal / hardTotal;

		float survivingHardTotal = 0.0f;
		[unroll] for (uint remapLayer = 0u; remapLayer < kLandscapeLayerCount; ++remapLayer)
		{
			if (!active[remapLayer]) {
				continue;
			}

			if (classes[remapLayer] == LandscapeClass::Soft) {
				remapped[remapLayer] += transfer * (original[remapLayer] - kLandscapeActiveWeightThreshold) / softSupport;
				continue;
			}

			// Hard sharpening is independent of soft participation. Exact ties
			// choose the lowest layer index, keeping the intentional crisp edge.
			const float targetWeight = remapLayer == dominantHardLayer ? hardTotal : 0.0f;
			remapped[remapLayer] = lerp(original[remapLayer], targetWeight, strength) * hardScale;
			// Return pruned mass only to hard layers; otherwise an entering soft
			// layer would abruptly inherit mass from existing hard pruning.
			if (!IsLandscapeLayerActive(remapped[remapLayer])) {
				remapped[remapLayer] = 0.0f;
			} else {
				survivingHardTotal += remapped[remapLayer];
			}
		}

		if (survivingHardTotal <= 0.0f) {
			remapped[dominantHardLayer] = targetHardTotal;
			survivingHardTotal = targetHardTotal;
		}

		// Threshold removal may reduce the sampled mass. Scale only upward: a
		// rounding overshoot must not push a surviving layer below the threshold.
		const float survivorScale = survivingHardTotal < targetHardTotal ? targetHardTotal / survivingHardTotal : 1.0f;
		bool changed = false;
		[unroll] for (uint normalizeLayer = 0u; normalizeLayer < kLandscapeLayerCount; ++normalizeLayer)
		{
			if (active[normalizeLayer] && classes[normalizeLayer] == LandscapeClass::Hard && remapped[normalizeLayer] > 0.0f) {
				remapped[normalizeLayer] *= survivorScale;
			}
			if (active[normalizeLayer]) {
				changed = changed || remapped[normalizeLayer] != original[normalizeLayer];
			}
		}

		if (!changed) {
			return false;
		}

		StoreLandscapeWeights(remapped, weights1, weights2);
		return true;
	}

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
