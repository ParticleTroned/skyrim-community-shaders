#include "Common/FrameBuffer.hlsli"
#include "LightLimitFix/Common.hlsli"

cbuffer PerFrame : register(b0)
{
	uint LightCount;
	uint ContactShadowFlagsPacked;
	uint ContactShadowParamsPacked;
	uint pad0;
	uint4 ClusterSize;
}

//references
//https://github.com/pezcode/Cluster

StructuredBuffer<ClusterAABB> clusters : register(t0);
StructuredBuffer<Light> lights : register(t1);

RWStructuredBuffer<uint> lightIndexList : register(u1);
RWStructuredBuffer<LightGrid> lightGrid : register(u2);
RWStructuredBuffer<uint> contactShadowIndexCounter : register(u3);
RWStructuredBuffer<uint> contactShadowIndexList : register(u4);
RWStructuredBuffer<LightGrid> contactShadowGrid : register(u5);

groupshared float4 sharedLightsVS[GROUP_SIZE];
#if defined(VR)
groupshared float3 sharedLightsVSRight[GROUP_SIZE];
#endif

bool LightIntersectsCluster(float3 position, float radiusSquared, ClusterAABB cluster)
{
	float3 closest = max(cluster.minPoint.xyz, min(position, cluster.maxPoint.xyz));

	float3 dist = closest - position;
	return dot(dist, dist) <= radiusSquared;
}

float GetContactShadowScore(Light light, float3 positionVS, ClusterAABB cluster)
{
	float3 closest = max(cluster.minPoint.xyz, min(positionVS, cluster.maxPoint.xyz));
	float3 delta = closest - positionVS;
	float radiusSquared = max(light.radius * light.radius, 1.0);
	float distanceWeight = saturate(1.0 - dot(delta, delta) / radiusSquared);
	float luminance = dot(abs(light.color), float3(0.2126, 0.7152, 0.0722)) * max(light.fade, 0.0);

	if ((light.lightFlags & LightFlags::Particle) != 0)
		luminance *= 0.5;

	return luminance * distanceWeight;
}

bool IsContactShadowEligible(Light light, uint contactShadowFlags, uint particleBudget)
{
	const bool isParticle = (light.lightFlags & LightFlags::Particle) != 0;

	if (isParticle)
		return (contactShadowFlags & ContactShadowFlags::Particle) != 0 && particleBudget > 0;

	if ((light.lightFlags & LightFlags::Simple) != 0)
		return false;

	return (contactShadowFlags & ContactShadowFlags::Point) != 0;
}

void InsertContactShadowCandidate(
	uint lightIndex,
	float score,
	bool isParticle,
	uint candidateBudget,
	uint particleBudget,
	inout uint candidateCount,
	inout uint particleCount,
	inout uint indices[MAX_CONTACT_SHADOW_LIGHTS],
	inout float scores[MAX_CONTACT_SHADOW_LIGHTS],
	inout uint particles[MAX_CONTACT_SHADOW_LIGHTS])
{
	if (score <= 1e-5 || candidateBudget == 0)
		return;

	candidateBudget = min(candidateBudget, MAX_CONTACT_SHADOW_LIGHTS);
	particleBudget = min(particleBudget, candidateBudget);

	if (isParticle && particleBudget == 0)
		return;

	if (isParticle && particleCount >= particleBudget) {
		uint weakestParticle = MAX_CONTACT_SHADOW_LIGHTS;
		float weakestParticleScore = score;
		for (uint i = 0; i < candidateCount; ++i) {
			if (particles[i] != 0 && scores[i] < weakestParticleScore) {
				weakestParticle = i;
				weakestParticleScore = scores[i];
			}
		}

		if (weakestParticle == MAX_CONTACT_SHADOW_LIGHTS)
			return;

		indices[weakestParticle] = lightIndex;
		scores[weakestParticle] = score;
		return;
	}

	if (candidateCount < candidateBudget) {
		indices[candidateCount] = lightIndex;
		scores[candidateCount] = score;
		particles[candidateCount] = isParticle ? 1 : 0;
		candidateCount++;
		if (isParticle)
			particleCount++;
		return;
	}

	uint weakest = 0;
	float weakestScore = scores[0];
	for (uint i = 1; i < candidateCount; ++i) {
		if (scores[i] < weakestScore) {
			weakest = i;
			weakestScore = scores[i];
		}
	}

	if (score <= weakestScore)
		return;

	if (particles[weakest] != 0)
		particleCount--;

	indices[weakest] = lightIndex;
	scores[weakest] = score;
	particles[weakest] = isParticle ? 1 : 0;
	if (isParticle)
		particleCount++;
}

