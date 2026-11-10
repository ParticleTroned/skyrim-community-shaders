#ifndef GRASS_COLLISION_COMMON_HLSLI
#define GRASS_COLLISION_COMMON_HLSLI
namespace GrassCollision
{
	cbuffer GrassCollisionPerFrame : register(b5)
	{
		float2 PosOffset;
		uint2 ArrayOrigin;
		float2 PreviousPosOffset;
		uint2 PreviousArrayOrigin;
		float CompressionHeight, CollisionStrength, PreviousCollisionStrength, MaximumCompressibleGrassHeight;
		float MaximumBend, PreviousMaximumBend, PreviousCompressionHeight, PreviousMaximumCompressibleGrassHeight;
	}
	float3 RotateVector(float3 value, float3 axis, float angle)
	{
		float sine, cosine;
		sincos(angle, sine, cosine);
		return value * cosine + cross(axis, value) * sine + axis * dot(axis, value) * (1 - cosine);
	}

	float3 DeformNormal(float3 normal, float3 axis, float angle, float compressionScale)
	{
		float3 result = normal;
		[branch] if (angle != 0 || compressionScale != 1)
		{
			result = RotateVector(normal, axis, angle);
			result.z /= max(compressionScale, 1e-4);
			result *= rsqrt(max(dot(result, result), 1e-12));
		}
		return result;
	}

}
#endif
