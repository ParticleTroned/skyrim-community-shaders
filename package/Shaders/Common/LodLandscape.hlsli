#if !(defined(__LOD_LANDSCAPE_DEPENDENCY_HLSL__))
#define __LOD_LANDSCAPE_DEPENDENCY_HLSL__

#	include "Common/ReverseZ.hlsli"

namespace LodLandscape
{
	float4 AdjustLodLandscapeVertexPositionMS(float4 positionMS, row_major float4x4 world, float4 cellParams)
	{
		float4 positionWS = mul(world, positionMS);
		float worldXShift = positionWS.x - cellParams.x;
		float worldYShift = positionWS.y - cellParams.y;
		if ((abs(worldXShift) < cellParams.z) && (abs(worldYShift) < cellParams.w)) {
			positionMS.z -= (230 + positionWS.z / 1e9);
		}
		return positionMS;
	}

	float4 AdjustLodLandscapeVertexPositionCS(float4 positionCS)
	{
		positionCS = FrameBuffer::OffsetClipDepth(positionCS, min(1, 1e-4 * max(0, FrameBuffer::ToStandardClipZ(positionCS) - 70000)) * 0.5);
		return positionCS;
	}
}

#endif  // __LOD_LANDSCAPE_DEPENDENCY_HLSL__
