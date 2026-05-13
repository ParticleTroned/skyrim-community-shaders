#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/Math.hlsli"
#	include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float UnderwaterMask : SV_TARGET;
};

SamplerState LinearSampler : register(s0);

Texture2D<float> UnderwaterMask : register(t0);
#	if defined(VR)
Texture2D<uint> StencilTex : register(t1);
#	endif

cbuffer JitterCB : register(b0)
{
	float2 jitter;
};

#if defined(VR)
bool IsHiddenStencil(uint2 coord)
{
	uint width;
	uint height;
	StencilTex.GetDimensions(width, height);
	int2 maxCoord = int2(width, height) - 1;

	[unroll]
	for (int y = -1; y <= 1; ++y) {
		[unroll]
		for (int x = -1; x <= 1; ++x) {
			int2 sampleCoord = int2(coord) + int2(x, y);
			if (any(sampleCoord < int2(0, 0)) || any(sampleCoord > maxCoord))
				continue;
			if (StencilTex.Load(int3(sampleCoord, 0)) > 0)
				return true;
		}
	}

	return false;
}
#endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within dynamic-resolution bounds (VR: preserve per-eye bounds).
	uv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);

#	if defined(VR)
	uint stencilWidth;
	uint stencilHeight;
	StencilTex.GetDimensions(stencilWidth, stencilHeight);
	uint2 stencilCoord = min(uint2(input.TexCoord * float2(stencilWidth, stencilHeight)), uint2(stencilWidth, stencilHeight) - 1);
	if (IsHiddenStencil(stencilCoord)) {
		psout.UnderwaterMask = 0.0;
		return psout;
	}

	// In VR the vanilla waterline draw can leave the right-eye half of the internal
	// mask wrong. Reconstruct only the water-plane visibility analytically; do not
	// classify against scene depth, because that projects above-water geometry
	// silhouettes into the mask.
	uint eyeIndex = (input.TexCoord.x >= 0.5) ? 1 : 0;
	float waterHeight = SharedData::GetWaterData(float3(0, 0, 0), eyeIndex).w;

	if (waterHeight <= WATER_HEIGHT_NO_TILE_SENTINEL) {
		float sysHeight = SharedData::WaterSystemHeight;
		if (sysHeight > WATER_HEIGHT_NO_TILE_SENTINEL)
			waterHeight = sysHeight + FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPosAdjust[eyeIndex].z;
	}

	if (waterHeight > WATER_HEIGHT_NO_TILE_SENTINEL) {
		float2 eyeUV = float2(input.TexCoord.x * 2.0 - (float)eyeIndex, input.TexCoord.y);
		float2 ndc = float2(eyeUV.x * 2.0 - 1.0, 1.0 - eyeUV.y * 2.0);

		float4 worldFarPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(ndc, 0.0, 1.0));
		worldFarPos /= worldFarPos.w;
		float3 rayDir = normalize(worldFarPos.xyz);

		psout.UnderwaterMask = (waterHeight > 0.0 || rayDir.z < 0.0) ? 1.0 : 0.0;
		return psout;
	}
#	endif

	// Upscale using linear sampling with jitter-corrected coordinates
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif
