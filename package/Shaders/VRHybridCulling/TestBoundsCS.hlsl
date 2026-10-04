#include "Common/DepthOrder.hlsli"
#include "VRHybridCulling/ProjectedBounds.hlsli"

struct OBBTransform
{
	row_major float4x4 transform;
};

StructuredBuffer<OBBTransform> ObjectBounds : register(t0);
Texture2DArray<float> DepthPyramid : register(t1);
RWStructuredBuffer<uint> Visibility : register(u0);
#ifdef CSX_HIZ_DIAGNOSTICS
RWStructuredBuffer<uint4> TraversalDiagnostics : register(u1);
#endif

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

bool IsOccludedInEye(float4x4 transform, uint eye HIZ_DIAGNOSTIC_PARAMETERS)
{
	if (any(EyeRect[eye].zw == 0) || any(EyeRect[eye].zw > 16384) ||
		!all(isfinite(CameraAdjust[eye])))
		HIZ_VISIBLE(HIZ_INVALID_INPUT);
	[unroll] for (uint row = 0; row < 4; ++row) if (!all(isfinite(ViewProjection[eye][row]))) HIZ_VISIBLE(HIZ_INVALID_INPUT);

	float2 minimumUV = 3.402823466e+38;
	float2 maximumUV = -3.402823466e+38;
	float nearestDepth = DepthOrder::Far();
	float3 projectedVertices[8];
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
		if (!all(isfinite(clip)))
			HIZ_VISIBLE(HIZ_INVALID_INPUT);
		if (clip.w <= 1e-6 || clip.z <= 0.0 || clip.z >= clip.w)
			HIZ_VISIBLE(HIZ_CLIP_CROSSING);
		float3 ndc = clip.xyz / clip.w;
		if (!all(isfinite(ndc)))
			HIZ_VISIBLE(HIZ_INVALID_INPUT);
		float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
		projectedVertices[vertex] = float3(uv * EyeRect[eye].zw, ndc.z);
		minimumUV = min(minimumUV, uv);
		maximumUV = max(maximumUV, uv);
		nearestDepth = DepthOrder::Nearest(nearestDepth, ndc.z);
	}

	// Native frustum culling owns off-screen rejection, including stereo margins.
	if (any(maximumUV < 0.0) || any(minimumUV > 1.0))
		HIZ_VISIBLE(HIZ_VIEWPORT_GUARD);
	float2 eyeSize = EyeRect[eye].zw;
	float2 minimumPixelBound = minimumUV * eyeSize - PixelGuardBand;
	float2 maximumPixelBound = maximumUV * eyeSize + PixelGuardBand;
	// Pixels outside this eye have no depth evidence, including the motion margin.
	if (any(minimumPixelBound < 0.0) || any(maximumPixelBound >= eyeSize))
		HIZ_VISIBLE(HIZ_VIEWPORT_GUARD);
	uint2 minimumPixel = (uint2)floor(minimumPixelBound);
	uint2 maximumPixel = (uint2)floor(maximumPixelBound);
	const uint2 baseMinimumCell = minimumPixel / SourceReduction;
	const uint2 baseMaximumCell = maximumPixel / SourceReduction;
	uint2 minimumCell = baseMinimumCell;
	uint2 maximumCell = baseMaximumCell;
	if (any(maximumCell >= PyramidSize))
		HIZ_VISIBLE(HIZ_INVALID_INPUT);

	uint mip = 0;
	[loop] while (any(maximumCell - minimumCell > 1) && mip + 1 < MipCount)
	{
		minimumCell >>= 1;
		maximumCell >>= 1;
		++mip;
	}
	if (any(maximumCell - minimumCell > 1))
		HIZ_VISIBLE(HIZ_INVALID_INPUT);

	float farthestDepth = DepthOrder::Near();
	float4 coarseDepths = 0;
	[unroll] for (uint y = 0; y < 2; ++y)
	{
		[unroll] for (uint x = 0; x < 2; ++x)
		{
			uint2 cell = min(minimumCell + uint2(x, y), maximumCell);
			float depth = DepthPyramid.Load(int4(cell, eye, mip));
			HIZ_COUNT_DEPTH;
			if (!isfinite(depth) || depth < 0.0 || depth > 1.0 || (!DepthOrder::Reversed && depth == 0.0))
				HIZ_VISIBLE(HIZ_INVALID_INPUT);
			coarseDepths[y * 2 + x] = depth;
			farthestDepth = DepthOrder::Farthest(farthestDepth, depth);
		}
	}
	if (DepthOrder::IsBehindWithBias(nearestDepth, farthestDepth, DepthBias))
		HIZ_OCCLUDED;

	// Four roots plus three pending siblings per level fit below this fixed capacity.
	const uint stackCapacity = 40;
	const uint maximumDepthLoads = 64;
	uint stack[stackCapacity];
	uint pending = 0;
	uint depthLoads = 4;
	const uint initialMip = mip;
	const uint2 rootMinimum = minimumCell;
	[unroll] for (uint rootY = 0; rootY < 2; ++rootY)
	{
		[unroll] for (uint rootX = 0; rootX < 2; ++rootX)
		{
			uint2 cell = rootMinimum + uint2(rootX, rootY);
			if (all(cell <= maximumCell) &&
				!DepthOrder::IsBehindWithBias(nearestDepth, coarseDepths[rootY * 2 + rootX], DepthBias))
				stack[pending++] = cell.x | (cell.y << 12) | (mip << 24);
		}
	}

	[loop] while (pending != 0)
	{
		uint node = stack[--pending];
		uint2 cell = uint2(node & 4095, (node >> 12) & 4095);
		uint nodeMip = node >> 24;
		float depth;
		if (nodeMip == initialMip) {
			uint2 root = cell - rootMinimum;
			depth = coarseDepths[root.y * 2 + root.x];
		} else {
			if (depthLoads == maximumDepthLoads)
				HIZ_VISIBLE(HIZ_DEPTH_BUDGET);
			depth = DepthPyramid.Load(int4(cell, eye, nodeMip));
			++depthLoads;
			HIZ_COUNT_DEPTH;
		}
		if (!isfinite(depth) || depth < 0.0 || depth > 1.0 || (!DepthOrder::Reversed && depth == 0.0))
			HIZ_VISIBLE(HIZ_INVALID_INPUT);
		if (DepthOrder::IsBehindWithBias(nearestDepth, depth, DepthBias))
			continue;

		float cellSize = SourceReduction << nodeMip;
		float margin = PixelGuardBand + 1.0 / 32.0;
		float2 regionMinimum = float2(cell) * cellSize - margin;
		float2 regionMaximum = (float2(cell) + 1.0) * cellSize + margin;
		HIZ_COUNT_REGION;
		if (ProjectedBounds::OccludedInRegion(projectedVertices, regionMinimum, regionMaximum, depth, DepthBias HIZ_DIAGNOSTIC_ARGUMENT))
			continue;
		if (nodeMip == 0)
			HIZ_VISIBLE(HIZ_FINEST_UNRESOLVED);

		--nodeMip;
		uint2 childMinimum = max(cell * 2, baseMinimumCell >> nodeMip);
		uint2 childMaximum = min(cell * 2 + 1, baseMaximumCell >> nodeMip);
		[unroll] for (uint childY = 0; childY < 2; ++childY)
		{
			[unroll] for (uint childX = 0; childX < 2; ++childX)
			{
				uint2 child = childMinimum + uint2(childX, childY);
				if (all(child <= childMaximum)) {
					if (pending == stackCapacity)
						HIZ_VISIBLE(HIZ_STACK_CAPACITY);
					stack[pending++] = child.x | (child.y << 12) | (nodeMip << 24);
				}
			}
		}
	}
	HIZ_OCCLUDED;
}

[numthreads(64, 1, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint resultCount, resultStride;
	Visibility.GetDimensions(resultCount, resultStride);
	uint objectIndex = dispatchID.x;
	if (objectIndex >= ObjectCount || objectIndex >= resultCount)
		return;
	Visibility[objectIndex] = 1;
#ifdef CSX_HIZ_DIAGNOSTICS
	uint diagnosticCount, diagnosticStride;
	TraversalDiagnostics.GetDimensions(diagnosticCount, diagnosticStride);
	if (objectIndex >= diagnosticCount || diagnosticStride != 16)
		return;
	TraversalDiagnostics[objectIndex] = uint4(4, 0, 0, 0);
#endif

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
	// SM5 logical operators evaluate both sides; explicitly skip an unused second eye.
#ifdef CSX_HIZ_DIAGNOSTICS
	uint4 diagnostic = 0;
	bool firstOccluded = IsOccludedInEye(transform, 0 HIZ_DIAGNOSTIC_ARGUMENT);
	uint4 firstEye = diagnostic;
	diagnostic = 0;
	[branch] if (firstOccluded)
	{
		if (IsOccludedInEye(transform, 1 HIZ_DIAGNOSTIC_ARGUMENT))
			Visibility[objectIndex] = 0;
	}
	TraversalDiagnostics[objectIndex] = uint4(firstEye.x | (diagnostic.x << 8), firstEye.yzw + diagnostic.yzw);
#else
	[branch] if (!IsOccludedInEye(transform, 0)) return;
	if (IsOccludedInEye(transform, 1))
		Visibility[objectIndex] = 0;
#endif
}
