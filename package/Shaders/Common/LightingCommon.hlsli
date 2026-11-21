#ifndef LIGHTING_COMMON_HLSLI
#define LIGHTING_COMMON_HLSLI

#include "Common/Math.hlsli"

#if defined(HAIR) && defined(CS_HAIR) && !defined(TRUE_PBR)
#	define CS_HAIR_SHADING
#endif

#if defined(SKIN) && defined(CS_SKIN) && !defined(TRUE_PBR)
#	define CS_SKIN_SHADING
#endif

struct DirectContext
{
	float3 worldNormal;
	float3 vertexNormal;
	float3 viewDir;
	float3 lightDir;
	float3 halfVector;
	float3 lightColor;
	float detailedShadow;
	float softShadow;
#if defined(TRUE_PBR)
	float3 coatWorldNormal;
	float3 coatViewDir;
	float3 coatLightDir;
	float3 coatHalfVector;
	float3 coatLightColor;
#elif defined(CS_HAIR_SHADING)
	float hairShadow;
#endif
};

struct IndirectContext
{
	float3 worldNormal;
	float3 vertexNormal;
	float3 viewDir;
};

struct DirectLightingOutput
{
	float3 diffuse;
	float3 specular;
	float3 transmission;
#if defined(TRUE_PBR)
	float3 coatDiffuse;
#endif
};

struct IndirectLobeWeights
{
	float3 diffuse;
	float3 specular;
};

#if defined(TRUE_PBR)
#	if defined(GLINT)
#		include "Common/Glints/Glints2023.hlsli"
#	else
namespace Glints
{
	typedef float GlintCachedVars;
}
#	endif
#endif

struct MaterialProperties
{
	float3 BaseColor;
#if !defined(TRUE_PBR)
	float Shininess;
	float Glossiness;
	float3 SpecularColor;
	float3 rimSoftLightColor;
	float3 backLightColor;
	float Roughness;
	float3 F0;
#	if defined(CS_SKIN_SHADING)
	float RoughnessSecondary;
	float SecondarySpecIntensity;
	float Thickness;
	float AO;
	float FuzzWeight;
	float FuzzRoughness;
	float3 FuzzColor;
	float3 DirectSpecularEnergyCompensation;
#	endif
#else
	float Roughness;
	float Metallic;
	float AO;
	float3 F0;
	float3 SubsurfaceColor;
	float Thickness;
#	if defined(SNOW_COVER) && (defined(TREE_ANIM) || defined(PBR_GRASS))
	// Zero-initialized materials retain their current foliage colour without snow.
	float FoliageSnowCoverage;
	float3 FoliageBaseColor;
#	endif
	float3 CoatColor;
	float CoatStrength;
	float CoatRoughness;
	float3 CoatF0;
	float3 FuzzColor;
	float FuzzWeight;
	float GlintScreenSpaceScale;
	float GlintLogMicrofacetDensity;
	float GlintMicrofacetRoughness;
	float GlintDensityRandomization;
	Glints::GlintCachedVars GlintCache;
	float Noise;
#endif
};

namespace Foliage
{
	namespace Constants
	{
		static const float Wrap = 0.5f;
		static const float ScatterRoughness = 0.6f;
		static const float Pi = 3.14159265f;
	}
}

// Cheap view-dependent foliage transmission shared by PBR and vanilla foliage paths.
float GetFoliageTransmission(float NdotL, float VdotL)
{
	const float wrapDenominator = (1.0f + Foliage::Constants::Wrap) * (1.0f + Foliage::Constants::Wrap);
	float wrappedBacklight = saturate((-NdotL + Foliage::Constants::Wrap) / wrapDenominator);

	float forwardScatter = saturate(-VdotL);
	const float scatterAlpha2 = Foliage::Constants::ScatterRoughness * Foliage::Constants::ScatterRoughness;
	float scatterDenominator = forwardScatter * forwardScatter * (scatterAlpha2 - 1.0f) + 1.0f;
	float scatter = scatterAlpha2 / (Foliage::Constants::Pi * scatterDenominator * scatterDenominator);

	return wrappedBacklight * scatter;
}

float ShininessToRoughness(float shininess)
{
	return pow(abs(2.0 / (shininess + 2.0)), 0.25);
}

float3 SafeNormalizeLighting(float3 v, float3 fallback, float minimumLengthSq = EPSILON_DIVISION)
{
	float lenSq = dot(v, v);
	return (lenSq > minimumLengthSq && lenSq == lenSq && lenSq < 1.0e16) ? v * rsqrt(lenSq) : fallback;
}

/// Reconstructs the existing independently projected axes from surface derivatives.
float3x3 ReconstructTBN(float3 dFdx, float3 dFdy, float3 worldNormal, float2 dUVdx, float2 dUVdy)
{
	float3 normal = SafeNormalizeLighting(worldNormal, float3(0, 0, 1), EPSILON_LENGTH_SQ);
	// Screen-space derivatives can be much smaller than unit lighting vectors.
	float3 tangent = SafeNormalizeLighting(dFdx * dUVdy.y - dFdy * dUVdx.y, 0, EPSILON_LENGTH_SQ);
	float3 bitangent = SafeNormalizeLighting(dFdy * dUVdx.x - dFdx * dUVdy.x, 0, EPSILON_LENGTH_SQ);
	tangent = SafeNormalizeLighting(tangent - worldNormal * dot(worldNormal, tangent), 0, EPSILON_LENGTH_SQ);
	bitangent = SafeNormalizeLighting(bitangent - worldNormal * dot(worldNormal, bitangent), 0, EPSILON_LENGTH_SQ);

	[branch] if (abs(dot(cross(tangent, bitangent), normal)) <= EPSILON_LENGTH_SQ)
	{
		float3 axis = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(0, 1, 0);
		tangent = normalize(cross(axis, normal));
		bitangent = cross(normal, tangent);
	}
	return float3x3(tangent, bitangent, normal);
}

float3x3 ReconstructTBN(float3 worldPos, float3 worldNormal, float2 uv)
{
	return ReconstructTBN(ddx(worldPos), ddy(worldPos), worldNormal, ddx(uv), ddy(uv));
}
#endif
