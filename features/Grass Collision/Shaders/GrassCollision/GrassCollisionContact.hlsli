#ifndef GRASS_COLLISION_CONTACT_HLSLI
#define GRASS_COLLISION_CONTACT_HLSLI
namespace GrassCollisionContact
{
	// Heights relative to the fixed field origin stay finite in half-float storage.
	static const float MaximumHeight = 32752.0;
	static const float HeightTolerance = 2.0;
	// Every update count through this cap is exactly representable in binary16.
	static const float MaximumIdleUpdates = 2048.0;

	bool IsValid(float2 bounds)
	{
		return all(isfinite(bounds)) && all(abs(bounds) <= MaximumHeight) && bounds.x <= bounds.y;
	}

	bool Overlaps(float2 first, float2 second)
	{
		return first.x <= second.y && second.x <= first.y;
	}

	float2 ConservativeHalfBounds(float2 bounds)
	{
		uint2 packed = f32tof16(bounds);
		float2 rounded = f16tof32(packed);
		// Round only inward endpoints outward, by at most one adjacent half value.
		if (rounded.x > bounds.x)
			packed.x += bounds.x < 0 ? 1u : 0xffffffffu;
		if (rounded.y < bounds.y)
			packed.y += bounds.y < 0 ? 0xffffffffu : 1u;
		return f16tof32(packed);
	}

	float BladeWeight(float2 bounds, float rootHeight, float bladeHeight)
	{
		if (!IsValid(bounds) || rootHeight > bounds.y + HeightTolerance)
			return 0;
		return saturate((rootHeight + bladeHeight - bounds.x) / max(bladeHeight, 1e-4));
	}
}
#endif
