#if !(defined(DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI))
#define DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI

#	include "Common/ReverseZ.hlsli"

#	include "Common/Color.hlsli"
#	include "Common/FrameBuffer.hlsli"
#	include "Common/SharedData.hlsli"

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
	uint CapturePadding0;
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

/** Requires a geometry-only depth footprint inside the active rendered image. */
bool IsCaptureDepthFootprintValid(float2 sampleUV)
{
	uint width, height;
	DepthTexture.GetDimensions(width, height);
	float2 dimensions = float2(width, height);
	float2 minUV = float2(0.0, 0.0);
	float2 maxUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(float2(1.0, 1.0));
	if (any(dimensions == 0.0) || !all(isfinite(sampleUV)) || !all(isfinite(maxUV)))
		return false;

	maxUV = min(maxUV, float2(1.0, 1.0));
	float2 minPixel = ceil(minUV * dimensions);
	float2 maxPixel = floor(maxUV * dimensions) - 1.0;
	float2 samplePixel = sampleUV * dimensions;
	// A full-texel inset keeps the gather inside the active image even with sampler coordinate rounding.
	if (any(samplePixel < minPixel + 1.0) || any(samplePixel > maxPixel))
		return false;

	float4 depths = DepthTexture.GatherRed(LinearSampler, sampleUV);
	return all(isfinite(depths)) && all(depths >= 0.0) && all(depths <= 1.0) && (DepthOrder::IsReversed() ? all(depths > 0.0) : all(depths < 1.0));
}

bool SampleCaptureDirection(float3 direction, out float3 position, out float3 color, out float2 uv, out float weight)
{
	position = 0.0;
	color = 0.0;
	weight = 0.0;
	float3 viewDirection = FrameBuffer::WorldToView(direction, false);
	uv = FrameBuffer::ViewToUV(viewDirection, false);
	if (!all(isfinite(uv)) || FrameBuffer::IsOutsideFrame(uv))
		return false;

	float forwardMask = 1.0 - smoothstep(-0.12, 0.55, viewDirection.z);
	forwardMask = smoothstep(0.0, 1.0, sqrt(saturate(forwardMask)));
	const bool suppressSkyAndFrameEdge = false;
	float edgeMask = suppressSkyAndFrameEdge ? GetFrameEdgeMask(uv) : 1.0;
	if (forwardMask <= 1e-3 || edgeMask <= 1e-3)
		return false;

	float2 sampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
#if defined(REFLECTIONS)
	bool excludeSky = suppressSkyAndFrameEdge;
#else
	bool excludeSky = true;
#endif
	[branch] if (excludeSky)
	{
		if (!IsCaptureDepthFootprintValid(sampleUV))
			return false;
	}
	float depth = DepthTexture.SampleLevel(LinearSampler, sampleUV, 0);
	float nearValidity = smoothstep(0.75, 2.75, SharedData::GetScreenDepth(depth));
	if (!isfinite(depth) || nearValidity <= 1e-3)
		return false;
	float nearWeight = lerp(0.88, 1.0, nearValidity);
#if defined(REFLECTIONS)
	float skyMask = suppressSkyAndFrameEdge ? (1.0 - smoothstep(0.9988, 0.99997, FrameBuffer::ToStandardDepth(depth))) : 1.0;
#	else
	float skyMask = 1.0 - smoothstep(0.9992, 0.99995, FrameBuffer::ToStandardDepth(depth));
#	endif
	weight = forwardMask * nearWeight * edgeMask * skyMask;
	if (weight <= 1e-3)
		return false;

	// Reconstruct with unscaled viewport UVs; only the texture lookup uses render scale.
	float4 positionCS = mul(FrameBuffer::CameraViewProjInverse, float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
	position = positionCS.xyz / positionCS.w * 0.001;
	color = ColorTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
	return all(isfinite(position)) && all(isfinite(color));
}

bool SampleCapture(uint3 texel, out float3 position, out float3 color, out float2 uv)
{
	position = 0.0;
	color = 0.0;
	uv = 0.0;
	float weight;
	if (!SampleCaptureDirection(-GetSamplingVector(texel), position, color, uv, weight))
		return false;
	color = Color::IrradianceToLinear(color);
	if (!all(isfinite(color)) || !all(isfinite(position)) || any(abs(position) > 65504.0))
		return false;
	color = clamp(color, 0.0, 65504.0);
	return true;
}

#endif
