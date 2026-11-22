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

// Global light indices fit within GROUP_SIZE; membership belongs to each cluster entry.
static const uint ClusteredContactShadowMemberBit = 0x80000000u;

uint DecodeClusteredLightIndex(uint entry)
{
	return entry & ~ClusteredContactShadowMemberBit;
}

bool HasContactShadowMembership(uint entry)
{
	return (entry & ClusteredContactShadowMemberBit) != 0u;
}

namespace ContactShadowFlags
{
	static const uint Point = (1u << 0);
	static const uint Particle = (1u << 1);
}

#include "Common/PointLightFlags.hlsli"

namespace LightFlags
{
	static const uint PortalStrict = POINT_LIGHT_FLAG_PORTAL_STRICT;
	static const uint Shadow = POINT_LIGHT_FLAG_SHADOW;
	static const uint Simple = POINT_LIGHT_FLAG_SIMPLE;
	static const uint AffectWater = POINT_LIGHT_FLAG_AFFECT_WATER;

	static const uint Initialised = POINT_LIGHT_FLAG_INITIALISED;
	static const uint Disabled = POINT_LIGHT_FLAG_DISABLED;
	static const uint InverseSquare = POINT_LIGHT_FLAG_INVERSE_SQUARE;
	static const uint Linear = POINT_LIGHT_FLAG_LINEAR;
	static const uint Particle = POINT_LIGHT_FLAG_PARTICLE;
	static const uint Spot = POINT_LIGHT_FLAG_SPOT;
	static const uint OmniDirectional = POINT_LIGHT_FLAG_OMNIDIRECTIONAL;
}

namespace ClusteredLightFlags
{
	static const uint Available = 1u;
	static const uint PreparedColours = 2u;
}

struct PreparedPointLightColour
{
	float4 nonPBR;
	float4 pbr;
};

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
	float4 positionWS[2];
	uint4 roomFlags;
	uint lightFlags;
	uint shadowLightIndex;
	uint effectSpotlightIndex;
	float effectSpotlightHalfFalloff;
};
#endif  //__LLF_COMMON_DEPENDENCY_HLSL__
