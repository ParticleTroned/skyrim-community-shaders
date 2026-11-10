#define __VR_STEREO_EFFECTS_DEPENDENCY_HLSL__
#define VSHADER
#include "FurShells/FurShells.hlsli"

namespace FurShells
{
	// Unfiltered contact evaluation is the reference for conservative capsule rejection.
	float3 ReferenceTouch(bool previous, float3 position, float3 normal, float shell, float furLength, out float3 debug)
	{
		float3 total = 0;
		float press = 0;
		debug = 0;
		float3 displacement = 0;
		[branch] if ((previous ? Previous.TouchCount : Current.TouchCount) > 0)
		{
			float margin = max(furLength * TouchMarginScale, TouchMinMargin);
			[loop] for (uint index = 0; index < min(uint(previous ? Previous.TouchCount : Current.TouchCount), 12u); ++index)
			{
				float3 pointA = (previous ? Previous.TouchA[index] : Current.TouchA[index]).xyz;
				float3 axis = (previous ? Previous.TouchB[index] : Current.TouchB[index]).xyz - pointA;
				float t = saturate(dot(position - pointA, axis) / max(dot(axis, axis), 1e-4));
				float3 offset = position - (pointA + axis * t);
				float distance = length(offset);
				float3 direction = offset / max(distance, 1e-4);
				float radius = (previous ? Previous.TouchA[index] : Current.TouchA[index]).w;
				float strength = (previous ? Previous.TouchMove[index] : Current.TouchMove[index]).w;
				float reach = saturate((radius + margin - distance) / margin);
				float influence = reach * strength;
				float penetration = max(radius - distance, 0) * strength;
				bool remembered = (previous ? Previous.TouchB[index] : Current.TouchB[index]).w > 0.5;
				debug = float3(remembered ? debug.x : max(debug.x, influence), remembered ? debug.y : max(debug.y, step(0, radius - distance)), remembered && reach > 0 ? max(debug.z, strength) : debug.z);
				float stable = saturate(distance / max(radius * TouchStableRadius, 1e-3));
				total += direction * ((influence * furLength * TouchBend + penetration) * stable) + (previous ? Previous.TouchMove[index] : Current.TouchMove[index]).xyz * (influence * TouchDrag);
				press = max(press, influence);
			}
			total *= previous ? Previous.TouchStrength : Current.TouchStrength;
			float magnitude = length(total);
			float3 push = total * (min(magnitude, TouchMaxPush) / max(magnitude, 1e-4)) * (shell * shell);
			displacement = push - normal * (furLength * shell * press * TouchPress * saturate(previous ? Previous.TouchStrength : Current.TouchStrength));
		}
		return displacement;
	}
}

cbuffer Cases : register(b0)
{
	uint PositionCount;
	uint PreviousState;
	float FurLength;
	float Shell;
	float3 PositionOffset;
};
StructuredBuffer<float4> Positions : register(t0);
struct Result
{
	float4 filtered;
	float4 filteredDebug;
	float4 reference;
	float4 referenceDebug;
};
RWStructuredBuffer<Result> Results : register(u0);

[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (id.x >= PositionCount)
		return;
	float3 debug, referenceDebug;
	Result result;
	float3 position = Positions[id.x].xyz + PositionOffset;
	result.filtered = float4(FurShells::GetTouchDisplacement(PreviousState != 0, position, float3(0, 0, 1), Shell, FurLength, debug), 0);
	result.filteredDebug = float4(debug, 0);
	result.reference = float4(FurShells::ReferenceTouch(PreviousState != 0, position, float3(0, 0, 1), Shell, FurLength, referenceDebug), 0);
	result.referenceDebug = float4(referenceDebug, 0);
	Results[id.x] = result;
}
