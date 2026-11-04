#ifndef GRASS_COLLISION_HLSLI
#define GRASS_COLLISION_HLSLI
#include "GrassCollision/GrassCollisionCommon.hlsli"
#ifdef GRASS_COLLISION_BASIC
#	include "GrassCollision/GrassCollisionBasic.hlsli"
#else
#	include "GrassCollision/GrassCollisionField.hlsli"

namespace GrassCollision
{
	float CompressionWeight(float normalizedHeight, float bladeHeight, float reach, float maximumHeight)
	{
		maximumHeight = max(maximumHeight, 1.0);
		float heightFade = min(maximumHeight * 0.25, 16.0);
		return smoothstep(0, max(reach, 0.1), normalizedHeight) *
		       (1 - smoothstep(maximumHeight - heightFade, maximumHeight, bladeHeight));
	}

	float3 CalculateDisplacement(float3 position, float3 root, float4 field, float bendWeight,
		float compressionWeight, float strength, float maximumBend, float4x4 world,
		out float3 bendAxis, out float bendAngle, out float compressionScale)
	{
		bendAxis = float3(0, 1, 0);
		bendAngle = 0;
		compressionScale = 1;
		float3 relative = position - root;
		if (strength <= 0 || field.w <= 0 || bendWeight <= 0 || relative.z <= 0 || !all(isfinite(field)))
			return 0;
		float3 deformed = relative;
		float magnitude = length(field.xy);
		if (magnitude > 1e-6) {
			float3 direction = mul(transpose((float3x3)world), float3(field.xy / magnitude, 0));
			direction.z = 0;
			float directionLength = length(direction);
			if (directionLength > 1e-6) {
				bendAxis = cross(float3(0, 0, 1), direction / directionLength);
				bendAngle = min(magnitude * strength, maximumBend) * bendWeight * field.w;
				deformed = RotateVector(relative, bendAxis, bendAngle);
			}
		}
		compressionScale = 1 - saturate(field.z * strength) * compressionWeight * field.w;
		deformed.z *= compressionScale;
		// Authored roots stay fixed and bending cannot push a vertex below its root.
		deformed.z = max(deformed.z, 0);
		return deformed - relative;
	}

	void ApplyDeformation(VS_INPUT input, inout float3 currentPosition, inout float3 previousPosition,
		float distance, out float3 bendAxis, out float bendAngle, out float compressionScale)
	{
		bendAxis = float3(0, 1, 0);
		bendAngle = 0;
		compressionScale = 1;
		if (input.Color.w <= 0 || distance <= 0 || (CollisionStrength <= 0 && PreviousCollisionStrength <= 0))
			return;
		float normalizedHeight = saturate(input.Color.w);
		float bendWeight = normalizedHeight * normalizedHeight;
		float instanceUpScale = length(float3(input.InstanceData2.z, input.InstanceData3.z, input.InstanceData3.w));
		float vertexHeight = abs(input.Position.z * (input.InstanceData4.y * ScaleMask.z + 1)) * instanceUpScale;
		float bladeHeight = vertexHeight / max(normalizedHeight, 1e-4);
		float4 currentField, previousField;
		SampleDeformation(input.InstanceData1.xyz, World[0], PreviousWorld[0], distance, currentField, previousField);
		currentPosition += CalculateDisplacement(currentPosition, input.InstanceData1.xyz, currentField, bendWeight,
			CompressionWeight(normalizedHeight, bladeHeight, CompressionHeight, MaximumCompressibleGrassHeight),
			CollisionStrength, MaximumBend, World[0], bendAxis, bendAngle, compressionScale);
		float3 previousAxis;
		float previousAngle, previousCompression;
		previousPosition += CalculateDisplacement(previousPosition, input.InstanceData1.xyz, previousField, bendWeight,
			CompressionWeight(normalizedHeight, bladeHeight, PreviousCompressionHeight, PreviousMaximumCompressibleGrassHeight),
			PreviousCollisionStrength, PreviousMaximumBend, PreviousWorld[0], previousAxis, previousAngle, previousCompression);
	}
}
#endif
#endif
