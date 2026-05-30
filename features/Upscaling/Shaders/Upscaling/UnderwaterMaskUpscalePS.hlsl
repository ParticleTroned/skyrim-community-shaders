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
Texture2D<float> SceneDepth : register(t1);
#		if !defined(NO_HMD_STENCIL_MASK)
Texture2D<uint> StencilTex : register(t2);
#		endif
#	endif

cbuffer JitterCB : register(b0)
{
	float2 jitter;
	float useWideKernel;
	float pad0;
};

#if defined(VR)
static const float kSkyDepthThreshold = 1e-6;

#	if defined(RAW_SCENE_DEPTH)
float SampleRawDepthClamped(int2 coord, int2 maxCoord)
{
	int2 c = clamp(coord, int2(0, 0), maxCoord);
	return SceneDepth.Load(int3(c, 0));
}

float SampleRawMinDepth2x2(float2 uv)
{
	float4 depthQuad = SceneDepth.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

float SampleRawMinDepth3x3(float2 uv)
{
	uint width;
	uint height;
	SceneDepth.GetDimensions(width, height);
	int2 maxCoord = int2(width, height) - 1;
	int2 centerCoord = int2(uv * float2(width, height));

	float row0 = min(
		SampleRawDepthClamped(centerCoord + int2(-1, -1), maxCoord),
		min(
			SampleRawDepthClamped(centerCoord + int2(0, -1), maxCoord),
			SampleRawDepthClamped(centerCoord + int2(1, -1), maxCoord)));

	float row1 = min(
		SampleRawDepthClamped(centerCoord + int2(-1, 0), maxCoord),
		min(
			SampleRawDepthClamped(centerCoord + int2(0, 0), maxCoord),
			SampleRawDepthClamped(centerCoord + int2(1, 0), maxCoord)));

	float row2 = min(
		SampleRawDepthClamped(centerCoord + int2(-1, 1), maxCoord),
		min(
			SampleRawDepthClamped(centerCoord + int2(0, 1), maxCoord),
			SampleRawDepthClamped(centerCoord + int2(1, 1), maxCoord)));

	return min(row0, min(row1, row2));
}
#	endif

#	if !defined(NO_HMD_STENCIL_MASK)
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
#	endif
#endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within bounds
#	if defined(RAW_SCENE_DEPTH)
	uv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);
#	else
	uv = clamp(uv, 0.0, FrameBuffer::DynamicResolutionParams1.xy);
#	endif

#	if defined(VR)
	// In VR the vanilla waterline draw (DrawIndexedInstanced, 2 instances) emits
	// identical left-eye clip positions for both instances.  The internal-res mask
	// therefore only represents the left eye: the right-eye half of the buffer
	// contains the tapered apex of the left-eye polygon, which is nearly all black.
	// GetDynamicResolutionAdjustedScreenPosition then samples that black region for
	// the right eye, making the entire right-eye underwater fog incorrect.
	//
	// Fix: reconstruct the mask analytically per-eye, using scene depth for geometry
	// pixels and ray direction only for sky/unrendered pixels. Submit-stage uses the
	// raw-depth/no-stencil variant so the HMD hidden-area mask cannot carve HAM
	// silhouettes into the water fog. If raw submit cannot resolve water height, do
	// not reuse the copied vanilla mask: it is the artifact source.

	uint eyeIndex = (input.TexCoord.x >= 0.5) ? 1 : 0;
	uint depthWidth;
	uint depthHeight;
	SceneDepth.GetDimensions(depthWidth, depthHeight);
#		if defined(RAW_SCENE_DEPTH)
	float2 depthUV = uv;
#		else
	float2 depthUV = input.TexCoord;
#		endif
	uint2 depthCoord = min(uint2(depthUV * float2(depthWidth, depthHeight)), uint2(depthWidth, depthHeight) - 1);
#		if !defined(NO_HMD_STENCIL_MASK)
	if (IsHiddenStencil(depthCoord)) {
		psout.UnderwaterMask = 0.0;
		return psout;
	}
