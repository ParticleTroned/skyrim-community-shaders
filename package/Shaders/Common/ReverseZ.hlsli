#ifndef __REVERSE_Z_DEPENDENCY_HLSL__
#define __REVERSE_Z_DEPENDENCY_HLSL__

#include "Common/DepthOrder.hlsli"
#include "Common/FrameBuffer.hlsli"

#ifdef REVERSE_Z
#	define SCENE_DEPTH_FORMAT float
#else
#	define SCENE_DEPTH_FORMAT unorm float
#endif

// REVERSE_Z selects float storage. The uploaded scene camera selects its encoding:
// SE's startup menus can render standard depth before the world camera reverses.
namespace FrameBuffer
{
	float FarPlaneDepth()
	{
		return DepthOrder::Far();
	}

	float NearPlaneDepth()
	{
		return DepthOrder::Near();
	}

	/** @brief The nearer of two raw depths under the active convention. */
	float NearerDepth(float a, float b)
	{
		return DepthOrder::Nearest(a, b);
	}

	/** @brief The farther of two raw depths under the active convention. */
	float FartherDepth(float a, float b)
	{
		return DepthOrder::Farthest(a, b);
	}

	/** @brief The nearest of four raw depths under the active convention. */
	float NearestDepth(float4 v)
	{
		return NearerDepth(NearerDepth(v.x, v.y), NearerDepth(v.z, v.w));
	}

	/** @brief The farthest of four raw depths under the active convention. */
	float FarthestDepth(float4 v)
	{
		return FartherDepth(FartherDepth(v.x, v.y), FartherDepth(v.z, v.w));
	}

	/** @brief True when a is strictly nearer than b under the active convention. */
	bool IsNearerDepth(float a, float b)
	{
		return DepthOrder::IsNearer(a, b);
	}

	bool IsReverseProjection(float4x4 projection)
	{
#ifdef REVERSE_Z
		return projection[2][2] * projection[3][2] < 0.0;
#else
		return false;
#endif
	}

	bool IsReverseProjection()
	{
		return DepthOrder::IsReversed();
	}

	float FarPlaneClipZ(float clipW, bool reverseProjection)
	{
		return reverseProjection ? 0.0 : clipW;
	}

	float FarPlaneClipZ(float clipW)
	{
		return FarPlaneClipZ(clipW, IsReverseProjection());
	}

	float ToStandardClipZ(float4 clipPosition, bool reverseProjection)
	{
		return reverseProjection ? clipPosition.w - clipPosition.z : clipPosition.z;
	}

	float ToStandardClipZ(float4 clipPosition)
	{
		return ToStandardClipZ(clipPosition, IsReverseProjection());
	}

	float3 ToStandardClip(float4 clipPosition, bool reverseProjection)
	{
		return float3(clipPosition.xy, ToStandardClipZ(clipPosition, reverseProjection));
	}

	float3 ToStandardClip(float4 clipPosition)
	{
		return ToStandardClip(clipPosition, IsReverseProjection());
	}

	float4 OffsetClipDepth(float4 clipPosition, float standardOffset, bool reverseProjection)
	{
		clipPosition.z += reverseProjection ? -standardOffset : standardOffset;
		return clipPosition;
	}

	float4 OffsetClipDepth(float4 clipPosition, float standardOffset)
	{
		return OffsetClipDepth(clipPosition, standardOffset, IsReverseProjection());
	}

	float ToNativeDepth(float standardDepth)
	{
		return IsReverseProjection() ? 1.0 - standardDepth : standardDepth;
	}

	float4 ToNativeDepth(float4 standardDepth)
	{
		return IsReverseProjection() ? 1.0 - standardDepth : standardDepth;
	}

	float ToStandardDepth(float depth)
	{
		return IsReverseProjection() ? 1.0 - depth : depth;
	}

	float2 ToStandardDepth(float2 depth)
	{
		return IsReverseProjection() ? 1.0 - depth : depth;
	}

	float4 ToStandardDepth(float4 depth)
	{
		return IsReverseProjection() ? 1.0 - depth : depth;
	}
}

#endif  //__REVERSE_Z_DEPENDENCY_HLSL__
