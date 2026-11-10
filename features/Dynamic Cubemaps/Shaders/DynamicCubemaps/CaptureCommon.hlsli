#ifndef DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI
#define DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI

#include "Common/ReverseZ.hlsli"

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

RWTexture2DArray<float4> DynamicCubemap : register(u0);
RWTexture2DArray<float4> DynamicCubemapRaw : register(u1);
RWTexture2DArray<float4> DynamicCubemapPosition : register(u2);

struct CaptureLightingState
{
	float ReferenceLuminance;
	uint PendingResetMask;
	uint Reset;
	uint Initialized;
};

RWStructuredBuffer<CaptureLightingState> LightingState : register(u3);
Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);
SamplerState LinearSampler : register(s0);

cbuffer UpdateData : register(b0)
{
	float3 CameraPosAdjustDelta;
	uint CaptureFlags;
	uint CaptureIndex;
	float CaptureDeltaTime;
	uint ResetCapture;
	uint CapturePadding;
}

static const float CaptureHistoryLifetime = 30.0;

float GetCaptureTime()
{
	// Sixteenth-second ticks remain exact in the half-float position texture.
	return floor(fmod(SharedData::Timer, 64.0) * 16.0) / 16.0;
}

bool CaptureHistoryExpired(float lastSeen)
{
	return CaptureDeltaTime >= CaptureHistoryLifetime || fmod(GetCaptureTime() - lastSeen + 64.0, 64.0) >= CaptureHistoryLifetime;
}

float3 AdjustCapturePosition(float3 position)
{
	return position + CameraPosAdjustDelta * 0.001;
}

bool CaptureGeometryMatches(float3 previousPosition, float3 position)
{
	return length(previousPosition - position) <= max(0.032, length(position) * 0.03);
}

float3 GetSamplingVector(uint3 texel)
{
	uint width, height, faces;
	DynamicCubemap.GetDimensions(width, height, faces);
	float2 st = (texel.xy + 0.5) / float2(width, height);
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	float3 direction = 0.0;
	switch (texel.z) {
	case 0:
		direction = float3(1.0, uv.y, -uv.x);
		break;
	case 1:
		direction = float3(-1.0, uv.y, uv.x);
		break;
	case 2:
		direction = float3(uv.x, 1.0, -uv.y);
		break;
	case 3:
		direction = float3(uv.x, -1.0, uv.y);
		break;
	case 4:
		direction = float3(uv.x, uv.y, 1.0);
		break;
	case 5:
		direction = float3(-uv.x, uv.y, -1.0);
		break;
	}
	return normalize(direction);
}

float GetFrameEdgeMask(float2 uv)
{
	float2 edgeDistance = min(uv, 1.0 - uv);
	return smoothstep(0.015, 0.14, min(edgeDistance.x, edgeDistance.y));
}

/** Requires a geometry-only depth footprint inside the selected eye's active image. */
bool IsCaptureDepthFootprintValid(float2 sampleUV, uint eyeIndex)
{
	uint width, height;
	DepthTexture.GetDimensions(width, height);
	float2 dimensions = float2(width, height);
	float2 minUV = Stereo::ConvertToStereoUV(float2(0.0, 0.0), eyeIndex);
	float2 maxUV = Stereo::ConvertToStereoUV(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(float2(1.0, 1.0), 0), eyeIndex);
	if (any(dimensions == 0.0) || !all(isfinite(sampleUV)) || !all(isfinite(maxUV)))
		return false;

	maxUV = min(maxUV, Stereo::ConvertToStereoUV(float2(1.0, 1.0), eyeIndex));
	float2 minPixel = ceil(minUV * dimensions);
	float2 maxPixel = floor(maxUV * dimensions) - 1.0;
	float2 samplePixel = sampleUV * dimensions;
	// A full-texel inset keeps the gather inside the active eye even with sampler coordinate rounding.
	if (any(samplePixel < minPixel + 1.0) || any(samplePixel > maxPixel))
		return false;

	float4 depths = DepthTexture.GatherRed(LinearSampler, sampleUV);
	return all(isfinite(depths)) && all(depths >= 0.0) && all(depths <= 1.0) && (DepthOrder::Reversed ? all(depths > 0.0) : all(depths < 1.0));
}

