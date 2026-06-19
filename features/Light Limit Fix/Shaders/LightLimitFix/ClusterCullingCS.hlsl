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

RWStructuredBuffer<uint> lightIndexCounter : register(u0);
RWStructuredBuffer<uint> lightIndexList : register(u1);
RWStructuredBuffer<LightGrid> lightGrid : register(u2);
RWStructuredBuffer<uint> contactShadowIndexCounter : register(u3);
RWStructuredBuffer<uint> contactShadowIndexList : register(u4);
RWStructuredBuffer<LightGrid> contactShadowGrid : register(u5);

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

[numthreads(NUMTHREAD_X, NUMTHREAD_Y, NUMTHREAD_Z)] void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
	if (any(dispatchThreadId >= uint3(ClusterSize.x, ClusterSize.y, ClusterSize.z)))
		return;

	uint visibleLightCount = 0;
	uint visibleLightIndices[MAX_CLUSTER_LIGHTS];
	uint contactShadowLightCount = 0;
	uint contactShadowParticleCount = 0;
	uint contactShadowLightIndices[MAX_CONTACT_SHADOW_LIGHTS];
	float contactShadowLightScores[MAX_CONTACT_SHADOW_LIGHTS];
	uint contactShadowParticles[MAX_CONTACT_SHADOW_LIGHTS];

	uint clusterIndex = dispatchThreadId.x +
	                    dispatchThreadId.y * ClusterSize.x +
	                    dispatchThreadId.z * (ClusterSize.x * ClusterSize.y);

	ClusterAABB cluster = clusters[clusterIndex];
	const uint contactShadowClusterBudget = min(ContactShadowParams::ClusterBudget(ContactShadowParamsPacked), MAX_CONTACT_SHADOW_LIGHTS);
	const uint contactShadowParticleBudget = ContactShadowParams::ParticleBudget(ContactShadowParamsPacked);
	const bool contactShadowsEnabled = ContactShadowFlagsPacked != 0 && contactShadowClusterBudget > 0;

	for (uint i = 0; i < LightCount; i++) {
		Light light = lights[i];

		float radiusSquared = light.radius * light.radius;
		bool isVisible = false;
		bool reachedVisibleLightLimit = false;

		float3 positionVS = FrameBuffer::WorldToView(light.positionWS.xyz, true);

		[branch] if (LightIntersectsCluster(positionVS, radiusSquared, cluster))
		{
			isVisible = true;
			visibleLightIndices[visibleLightCount] = i;
			visibleLightCount++;
			if (visibleLightCount >= MAX_CLUSTER_LIGHTS)
				reachedVisibleLightLimit = true;
		}

		if (contactShadowsEnabled && isVisible && IsContactShadowEligible(light, ContactShadowFlagsPacked, contactShadowParticleBudget)) {
			float contactShadowScore = GetContactShadowScore(light, positionVS, cluster);
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

		if (reachedVisibleLightLimit)
			break;
	}

	uint offset = 0;
	InterlockedAdd(lightIndexCounter[0], visibleLightCount, offset);

	for (uint j = 0; j < visibleLightCount; j++) {
		lightIndexList[offset + j] = visibleLightIndices[j];
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
function CullLights( L, C, G, I )
    Input: A set L of n lights.
    Input: A counter C of the current index into the global light index list.
    Input: A 2D grid G of index offset and count in the global light index list.
    Input: A list I of global light index list.
    Output: A 2D grid G with the current tiles offset and light count.
    Output: A list I with the current tiles overlapping light indices appended to it.

1.  let t be the index of the current tile  ; t is the 2D index of the tile.
2.  let i be a local light index list       ; i is a local light index list.
3.  let f <- Frustum(t)                     ; f is the frustum for the current tile.

4.  for l in L                      ; Iterate the lights in the light list.
5.      if Cull( l, f )             ; Cull the light against the tile frustum.
6.          AppendLight( l, i )     ; Append the light to the local light index list.

7.  c <- AtomicInc( C, i.count )    ; Atomically increment the current index of the
                                    ; global light index list by the number of lights
                                    ; overlapping the current tile and store the
                                    ; original index in c.

8.  G(t) <- ( c, i.count )          ; Store the offset and light count in the light grid.

9.  I(c) <- i                       ; Store the local light index list into the global
                                    ; light index list.
*/
