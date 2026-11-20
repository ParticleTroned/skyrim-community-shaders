#ifndef CSX_PROJECTED_MATERIAL_HLSLI
#define CSX_PROJECTED_MATERIAL_HLSLI

namespace ProjectedMaterial
{
	/// Map a parallax UV hit back onto the receiver's local surface for coating projection.
	/// Gradients must come from the undisplaced UVs and camera-relative position before divergent flow.
	float3 GetParallaxPositionOffset(float2 uvOffset, float2 uvDx, float2 uvDy, float3 positionDx, float3 positionDy)
	{
		float3 result = 0.0;
		[branch] if (any(uvOffset != 0.0))
		{
			float gradientScale = max(max(abs(uvDx.x), abs(uvDx.y)), max(abs(uvDy.x), abs(uvDy.y)));
			if (gradientScale > 0.0 && isfinite(gradientScale)) {
				float2 dx = uvDx / gradientScale;
				float2 dy = uvDy / gradientScale;
				float determinant = dx.x * dy.y - dx.y * dy.x;
				// Collapsed or nearly parallel UV gradients cannot define a stable receiver location.
				if (determinant * determinant > 1e-10 * dot(dx, dx) * dot(dy, dy)) {
					float2 delta = uvOffset / gradientScale;
					float2 screenOffset = float2(delta.x * dy.y - delta.y * dy.x, delta.y * dx.x - delta.x * dx.y) / determinant;
					float3 positionOffset = positionDx * screenOffset.x + positionDy * screenOffset.y;
					result = all(isfinite(positionOffset)) ? positionOffset : 0.0;
				}
			}
		}
		return result;
	}
}

#endif