#		endif

	// WaterData is a 5×5 grid centered on the camera; tile 12 (row 2, col 2) is
	// always the camera's own tile. Pass eyeIndex so GetWaterData corrects the .w
	// (water surface height) from eye-0 camera-relative Z into the current eye's frame.
	float waterHeight = SharedData::GetWaterData(float3(0, 0, 0), eyeIndex).w;

	// Tile sentinel: try the CPU water-system fallback. It is stored eye-0
	// camera-relative so apply the same per-eye correction as GetWaterData.
	if (waterHeight <= WATER_HEIGHT_NO_TILE_SENTINEL) {
		float sysHeight = SharedData::WaterSystemHeight;
		if (sysHeight > WATER_HEIGHT_NO_TILE_SENTINEL)
			waterHeight = sysHeight + FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPosAdjust[eyeIndex].z;
	}

	// GetWaterData returns INT_MIN (~-2.147e9) when the tile is outside the 5x5 grid.
	if (waterHeight > WATER_HEIGHT_NO_TILE_SENTINEL) {
		// Unpack from side-by-side stereo layout to per-eye UV [0, 1]
		float2 eyeUV = float2(input.TexCoord.x * 2.0 - (float)eyeIndex, input.TexCoord.y);

		// Convert to NDC [-1, 1].  UV y=0 is the top of the screen; NDC y=+1 is the top.
		float2 ndc = float2(eyeUV.x * 2.0 - 1.0, 1.0 - eyeUV.y * 2.0);

		// Sample either the current full-resolution upscaled depth or, in submit-stage,
		// the raw dynamic-resolution scene depth with dynamic-resolution UVs.
#		if defined(RAW_SCENE_DEPTH)
		float depth = (useWideKernel > 0.5f) ? SampleRawMinDepth3x3(depthUV) : SampleRawMinDepth2x2(depthUV);
#		else
		float depth = SceneDepth.Load(int3(depthCoord, 0)).x;
#		endif

		if (depth > kSkyDepthThreshold) {
			// Geometry pixel: reconstruct world position from depth.
			// CameraViewProjInverse[eyeIndex] maps clip-space back to the per-eye
			// camera-relative world space.  waterHeight has been adjusted to the same
			// frame, so the comparison is correct for both eyes.
			float4 worldPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(ndc, depth, 1.0));
			worldPos /= worldPos.w;
			// kSurfaceBias (Skyrim world units, ~1 unit ≈ 1.4 cm) anchors the mask
			// threshold relative to the flat waterHeight plane to absorb wave-vertex
			// displacement (measured max trough ≈ 2.92 units; 3.5 gives margin).
			//
			// When the camera is underwater, expand upward so the near-surface band
			// remains fogged.
			static const float kSurfaceBias = 3.5;
			bool lookingUp = worldPos.z > 0.0;
			bool cameraUnderwater = waterHeight > 0.0;
			float threshold = (cameraUnderwater && lookingUp) ? waterHeight + kSurfaceBias : waterHeight - kSurfaceBias;
			psout.UnderwaterMask = (worldPos.z < threshold) ? 1.0 : 0.0;
		} else {
			// depth <= kSkyDepthThreshold: sky / unrendered pixels (reversed-Z depth clear value).
			// Unproject to obtain the per-pixel ray direction and decide based on that.
			float4 worldFarPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(ndc, 0.0, 1.0));
			worldFarPos /= worldFarPos.w;
			float3 rayDir = normalize(worldFarPos.xyz);
			// Per-eye waterHeight > 0 means the water surface is above THIS eye's camera
			// (eye is below water); <= 0 means the eye camera is above the water surface.
			psout.UnderwaterMask = (waterHeight > 0.0 || rayDir.z < 0.0) ? 1.0 : 0.0;
		}
		return psout;
	}
#		if defined(RAW_SCENE_DEPTH)
	psout.UnderwaterMask = 0.0;
	return psout;
#		endif
#	endif

	// Upscale using linear sampling with jitter-corrected coordinates.
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif
