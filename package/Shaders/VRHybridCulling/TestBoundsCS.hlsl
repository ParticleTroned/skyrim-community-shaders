#include "Common/DepthOrder.hlsli"
#include "VRHybridCulling/ProjectedBounds.hlsli"

struct OBBTransform
{
	row_major float4x4 transform;
};

StructuredBuffer<OBBTransform> ObjectBounds : register(t0);
Texture2DArray<float> DepthPyramid : register(t1);
Texture2D<float> SourceDepth : register(t2);
RWStructuredBuffer<uint> Visibility : register(u0);
#ifdef CSX_HIZ_DIAGNOSTICS
RWStructuredBuffer<HiZTraversalDiagnostic> TraversalDiagnostics : register(u1);
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

bool FarClipEnabled()
{
#ifdef CSX_HIZ_CLIP_AB
	return (Reserved & 4u) == 0;
#else
	return true;
#endif
}

bool SourceRefinementEnabled()
{
#ifdef CSX_HIZ_REFINEMENT_AB
	return (Reserved & 1) == 0;
#else
	return true;
#endif
}

bool IsValidPyramidDepth(float depth)
{
	return isfinite(depth) && depth >= 0.0 && depth <= 1.0 && (DepthOrder::Reversed || depth != 0.0);
}

/// Untrusted source pixels cannot provide occlusion evidence, even in the reversed test permutation.
float ReadSourceDepth(uint2 pixel, uint eye)
{
	float depth = SourceDepth.Load(int3(pixel + EyeRect[eye].xy, 0));
	return isfinite(depth) && depth > 0.0 && depth <= 1.0 ? depth : DepthOrder::Far();
}

bool IsOccludedInEye(float4x4 transform, uint eye HIZ_DIAGNOSTIC_PARAMETERS)
{
	if (any(EyeRect[eye].zw == 0) || any(EyeRect[eye].zw > 16384) ||
		!all(isfinite(CameraAdjust[eye])))
		HIZ_VISIBLE(HIZ_INVALID_INPUT);
	[unroll] for (uint row = 0; row < 4; ++row) if (!all(isfinite(ViewProjection[eye][row]))) HIZ_VISIBLE(HIZ_INVALID_INPUT);

	float2 minimumUV = 3.402823466e+38;
	float2 maximumUV = -3.402823466e+38;
	float nearestDepth = DepthOrder::Far();
	float2 nearestVertexPixel = 0.0;
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
		if (clip.w <= 1e-6)
			HIZ_VISIBLE(HIZ_EYE_CROSSING);
		if (DepthOrder::Reversed ? clip.z >= clip.w : clip.z <= 0.0)
			HIZ_VISIBLE(HIZ_NEAR_CROSSING);
		bool crossesFar = DepthOrder::Reversed ? clip.z <= 0.0 : clip.z >= clip.w;
		if (crossesFar && !FarClipEnabled())
			HIZ_VISIBLE(HIZ_FAR_CROSSING);
		float3 ndc = clip.xyz / clip.w;
		if (!all(isfinite(ndc)))
			HIZ_VISIBLE(HIZ_INVALID_INPUT);
		if (crossesFar) {
			// Moving far vertices toward the camera lowers the affine face-depth bound.
			ndc.z = DepthOrder::Farthest(DepthOrder::Near(), DepthOrder::Nearest(ndc.z, DepthOrder::Far()));
			HIZ_COUNT_FAR_CLAMP;
		}
		float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
		ProjectedBounds::Vertices[vertex] = float3(uv * EyeRect[eye].zw, ndc.z);
		minimumUV = min(minimumUV, uv);
		maximumUV = max(maximumUV, uv);
		nearestDepth = DepthOrder::Nearest(nearestDepth, ndc.z);
		if (ndc.z == nearestDepth)
			nearestVertexPixel = ProjectedBounds::Vertices[vertex].xy;
	}

	// Native frustum culling owns off-screen rejection, including stereo margins.
	if (any(maximumUV < 0.0) || any(minimumUV > 1.0))
		HIZ_VISIBLE(HIZ_VIEWPORT_OFFSCREEN);
#ifdef CSX_HIZ_DIAGNOSTICS
	if (any(minimumUV < 0.0) || any(maximumUV > 1.0))
		HIZ_VISIBLE(HIZ_VIEWPORT_PARTIAL);
#endif
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
			if (!IsValidPyramidDepth(depth))
				HIZ_VISIBLE(HIZ_INVALID_INPUT);
			coarseDepths[y * 2 + x] = depth;
			farthestDepth = DepthOrder::Farthest(farthestDepth, depth);
		}
	}
	if (DepthOrder::IsBehindWithBias(nearestDepth, farthestDepth, DepthBias))
		HIZ_OCCLUDED;

	// A retained nearest vertex also prevents every ancestor covering it from proving occlusion.
	const uint2 witnessCell = (uint2)floor(nearestVertexPixel) / SourceReduction;
	uint depthLoads = 4;
	float witnessDepth;
	if (mip == 0) {
		uint2 root = witnessCell - minimumCell;
		witnessDepth = coarseDepths[root.y * 2 + root.x];
	} else {
		witnessDepth = DepthPyramid.Load(int4(witnessCell, eye, 0));
		++depthLoads;
		HIZ_COUNT_DEPTH;
	}
	if (!IsValidPyramidDepth(witnessDepth))
		HIZ_VISIBLE(HIZ_INVALID_INPUT);
	if (!DepthOrder::IsBehindWithBias(nearestDepth, witnessDepth, DepthBias)) {
		if (SourceReduction == 1 || !SourceRefinementEnabled())
			HIZ_VISIBLE(HIZ_NEAREST_UNRESOLVED);
		// A reduced witness can include unrelated holes; test the actual vertex pixel before retaining.
		float sourceWitness = ReadSourceDepth((uint2)floor(nearestVertexPixel), eye);
		++depthLoads;
		HIZ_COUNT_DEPTH;
		HIZ_COUNT_SOURCE_WITNESS;
		if (!DepthOrder::IsBehindWithBias(nearestDepth, sourceWitness, DepthBias))
			HIZ_VISIBLE(HIZ_NEAREST_UNRESOLVED);
	}

	ProjectedBounds::PrepareFaces();

	// Four roots plus three pending siblings per level fit below this fixed capacity.
	const uint stackCapacity = 40;
	const uint maximumDepthLoads = 64;
	uint stack[stackCapacity];
	uint pending = 0;
	uint sourcePending = 0;
	uint2 sourceOrigin = 0;
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

	[loop] while (pending != 0 || sourcePending != 0)
	{
		bool sourcePixel = sourcePending != 0;
		uint2 cell;
		uint nodeMip = 0;
		float depth;
		if (sourcePixel) {
			--sourcePending;
			cell = sourceOrigin + uint2(sourcePending % SourceReduction, sourcePending / SourceReduction);
			if (any(cell >= EyeRect[eye].zw))
				HIZ_VISIBLE(HIZ_FINEST_UNRESOLVED);
			if (depthLoads == maximumDepthLoads)
				HIZ_VISIBLE(HIZ_DEPTH_BUDGET);
			depth = ReadSourceDepth(cell, eye);
			++depthLoads;
			HIZ_COUNT_DEPTH;
			HIZ_COUNT_SOURCE_PIXEL;
		} else {
			uint node = stack[--pending];
			cell = uint2(node & 4095, (node >> 12) & 4095);
			nodeMip = node >> 24;
			if (nodeMip == initialMip) {
				uint2 root = cell - rootMinimum;
				depth = coarseDepths[root.y * 2 + root.x];
			} else if (nodeMip == 0 && all(cell == witnessCell)) {
				depth = witnessDepth;
			} else {
				if (depthLoads == maximumDepthLoads)
					HIZ_VISIBLE(HIZ_DEPTH_BUDGET);
				depth = DepthPyramid.Load(int4(cell, eye, nodeMip));
				++depthLoads;
				HIZ_COUNT_DEPTH;
			}
		}
		if (!IsValidPyramidDepth(depth))
			HIZ_VISIBLE(HIZ_INVALID_INPUT);
		if (DepthOrder::IsBehindWithBias(nearestDepth, depth, DepthBias)) {
			if (sourcePixel && sourcePending == 0) {
				HIZ_COUNT_RESOLVED_CELL;
			}
			continue;
		}

		float cellSize = sourcePixel ? 1 : SourceReduction << nodeMip;
		float margin = PixelGuardBand + 1.0 / 32.0;
		float2 regionMinimum = float2(cell) * cellSize - margin;
		float2 regionMaximum = (float2(cell) + 1.0) * cellSize + margin;
		HIZ_COUNT_REGION;
		if (ProjectedBounds::OccludedInRegion(regionMinimum, regionMaximum, depth, DepthBias HIZ_DIAGNOSTIC_ARGUMENT)) {
			if (sourcePixel && sourcePending == 0) {
				HIZ_COUNT_RESOLVED_CELL;
			}
			continue;
		}
		if (sourcePixel)
			HIZ_VISIBLE(HIZ_FINEST_UNRESOLVED);
		if (nodeMip == 0) {
			if (SourceReduction == 1 || !SourceRefinementEnabled())
				HIZ_VISIBLE(HIZ_FINEST_UNRESOLVED);
			// Reuse the same region tester and polygon storage for selectively expanded source pixels.
			sourceOrigin = cell * SourceReduction;
			sourcePending = SourceReduction * SourceReduction;
			HIZ_COUNT_REFINED_CELL;
			continue;
		}

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
	if (objectIndex >= diagnosticCount || diagnosticStride != 96)
		return;
	HiZTraversalDiagnostic invalidDiagnostic = (HiZTraversalDiagnostic)0;
	invalidDiagnostic.traversal.x = HIZ_INVALID_INPUT;
	TraversalDiagnostics[objectIndex] = invalidDiagnostic;
#endif

	uint inputCount, inputStride;
	ObjectBounds.GetDimensions(inputCount, inputStride);
	uint pyramidWidth, pyramidHeight, pyramidLayers, pyramidMips;
	DepthPyramid.GetDimensions(0, pyramidWidth, pyramidHeight, pyramidLayers, pyramidMips);
	uint sourceWidth, sourceHeight;
	SourceDepth.GetDimensions(sourceWidth, sourceHeight);
	if (ObjectCount > 4096 || objectIndex >= inputCount || inputStride != 64 || resultStride != 4 ||
		pyramidLayers != 2 || MipCount == 0 || MipCount != pyramidMips || MipCount > 12 ||
		any(PyramidSize != uint2(pyramidWidth, pyramidHeight)) || any(PyramidSize == 0) ||
		any(PyramidSize > 4096) || any((PyramidSize & (PyramidSize - 1)) != 0) ||
		SourceReduction < 1 || SourceReduction > 8 || (SourceReduction & (SourceReduction - 1)) != 0 ||
		!isfinite(DepthBias) || DepthBias < 8.0 / 16777216.0 || DepthBias > 1.0 ||
		!isfinite(PixelGuardBand) || PixelGuardBand < 1.0 || PixelGuardBand > 16384.0)
		return;
#if defined(CSX_HIZ_REFINEMENT_AB) || defined(CSX_HIZ_INTERSECTION_AB) || defined(CSX_HIZ_CLIP_AB)
	const uint allowedControls =
#	ifdef CSX_HIZ_REFINEMENT_AB
		1u |
#	endif
#	ifdef CSX_HIZ_INTERSECTION_AB
		2u |
#	endif
#	ifdef CSX_HIZ_CLIP_AB
		4u |
#	endif
		0u;
	if ((Reserved & ~allowedControls) != 0)
		return;
#else
	if (Reserved != 0)
		return;
#endif
	[unroll] for (uint eye = 0; eye < 2; ++eye) if (any(EyeRect[eye].zw == 0) || any(EyeRect[eye].zw > uint2(sourceWidth, sourceHeight)) ||
													any(EyeRect[eye].xy > uint2(sourceWidth, sourceHeight) - EyeRect[eye].zw)) return;

	float4x4 transform = ObjectBounds[objectIndex].transform;
	[unroll] for (uint row = 0; row < 4; ++row) if (!all(isfinite(transform[row]))) return;
	if (any(transform[3] != float4(0.0, 0.0, 0.0, 1.0)))
		return;
	// SM5 logical operators evaluate both sides; explicitly skip an unused second eye.
#ifdef CSX_HIZ_DIAGNOSTICS
	HiZTraversalDiagnostic diagnostic = (HiZTraversalDiagnostic)0;
	bool firstOccluded = IsOccludedInEye(transform, 0 HIZ_DIAGNOSTIC_ARGUMENT);
	HiZTraversalDiagnostic firstEye = diagnostic;
	diagnostic = (HiZTraversalDiagnostic)0;
	[branch] if (firstOccluded)
	{
		if (IsOccludedInEye(transform, 1 HIZ_DIAGNOSTIC_ARGUMENT))
			Visibility[objectIndex] = 0;
	}
	diagnostic.traversal = uint4(firstEye.traversal.x | (diagnostic.traversal.x << 8), firstEye.traversal.yzw + diagnostic.traversal.yzw);
	diagnostic.proofs += firstEye.proofs;
	diagnostic.planeWork += firstEye.planeWork;
	diagnostic.refinement += firstEye.refinement;
	diagnostic.regionWork += firstEye.regionWork;
	TraversalDiagnostics[objectIndex] = diagnostic;
#else
	[branch] if (!IsOccludedInEye(transform, 0)) return;
	if (IsOccludedInEye(transform, 1))
		Visibility[objectIndex] = 0;
#endif
}
