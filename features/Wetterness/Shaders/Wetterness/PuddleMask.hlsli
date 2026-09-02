#pragma once

#include "Common/Random.hlsli"

namespace Wetterness
{
	// Mode values match Wetterness::PuddleMaskMode and the serialized setting.
	static const uint PUDDLE_MASK_SIMPLE = 0u;
	static const uint PUDDLE_MASK_TEXTURED_HIGH_QUALITY = 2u;
	static const uint PUDDLE_MASK_LEGACY_PROCEDURAL = 3u;

	Texture2D<float2> TexPuddleMask : register(t71);

	/** Evaluates the selected noise pattern; Simple bypasses this helper. */
	float GetPuddleNoiseSignal(
		float3 puddleCoordsBase,
		float puddleLayout,
		float layoutT,
		float layoutFrequency,
		uint maskMode,
		SamplerState maskSampler)
	{
		[branch] if (maskMode == PUDDLE_MASK_LEGACY_PROCEDURAL)
		{
			float3 layoutSeed = float3(12.7, 19.1, 23.3) * puddleLayout;
			float layoutWarpStrength = lerp(0.0, 0.38, layoutT);
			float layoutWarp = 0.0;
			if (layoutWarpStrength > 0.0) {
				layoutWarp = Random::perlinNoise(puddleCoordsBase * lerp(0.22, 0.72, layoutT) + layoutSeed) * 2.0 - 1.0;
			}
			float3 puddlePatternOffset = float3(31.0, 17.0, 43.0) * layoutT;
			float3 puddleCoords = puddleCoordsBase * layoutFrequency + layoutWarp * float3(0.20, 0.14, 0.18) * layoutWarpStrength + puddlePatternOffset;
			return Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
		}

		float2 puddleTextureCoords = puddleCoordsBase.xy * layoutFrequency * 0.25 + float2(7.3, 11.9) * layoutT;
		float2 puddleMaskSample = TexPuddleMask.SampleLevel(maskSampler, frac(puddleTextureCoords), 0).rg;
		float puddleNoiseSignal = saturate(puddleMaskSample.r + (puddleMaskSample.g - 0.5) * 0.18);
		[branch] if (maskMode == PUDDLE_MASK_TEXTURED_HIGH_QUALITY)
		{
			float2 puddleTextureUvHighQuality = frac(float2(-puddleTextureCoords.y, puddleTextureCoords.x) * 1.73 + float2(0.37, 0.61));
			float2 puddleMaskHighQuality = TexPuddleMask.SampleLevel(maskSampler, puddleTextureUvHighQuality, 0).rg;
			puddleNoiseSignal = saturate(
				puddleNoiseSignal +
				(puddleMaskHighQuality.r - 0.5) * 0.22 +
				(puddleMaskHighQuality.g - 0.5) * 0.10);
		}
		return puddleNoiseSignal;
	}
}
