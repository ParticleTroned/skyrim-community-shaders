#include "Common/FrameBuffer.hlsli"
#include "Common/WaterContactCandidates.hlsli"
#include "Common/WaterFinite.hlsli"
#include "LightLimitFix/Common.hlsli"

cbuffer PerFrame : register(b0)
{
	uint LightCount;
	uint WaterCandidateBudget;
	uint ParticleContactEnabled;
	uint pad0;
	uint4 ClusterSize;
}

StructuredBuffer<ClusterAABB> clusters : register(t0);
StructuredBuffer<Light> lights : register(t1);
RWStructuredBuffer<uint> lightIndexList : register(u1);
RWStructuredBuffer<LightGrid> lightGrid : register(u2);

float CandidateScore(Light light, float3 center)
{
	float3 position = FrameBuffer::WorldToView(light.positionWS[0].xyz, true, 0);
	float distanceSquared = dot(position - center, position - center);
#if defined(VR)
	float3 rightPosition = FrameBuffer::WorldToView(light.positionWS[1].xyz, true, 1);
	distanceSquared = min(distanceSquared, dot(rightPosition - center, rightPosition - center));
#endif
	float falloff = saturate(1.0 - distanceSquared * light.invRadius * light.invRadius);
	return max(max(light.color.x, light.color.y), light.color.z) * light.fade * falloff;
}

[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
	if (any(threadId >= ClusterSize.xyz))
		return;
	uint index = threadId.x + ClusterSize.x * (threadId.y + ClusterSize.y * threadId.z);
	LightGrid grid = lightGrid[index];
	float3 center = 0.5 * (clusters[index].minPoint.xyz + clusters[index].maxPoint.xyz);
	float scores[8];
	uint candidates[8];
	[unroll] for (uint initSlot = 0; initSlot < 8; ++initSlot) {
		scores[initSlot] = -1.0;
		candidates[initSlot] = WaterContactCandidates::EmptyIndex;
	}
	uint count = min(grid.lightCount, (uint)MAX_CLUSTER_LIGHTS);
	uint budget = min(WaterCandidateBudget, WaterContactCandidates::MaxCandidates);
	[loop] for (uint local = 0; local < count; ++local) {
		uint lightIndex = lightIndexList[grid.offset + local];
		if (lightIndex >= LightCount)
			continue;
		Light light = lights[lightIndex];
		bool particle = (light.lightFlags & LightFlags::Particle) != 0;
		if (!(light.lightFlags & LightFlags::AffectWater) ||
			(light.lightFlags & LightFlags::Disabled) ||
			(particle ? ParticleContactEnabled == 0 : (light.lightFlags & LightFlags::Simple) != 0))
			continue;
		float score = CandidateScore(light, center);
		if (!WaterFinite::IsFinite(score) || score <= 0.0)
			continue;
		uint candidate = local;
		[unroll] for (uint rankSlot = 0; rankSlot < 8; ++rankSlot) {
			if (rankSlot < budget && score > scores[rankSlot]) {
				float oldScore = scores[rankSlot];
				uint oldCandidate = candidates[rankSlot];
				scores[rankSlot] = score;
				candidates[rankSlot] = candidate;
				score = oldScore;
				candidate = oldCandidate;
			}
		}
	}
	uint2 packed = WaterContactCandidates::Empty();
	[unroll] for (uint packSlot = 0; packSlot < 8; ++packSlot)
		WaterContactCandidates::Store(packed, packSlot, candidates[packSlot]);
	grid.pad0[0] = packed.x;
	grid.pad0[1] = packed.y;
	lightGrid[index] = grid;
}