[numthreads(NUMTHREAD_X, NUMTHREAD_Y, NUMTHREAD_Z)] void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
	const uint lightCount = min(LightCount, GROUP_SIZE);
	if (groupIndex < lightCount) {
		Light light = lights[groupIndex];
		sharedLightsVS[groupIndex] = float4(FrameBuffer::WorldToView(light.positionWS[0].xyz, true, 0), light.radius * light.radius);
#if defined(VR)
		sharedLightsVSRight[groupIndex] = FrameBuffer::WorldToView(light.positionWS[1].xyz, true, 1);
#endif
	}
	// Partial edge groups must reach the barrier before inactive lanes leave.
	GroupMemoryBarrierWithGroupSync();

	if (any(dispatchThreadId >= uint3(ClusterSize.x, ClusterSize.y, ClusterSize.z)))
		return;

	uint visibleLightCount = 0;
	uint contactShadowLightCount = 0;
	uint contactShadowParticleCount = 0;
	uint contactShadowLightIndices[MAX_CONTACT_SHADOW_LIGHTS];
	float contactShadowLightScores[MAX_CONTACT_SHADOW_LIGHTS];
	uint contactShadowParticles[MAX_CONTACT_SHADOW_LIGHTS];

	uint clusterIndex = dispatchThreadId.x +
	                    dispatchThreadId.y * ClusterSize.x +
	                    dispatchThreadId.z * (ClusterSize.x * ClusterSize.y);

	// The CPU reserves MAX_CLUSTER_LIGHTS entries per cluster.
	uint offset = clusterIndex * MAX_CLUSTER_LIGHTS;
	ClusterAABB cluster = clusters[clusterIndex];
	const uint contactShadowClusterBudget = min(ContactShadowParams::ClusterBudget(ContactShadowParamsPacked), MAX_CONTACT_SHADOW_LIGHTS);
	const uint contactShadowParticleBudget = ContactShadowParams::ParticleBudget(ContactShadowParamsPacked);
	const bool contactShadowsEnabled = ContactShadowFlagsPacked != 0 && contactShadowClusterBudget > 0;

	for (uint i = 0; i < lightCount; i++) {
		float4 lightVS = sharedLightsVS[i];
		float radiusSquared = lightVS.w;
		bool isVisible = false;
		bool reachedVisibleLightLimit = false;

#if defined(VR)
		float3 positionVSLeft = lightVS.xyz;
		float3 positionVSRight = sharedLightsVSRight[i];

		[branch] if (LightIntersectsCluster(positionVSLeft, radiusSquared, cluster) || LightIntersectsCluster(positionVSRight, radiusSquared, cluster))
		{
			isVisible = true;
#else
		float3 positionVS = lightVS.xyz;

		[branch] if (LightIntersectsCluster(positionVS, radiusSquared, cluster))
		{
			isVisible = true;
#endif
			lightIndexList[offset + visibleLightCount] = i;
			visibleLightCount++;
			if (visibleLightCount >= MAX_CLUSTER_LIGHTS)
				reachedVisibleLightLimit = true;
		}

		[branch] if (contactShadowsEnabled && isVisible)
		{
			Light light = lights[i];
			if (IsContactShadowEligible(light, ContactShadowFlagsPacked, contactShadowParticleBudget)) {
#if defined(VR)
				float contactShadowScore = max(GetContactShadowScore(light, positionVSLeft, cluster), GetContactShadowScore(light, positionVSRight, cluster));
#else
				float contactShadowScore = GetContactShadowScore(light, positionVS, cluster);
#endif
				InsertContactShadowCandidate(
					i,
					contactShadowScore,
					(light.lightFlags & LightFlags::Particle) != 0,
					contactShadowClusterBudget,
					contactShadowParticleBudget,
					contactShadowLightCount,
					contactShadowParticleCount,
					contactShadowLightIndices,
					contactShadowLightScores,
					contactShadowParticles);
			}
		}

		if (reachedVisibleLightLimit)
			break;
	}

	LightGrid output = {
		offset, visibleLightCount, 0, 0
	};

	lightGrid[clusterIndex] = output;

	uint contactShadowOffset = 0;
	if (contactShadowLightCount > 0) {
		InterlockedAdd(contactShadowIndexCounter[0], contactShadowLightCount, contactShadowOffset);

		for (uint k = 0; k < contactShadowLightCount; k++) {
			contactShadowIndexList[contactShadowOffset + k] = contactShadowLightIndices[k];
		}
	}

	LightGrid contactShadowOutput = {
		contactShadowOffset, contactShadowLightCount, 0, 0
	};

	contactShadowGrid[clusterIndex] = contactShadowOutput;
}

//https://www.3dgep.com/forward-plus/#Grid_Frustums_Compute_Shader
/*
function CullLights( L, G, I )
    Input: A set L of n lights.
    Input: A 2D grid G of index offset and count in the global light index list.
    Input: A list I of global light index list.
    Output: A 2D grid G with the current tiles offset and light count.
    Output: A list I with the current tiles overlapping light indices appended to it.

1.  let t be the index of the current tile  ; t is the 2D index of the tile.
2.  let i be I(t * MAX_CLUSTER_LIGHTS)      ; The tile's output light list.
3.  let f <- Frustum(t)                     ; f is the frustum for the current tile.

4.  for l in L                      ; Iterate the lights in the light list.
5.      if Cull( l, f )             ; Cull the light against the tile frustum.
6.          AppendLight( l, i )     ; Append the light to the tile's output list.

7.  o <- t * MAX_CLUSTER_LIGHTS    ; The tile's fixed output offset.

8.  G(t) <- ( o, i.count )          ; Store the offset and light count in the light grid.

*/
