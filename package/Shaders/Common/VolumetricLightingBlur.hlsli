#if !(defined(__VOLUMETRIC_LIGHTING_BLUR_HLSLI__))
#define __VOLUMETRIC_LIGHTING_BLUR_HLSLI__

cbuffer VLData : register(b1)
{
	int2 screenSize;
	int2 screenSizeMin1;
	float4 cameraData;
}

groupshared float vl[TG_DIM];
groupshared float depth[TG_DIM];

namespace VolumetricLightingBlur
{
	static const int TapOffsets[5] = { -12, -6, 0, 6, 12 };
	static const float TapWeights[5] = { 0.178400, 0.210431, 0.222338, 0.210431, 0.178400 };

	float GetLinearDepth(float deviceDepth)
	{
		// Validated near/far parameters keep far-plane cancellation from dividing by zero.
		return cameraData.w / max(cameraData.x - saturate(deviceDepth) * cameraData.z, cameraData.y);
	}

	/** @brief Filter an interior lane after all clamped halo loads have synchronized. */
	float Filter(int idx)
	{
		float centerDepth = depth[idx];
		float rcpCenterDepth = rcp(max(centerDepth, 1e-4));
		float weightedSum = 0.0;
		float weightSum = 0.0;
		[unroll] for (uint i = 0; i < 5; i++)
		{
			int tap = idx + TapOffsets[i];
			float relativeDelta = abs(depth[tap] - centerDepth) * rcpCenterDepth;
			float weight = TapWeights[i] * (1.0 - smoothstep(0.1, 0.3, relativeDelta));
			weightedSum += weight * vl[tap];
			weightSum += weight;
		}

		// The center tap retains its full weight, so the denominator stays positive.
		return weightedSum / weightSum;
	}
}

#endif
