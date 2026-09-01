#ifndef MESH_BLENDING_HLSLI
#define MESH_BLENDING_HLSLI

namespace MeshBlending
{
	static const float kMinimumBlendWidth = 1.0e-4f;
	static const float kMinimumNegativeGapTolerance = 1.0e-4f;
	static const float kInvalidDepthEpsilon = 1.0e-6f;
	static const float kLandscapeActiveWeightThreshold = 0.01f;
	static const uint kLandscapeLayerCount = 6u;
	static const uint kLandscapeClassShift = 10u;
	static const uint kLandscapeClassBits = 2u;
	static const uint kLandscapeClassMask = 0x003FFC00u;

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

	// Rebalances only layers that Lighting was already going to sample. The
	// descriptor stores one two-bit material class for each of the six LAND
	// textures. Unknown or reserved active classes fail open for the whole pixel;
	// partial classification would otherwise move authored weight unpredictably.
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
		uint softCount = 0u;
		uint hardCount = 0u;
		float activeTotal = 0.0f;
		float softTotal = 0.0f;
		float hardTotal = 0.0f;
		uint dominantHardLayer = 0u;
		float dominantHardWeight = -1.0f;

		[unroll]
		for (uint i = 0u; i < kLandscapeLayerCount; ++i) {
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
			activeTotal += original[i];
			if (classes[i] == LandscapeClass::Soft) {
				++softCount;
				softTotal += original[i];
			} else {
				++hardCount;
				hardTotal += original[i];
				if (original[i] > dominantHardWeight) {
					dominantHardWeight = original[i];
					dominantHardLayer = i;
				}
			}
		}

		if (activeCount < 2u || activeTotal <= 0.0f) {
			return false;
		}

		uint policy = 2u;  // hard/hard
		float targetSoftWeight = 0.0f;
		float targetHardTotal = 0.0f;
		if (softCount > 0u && hardCount > 0u) {
			policy = 0u;
			// A soft material deposits over a hard one. Transfer an amount
			// proportional to both aggregates, preserving ratios within each
			// family. A 50/50 boundary becomes 75/25 at strength 1, while a
			// barely present soft material grows gradually instead of jumping.
			const float transfer = softTotal * hardTotal / activeTotal;
			const float targetSoftTotal = softTotal + transfer;
			targetHardTotal = hardTotal - transfer;
			// All already-active soft materials intermix equally, even while the
			// soft aggregate is depositing over a hard receiver.
			targetSoftWeight = targetSoftTotal / float(softCount);
		} else if (softCount > 1u) {
			policy = 1u;
			// Soft materials intermix. Equal shares avoid an arbitrary winner at
			// the authored boundary (two active layers become exactly 50/50).
			targetSoftWeight = activeTotal / float(softCount);
		}

		float survivingTotal = 0.0f;
		[unroll]
		for (uint remapLayer = 0u; remapLayer < kLandscapeLayerCount; ++remapLayer) {
			if (!active[remapLayer]) {
				continue;
			}

			float targetWeight = 0.0f;
			if (policy == 0u) {
				targetWeight = classes[remapLayer] == LandscapeClass::Soft ?
				                   targetSoftWeight :
				                   (remapLayer == dominantHardLayer ? targetHardTotal : 0.0f);
			} else if (policy == 1u) {
				targetWeight = targetSoftWeight;
			} else {
				// Hard materials retain a crisp boundary. Exact ties resolve in
				// favour of the lowest layer index selected above.
				targetWeight = remapLayer == dominantHardLayer ? activeTotal : 0.0f;
			}

			remapped[remapLayer] = lerp(original[remapLayer], targetWeight, strength);
			// Lighting would skip such a layer after the remap. Remove it exactly
			// and return its mass to layers that will still be sampled.
			if (!IsLandscapeLayerActive(remapped[remapLayer])) {
				remapped[remapLayer] = 0.0f;
			} else {
				survivingTotal += remapped[remapLayer];
			}
		}

		if (survivingTotal <= 0.0f) {
			return false;
		}

		// Threshold removal may reduce the sampled mass. Scale only upward: a
		// harmless rounding overshoot must not make the entire material darker.
		const float survivorScale = survivingTotal < activeTotal ? activeTotal / survivingTotal : 1.0f;
		bool changed = false;
		[unroll]
		for (uint normalizeLayer = 0u; normalizeLayer < kLandscapeLayerCount; ++normalizeLayer) {
			if (active[normalizeLayer] && remapped[normalizeLayer] > 0.0f) {
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
