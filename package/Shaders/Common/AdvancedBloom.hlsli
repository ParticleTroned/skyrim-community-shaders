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
	static const float kThreshold = 1.0;
	static const float kSoftKnee = 0.5;

	float3 Prefilter(float3 color)
	{
		color = clamp(color, 0.0, 50.0);

		float luminance = Color::RGBToLuminance(color);
		float soft = luminance - kThreshold + kSoftKnee;
		soft = clamp(soft, 0.0, 2.0 * kSoftKnee);
		soft = soft * soft / max(4.0 * kSoftKnee, 1e-4);

		float contribution = max(soft, luminance - kThreshold);
		return color * (contribution / max(luminance, 1e-4));
	}

	float3 SamplePrefiltered(float2 uv, float2 screenUv)
	{
		float2 clampedUv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, screenUv);
		return Prefilter(BlendTex.SampleLevel(BlendSampler, clampedUv, 0).xyz);
	}

	float3 SampleCompass(float2 uv, float2 screenUv, float2 texelSize, float radius)
	{
		float2 stepSize = texelSize * radius;

		float3 sum = SamplePrefiltered(uv + float2(stepSize.x, 0.0), screenUv);
		sum += SamplePrefiltered(uv - float2(stepSize.x, 0.0), screenUv);
		sum += SamplePrefiltered(uv + float2(0.0, stepSize.y), screenUv);
		sum += SamplePrefiltered(uv - float2(0.0, stepSize.y), screenUv);

		return sum * 0.25;
	}

	float3 SampleDiagonals(float2 uv, float2 screenUv, float2 texelSize, float radius)
	{
		float2 stepSize = texelSize * radius;

		float3 sum = SamplePrefiltered(uv + stepSize, screenUv);
		sum += SamplePrefiltered(uv - stepSize, screenUv);
		sum += SamplePrefiltered(uv + float2(stepSize.x, -stepSize.y), screenUv);
		sum += SamplePrefiltered(uv + float2(-stepSize.x, stepSize.y), screenUv);

		return sum * 0.25;
	}

	float3 Compute(float2 uv, float2 screenUv)
	{
		float2 texelSize = max(SharedData::BufferDim.zw, float2(1.0 / 8192.0, 1.0 / 8192.0));

		float3 center = SamplePrefiltered(uv, screenUv);
		float3 fine = SampleCompass(uv, screenUv, texelSize, 1.5);
		float3 mid = SampleDiagonals(uv, screenUv, texelSize, 5.0);
		float3 wide = SampleCompass(uv, screenUv, texelSize, 16.0);

		return center * 0.28 + fine * 0.34 + mid * 0.24 + wide * 0.14;
	}
}

#endif  // __ADVANCED_BLOOM_DEPENDENCY_HLSL__
