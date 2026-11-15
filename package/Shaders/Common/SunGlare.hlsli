#ifndef __SUN_GLARE_DEPENDENCY_HLSL__
#define __SUN_GLARE_DEPENDENCY_HLSL__

#include "Common/Random.hlsli"
#include "Common/ReverseZ.hlsli"
#include "Common/VRStereoEffects.hlsli"

namespace SunGlare
{
	static const uint SampleCount = 16;
	static const float SampleRadius = 0.02;
	Texture2D<float4> SceneDepth : register(t17);

	/** @brief Fade the glare by sky coverage around its billboard centre in the selected eye. */
	float GetVisibility(float4 sunPosition, uint eyeIndex)
	{
		uint width, height;
		SceneDepth.GetDimensions(width, height);
		// An unbound depth view preserves native glare for reflections, menus and unavailable scene depth.
		if (width < 2 || height == 0 || sunPosition.w <= 0.0 || !all(isfinite(sunPosition)))
			return 1.0;

		float2 resolutionScale = FrameBuffer::DynamicResolutionParams1.xy;
		if (!all(isfinite(resolutionScale)) || any(resolutionScale <= 0.0) || any(resolutionScale > 1.0))
			return 1.0;
		bool stereo = false;
#if defined(VR)
		stereo = StereoEnabled != 0.0;
#endif
		uint2 activeSize = uint2(float2(width, height) * resolutionScale);
		if (activeSize.x < (stereo ? 2u : 1u) || activeSize.y == 0)
			return 1.0;

		float2 sunUV = sunPosition.xy / sunPosition.w * float2(0.5, -0.5) + 0.5;
		float visibility = 0.0;
		[unroll] for (uint i = 0; i < SampleCount; ++i)
		{
			float2 sampleUV = sunUV + Random::PoissonSampleOffsets16[i] * SampleRadius;
			if (any(sampleUV <= 0.0) || any(sampleUV >= 1.0)) {
				visibility += 1.0;
			} else {
				float2 depthUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(
					stereo ? Stereo::ConvertToStereoUV(sampleUV, eyeIndex) : sampleUV, stereo);
#if defined(VR)
				if (stereo)
					depthUV = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(
						depthUV, eyeIndex, uint2(width, height), resolutionScale);
#endif
				uint2 pixel = min(uint2(depthUV * float2(width, height)), activeSize - 1);
				float depth = SceneDepth.Load(int3(pixel, 0)).x;
				visibility += !FrameBuffer::IsNearerDepth(depth, FrameBuffer::FarPlaneDepth());
			}
		}
		return smoothstep(0.0, 1.0, visibility / SampleCount);
	}
}

#endif