bool SampleCaptureEye(float3 direction, uint eyeIndex, out float3 position, out float3 color, out float2 uv, out float weight)
{
	position = 0.0;
	color = 0.0;
	weight = 0.0;
	float3 viewDirection = FrameBuffer::WorldToView(direction, false, eyeIndex);
	uv = FrameBuffer::ViewToUV(viewDirection, false, eyeIndex);
	if (!all(isfinite(uv)) || FrameBuffer::IsOutsideFrame(uv))
		return false;

	float forwardMask = 1.0 - smoothstep(-0.12, 0.55, viewDirection.z);
	forwardMask = smoothstep(0.0, 1.0, sqrt(saturate(forwardMask)));
	if ((CaptureFlags & 0x1u) != 0)
		forwardMask = 1.0;
	bool suppressSkyAndFrameEdge = (CaptureFlags & 0x2u) != 0;
	float edgeMask = suppressSkyAndFrameEdge ? GetFrameEdgeMask(uv) : 1.0;
	if (forwardMask <= 1e-3 || edgeMask <= 1e-3)
		return false;

	float2 sampleUV = Stereo::ConvertToStereoUV(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv, 0), eyeIndex);
#if defined(REFLECTIONS)
	bool excludeSky = suppressSkyAndFrameEdge;
#else
	bool excludeSky = true;
#endif
	[branch] if (excludeSky)
	{
		if (!IsCaptureDepthFootprintValid(sampleUV, eyeIndex))
			return false;
	}
	float depth = DepthTexture.SampleLevel(LinearSampler, sampleUV, 0);
	float nearValidity = smoothstep(0.75, 2.75, SharedData::GetScreenDepth(depth));
	if (!isfinite(depth) || nearValidity <= 1e-3)
		return false;
	float nearWeight = lerp(0.88, 1.0, nearValidity);
#if defined(REFLECTIONS)
	float skyMask = suppressSkyAndFrameEdge ? (1.0 - smoothstep(0.9988, 0.99997, FrameBuffer::ToStandardDepth(depth))) : 1.0;
#else
	float skyMask = 1.0 - smoothstep(0.9992, 0.99995, FrameBuffer::ToStandardDepth(depth));
#endif
	weight = forwardMask * nearWeight * edgeMask * skyMask;
	if (weight <= 1e-3)
		return false;

	// Reconstruct with unscaled eye UVs; only the texture lookup uses render scale.
	float4 positionCS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
	position = positionCS.xyz / positionCS.w * 0.001;
	color = ColorTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
	return all(isfinite(position)) && all(isfinite(color));
}

bool SampleCapture(uint3 texel, out float3 position, out float3 color, out float2 uv)
{
	position = 0.0;
	color = 0.0;
	uv = 0.0;
	float totalWeight = 0.0;
	float3 direction = -GetSamplingVector(texel);
#if defined(VR)
	const uint eyeCount = 2;
#else
	const uint eyeCount = 1;
#endif
	[unroll] for (uint eye = 0; eye < eyeCount; eye++)
	{
		float3 eyePosition, eyeColor;
		float2 eyeUV;
		float weight;
		if (!SampleCaptureEye(direction, eye, eyePosition, eyeColor, eyeUV, weight))
			continue;
		position += eyePosition * weight;
		color += eyeColor * weight;
		uv += eyeUV * weight;
		totalWeight += weight;
	}
	if (totalWeight <= 0.0)
		return false;
	position /= totalWeight;
	uv /= totalWeight;
	color = Color::IrradianceToLinear(color / totalWeight);
	if (!all(isfinite(color)) || !all(isfinite(position)) || any(abs(position) > 65504.0))
		return false;
	color = clamp(color, 0.0, 65504.0);
	return true;
}

#endif
