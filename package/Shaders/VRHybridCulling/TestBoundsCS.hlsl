#include "Common/DepthOrder.hlsli"

struct OBBTransform
{
	row_major float4x4 transform;
};

StructuredBuffer<OBBTransform> ObjectBounds : register(t0);
Texture2DArray<float> DepthPyramid : register(t1);
RWStructuredBuffer<uint> Visibility : register(u0);

cbuffer TestConstants : register(b0)
{
	row_major float4x4 ViewProjection[2];
	float4 CameraAdjust[2];
	uint4 EyeRect[2];
	uint2 PyramidSize;
	uint MipCount;
	uint SourceReduction;
	uint ObjectCount;
	float DepthBias;
	float PixelGuardBand;
	uint Reserved;
};

bool IsOccludedInEye(float4x4 transform, uint eye)
{
	if (any(EyeRect[eye].zw == 0) || any(EyeRect[eye].zw > 16384) ||
		!all(isfinite(CameraAdjust[eye])))
		return false;
	[unroll] for (uint row = 0; row < 4; ++row) if (!all(isfinite(ViewProjection[eye][row]))) return false;

	float2 minimumUV = 3.402823466e+38;
	float2 maximumUV = -3.402823466e+38;
	float nearestDepth = DepthOrder::Far();
	// Camera-relative translation preserves small extents at large world coordinates.
	precise float4x4 relativeTransform = transform;
	[unroll] for (uint axis = 0; axis < 3; ++axis)
		relativeTransform[axis][3] -= CameraAdjust[eye][axis];
	[unroll] for (uint vertex = 0; vertex < 8; ++vertex)
	{
		float3 corner = float3((vertex & 1) != 0 ? 1.0 : -1.0,
			(vertex & 2) != 0 ? 1.0 : -1.0, (vertex & 4) != 0 ? 1.0 : -1.0);
		precise float3 world = mul(relativeTransform, float4(corner, 1.0)).xyz;
		float4 clip = mul(ViewProjection[eye], float4(world, 1.0));
		// Clipping a box through the eye or near plane needs a different bound.
		if (!all(isfinite(clip)) || clip.w <= 1e-6 || clip.z <= 0.0 || clip.z >= clip.w)
			return false;
		float3 ndc = clip.xyz / clip.w;
		if (!all(isfinite(ndc)))
			return false;
		float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
		minimumUV = min(minimumUV, uv);
		maximumUV = max(maximumUV, uv);
		nearestDepth = DepthOrder::Nearest(nearestDepth, ndc.z);
	}

	// Native frustum culling owns off-screen rejection, including stereo margins.
	if (any(maximumUV < 0.0) || any(minimumUV > 1.0))
		return false;
	float2 eyeSize = EyeRect[eye].zw;
	float2 minimumPixelBound = minimumUV * eyeSize - PixelGuardBand;
	float2 maximumPixelBound = maximumUV * eyeSize + PixelGuardBand;
	// Pixels outside this eye have no depth evidence, including the motion margin.
	if (any(minimumPixelBound < 0.0) || any(maximumPixelBound >= eyeSize))
		return false;
	uint2 minimumPixel = (uint2)floor(minimumPixelBound);
	uint2 maximumPixel = (uint2)floor(maximumPixelBound);
	uint2 minimumCell = minimumPixel / SourceReduction;
	uint2 maximumCell = maximumPixel / SourceReduction;
	if (any(maximumCell >= PyramidSize))
		return false;

	uint mip = 0;
	[loop] while (any(maximumCell - minimumCell > 1) && mip + 1 < MipCount)
	{
		minimumCell >>= 1;
		maximumCell >>= 1;
		++mip;
	}
	if (any(maximumCell - minimumCell > 1))
		return false;

	float farthestDepth = DepthOrder::Near();
	[unroll] for (uint y = 0; y < 2; ++y)
	{
		[unroll] for (uint x = 0; x < 2; ++x)
		{
			uint2 cell = min(minimumCell + uint2(x, y), maximumCell);
			float depth = DepthPyramid.Load(int4(cell, eye, mip));
			if (!isfinite(depth) || depth <= 0.0 || depth > 1.0)
				return false;
			farthestDepth = DepthOrder::Farthest(farthestDepth, depth);
		}
	}
	return DepthOrder::IsBehindWithBias(nearestDepth, farthestDepth, DepthBias);
}

[numthreads(64, 1, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint resultCount, resultStride;
	Visibility.GetDimensions(resultCount, resultStride);
	uint objectIndex = dispatchID.x;
	if (objectIndex >= ObjectCount || objectIndex >= resultCount)
		return;
	Visibility[objectIndex] = 1;

	uint inputCount, inputStride;
	ObjectBounds.GetDimensions(inputCount, inputStride);
	uint pyramidWidth, pyramidHeight, pyramidLayers, pyramidMips;
	DepthPyramid.GetDimensions(0, pyramidWidth, pyramidHeight, pyramidLayers, pyramidMips);
	if (ObjectCount > 4096 || objectIndex >= inputCount || inputStride != 64 || resultStride != 4 ||
		pyramidLayers != 2 || MipCount == 0 || MipCount != pyramidMips || MipCount > 12 ||
		any(PyramidSize != uint2(pyramidWidth, pyramidHeight)) || any(PyramidSize == 0) ||
		any(PyramidSize > 4096) || any((PyramidSize & (PyramidSize - 1)) != 0) ||
		SourceReduction < 1 || SourceReduction > 8 || (SourceReduction & (SourceReduction - 1)) != 0 ||
		!isfinite(DepthBias) || DepthBias < 8.0 / 16777216.0 || DepthBias > 1.0 ||
		!isfinite(PixelGuardBand) || PixelGuardBand < 1.0 || PixelGuardBand > 16384.0)
		return;

	float4x4 transform = ObjectBounds[objectIndex].transform;
	[unroll] for (uint row = 0; row < 4; ++row) if (!all(isfinite(transform[row]))) return;
	if (any(transform[3] != float4(0.0, 0.0, 0.0, 1.0)))
		return;
	// SM5 logical operators evaluate both sides; stop before unused stereo work.
	[branch] if (!IsOccludedInEye(transform, 0)) return;
	if (IsOccludedInEye(transform, 1))
		Visibility[objectIndex] = 0;
}
