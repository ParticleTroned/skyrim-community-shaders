#ifndef __LLF_COMMON_DEPENDENCY_HLSL__
#define __LLF_COMMON_DEPENDENCY_HLSL__

#define NUMTHREAD_X 16
#define NUMTHREAD_Y 16
#define NUMTHREAD_Z 4
#define GROUP_SIZE (NUMTHREAD_X * NUMTHREAD_Y * NUMTHREAD_Z)
// Per-cluster visible-light cap. Must match CLUSTER_MAX_LIGHTS in
// src/Features/LightLimitFix.cpp because the CPU sizes lightIndexList as
// clusterCount * CLUSTER_MAX_LIGHTS.
#define MAX_CLUSTER_LIGHTS 256
#define MAX_CONTACT_SHADOW_LIGHTS 8

namespace ContactShadowFlags
{
	static const uint Point = (1u << 0);
	static const uint Particle = (1u << 1);
}

namespace LightFlags
{
	static const uint PortalStrict = (1 << 0);
	static const uint Shadow = (1 << 1);
	static const uint Simple = (1 << 2);

	static const uint Initialised = (1 << 8);
	static const uint Disabled = (1 << 9);
	static const uint InverseSquare = (1 << 10);
	static const uint Linear = (1 << 11);
	static const uint Particle = (1 << 12);
}

struct ClusterAABB
{
	float4 minPoint;
	float4 maxPoint;
};

struct LightGrid
{
	uint offset;
	uint lightCount;
	uint pad0[2];
};

namespace ContactShadowParams
{
	uint Quality(uint params)
	{
		return params & 0xFFu;
	}

	uint ParticleBudget(uint params)
	{
		return (params >> 8) & 0xFFu;
	}

	uint ClusterBudget(uint params)
	{
		return (params >> 16) & 0xFFu;
	}

	uint StrictBudget(uint params)
	{
		return (params >> 24) & 0xFFu;
	}
}

struct Light
{
	float3 color;
	float fade;
	float radius;
	float invRadius;
	float fadeZone;
	float sizeBias;
	float4 positionWS;
	uint4 roomFlags;
	uint lightFlags;
	uint shadowLightIndex;
	uint pad0;
	uint pad1;
};

#endif  //__LLF_COMMON_DEPENDENCY_HLSL__
