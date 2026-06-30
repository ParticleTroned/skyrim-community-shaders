/*
Advanced Bloom is inspired by the bloom pipeline in Microsoft's MiniEngine
from DirectX-Graphics-Samples.
Source: https://github.com/microsoft/DirectX-Graphics-Samples

MIT License

Copyright (c) Microsoft Corporation.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef __ADVANCED_BLOOM_DEPENDENCY_HLSL__
#define __ADVANCED_BLOOM_DEPENDENCY_HLSL__

namespace AdvancedBloom
{
	static const float kThreshold = 0.8;
	static const float kSoftKnee = 0.45;

	float3 Prefilter(float3 color, float exposureScale)
	{
		color = clamp(color * exposureScale, 0.0, 50.0);

		float luminance = Color::RGBToLuminance(color);
		float soft = luminance - kThreshold + kSoftKnee;
		soft = clamp(soft, 0.0, 2.0 * kSoftKnee);
		soft = soft * soft / max(4.0 * kSoftKnee, 1e-4);

		float contribution = max(soft, luminance - kThreshold);
		return color * (contribution / max(luminance, 1e-4));
	}

	float3 SamplePrefiltered(float2 uv, float2 screenUv, float exposureScale)
	{
		float2 clampedUv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, screenUv);
		return Prefilter(BlendTex.SampleLevel(BlendSampler, clampedUv, 0).xyz, exposureScale);
	}

	float3 SampleCompass(float2 uv, float2 screenUv, float2 texelSize, float radius, float exposureScale)
	{
		float2 stepSize = texelSize * radius;

		float3 sum = SamplePrefiltered(uv + float2(stepSize.x, 0.0), screenUv, exposureScale);
		sum += SamplePrefiltered(uv - float2(stepSize.x, 0.0), screenUv, exposureScale);
		sum += SamplePrefiltered(uv + float2(0.0, stepSize.y), screenUv, exposureScale);
		sum += SamplePrefiltered(uv - float2(0.0, stepSize.y), screenUv, exposureScale);

		return sum * 0.25;
	}

	float3 SampleDiagonals(float2 uv, float2 screenUv, float2 texelSize, float radius, float exposureScale)
	{
		float2 stepSize = texelSize * radius;

		float3 sum = SamplePrefiltered(uv + stepSize, screenUv, exposureScale);
		sum += SamplePrefiltered(uv - stepSize, screenUv, exposureScale);
		sum += SamplePrefiltered(uv + float2(stepSize.x, -stepSize.y), screenUv, exposureScale);
		sum += SamplePrefiltered(uv + float2(-stepSize.x, stepSize.y), screenUv, exposureScale);

		return sum * 0.25;
	}

	float3 Compute(float2 uv, float2 screenUv, float exposureScale)
	{
		float2 texelSize = max(SharedData::BufferDim.zw, float2(1.0 / 8192.0, 1.0 / 8192.0));

		float safeExposureScale = max(exposureScale, 0.0);
		float3 center = SamplePrefiltered(uv, screenUv, safeExposureScale);
		float3 fine = SampleCompass(uv, screenUv, texelSize, 2.0, safeExposureScale);
		float3 mid = SampleDiagonals(uv, screenUv, texelSize, 8.0, safeExposureScale);
		float3 wide = SampleCompass(uv, screenUv, texelSize, 20.0, safeExposureScale);
		float3 broad = SampleDiagonals(uv, screenUv, texelSize, 40.0, safeExposureScale);

		return center * 0.16 + fine * 0.28 + mid * 0.26 + wide * 0.18 + broad * 0.12;
	}
}

#endif  // __ADVANCED_BLOOM_DEPENDENCY_HLSL__
