#if !(defined(__VOLUMETRIC_LIGHTING_CLARITY_HLSLI__))
#define __VOLUMETRIC_LIGHTING_CLARITY_HLSLI__

#	include "Common/DepthOrder.hlsli"
#	include "Common/VolumetricLighting.hlsli"

namespace VolumetricLighting
{
	static const float kHazeRadiusInViewHeight = 0.04;
	static const float kHazeRelativeDepthTolerance = 0.05;

	struct SamplingRegion
	{
		float2 minimumUV;
		float2 maximumUV;
		float2 hazeRadius;
		float2 activeScale;
	};

	/** Keep the center and neighborhood samples inside the same active texel region. */
	SamplingRegion GetSamplingRegion(Texture2D<float4> source, float2 resolutionScale)
	{
		uint width, height;
		source.GetDimensions(width, height);
		uint2 dimensions = uint2(width, height);
		uint2 active = clamp(uint2(float2(dimensions) * resolutionScale), uint2(1, 1), dimensions);
		float2 activeScale = float2(active) / dimensions;
		SamplingRegion region;
		region.minimumUV = (0.5 / float2(active)) * activeScale;
		region.maximumUV = ((float2(active) - 0.5) / active) * activeScale;
		region.hazeRadius = (kHazeRadiusInViewHeight * active.y) / float2(dimensions);
		region.activeScale = activeScale;
		return region;
	}

	/** Reject invalid depth instead of manufacturing shaft contrast from missing geometry. */
	float ReadLinearDepth(Texture2D<float4> depthSource, float2 uv, float4 cameraData)
	{
		uint width, height;
		depthSource.GetDimensions(width, height);
		if (width == 0 || height == 0)
			return 0.0;
		float depth = depthSource.Load(int3(clamp(int2(uv * float2(width, height)), 0, int2(width, height) - 1), 0)).x;
		if (!IsFinite(depth) || depth < 0.0 || depth > 1.0 || abs(depth - DepthOrder::Near()) <= 1e-6)
			return 0.0;
		float denominator = DepthOrder::IsReversed() ? cameraData.y + depth * cameraData.z : cameraData.x - depth * cameraData.z;
		float linearDepth = cameraData.w / max(denominator, 1e-6);
		return IsFinite(linearDepth) && denominator > 0.0 ? max(linearDepth, 0.0) : 0.0;
	}

	/** Every bilinear contributor must belong to the center surface, including at thin silhouettes. */
	bool IsSameSurface(Texture2D<float4> source, Texture2D<float4> depthSource, float2 sampleUV,
		SamplingRegion region, SamplingRegion depthRegion, float centerDepth, float4 cameraData)
	{
		uint width, height;
		source.GetDimensions(width, height);
		float2 dimensions = float2(width, height);
		float2 base = floor(sampleUV * dimensions - 0.5);
		[unroll] for (uint y = 0; y < 2; ++y)
			[unroll] for (uint x = 0; x < 2; ++x)
		{
			float2 tapUV = clamp((base + float2(x, y) + 0.5) / dimensions, region.minimumUV, region.maximumUV);
			float2 depthUV = clamp(tapUV / region.activeScale * depthRegion.activeScale, depthRegion.minimumUV, depthRegion.maximumUV);
			float tapDepth = ReadLinearDepth(depthSource, depthUV, cameraData);
			if (tapDepth <= 0.0 || abs(tapDepth - centerDepth) > kHazeRelativeDepthTolerance * min(tapDepth, centerDepth))
				return false;
		}
		return true;
	}

	/** Split local shaft contrast from haze without allowing foreground silhouettes to lower the haze floor. */
	bool SeparateLighting(Texture2D<float4> source, Texture2D<float4> depthSource, SamplerState sourceSampler,
		float2 uv, float2 viewUV, SamplingRegion region, float2 resolutionScale,
		float rawValue, float processedValue, float4 cameraData, out float2 hazeAndShafts)
	{
		hazeAndShafts = float2(processedValue, 0.0);
		uint depthWidth, depthHeight;
		depthSource.GetDimensions(depthWidth, depthHeight);
		if (depthWidth == 0 || depthHeight == 0 || !IsFinite(cameraData.w) || cameraData.w <= 0.0)
			return false;
		SamplingRegion depthRegion = GetSamplingRegion(depthSource, resolutionScale);
		float centerDepth = ReadLinearDepth(depthSource,
			clamp(viewUV * depthRegion.activeScale, depthRegion.minimumUV, depthRegion.maximumUV), cameraData);
		if (centerDepth <= 0.0)
			return false;
		if (!IsSameSurface(source, depthSource, uv, region, depthRegion, centerDepth, cameraData))
			return true;

		static const float2 directions[4] = {
			float2(1, 0), float2(0, 1), float2(0.707107, 0.707107), float2(-0.707107, 0.707107)
		};
		float haze = IsFinite(rawValue) ? max(rawValue, 0.0) : 0.0;
		[unroll] for (uint i = 0; i < 4; ++i)
		{
			float2 offset = directions[i] * region.hazeRadius;
			float2 firstUV = clamp(uv - offset, region.minimumUV, region.maximumUV);
			float2 secondUV = clamp(uv + offset, region.minimumUV, region.maximumUV);
			// Requiring support on both sides avoids turning a smooth ramp or one-sided edge into a beam.
			if (IsSameSurface(source, depthSource, firstUV, region, depthRegion, centerDepth, cameraData) &&
				IsSameSurface(source, depthSource, secondUV, region, depthRegion, centerDepth, cameraData)) {
				float first = source.SampleLevel(sourceSampler, firstUV, 0).x;
				float second = source.SampleLevel(sourceSampler, secondUV, 0).x;
				if (IsFinite(first) && IsFinite(second))
					haze = min(haze, max(max(first, second), 0.0));
			}
		}
		float hazePower = min(processedValue, Color::VolumetricLighting(haze.xxx).x);
		hazeAndShafts = float2(hazePower, max(processedValue - hazePower, 0.0));
		return true;
	}
}

#endif
