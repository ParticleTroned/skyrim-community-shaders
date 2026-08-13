#ifndef __SKYLIGHTING_DEPENDENCY_HLSL__
#define __SKYLIGHTING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace Skylighting
{
#if defined(SKYLIGHTING_PROBE_REGISTER)
	Texture3D<sh2> SkylightingProbeArray : register(SKYLIGHTING_PROBE_REGISTER);
#elif defined(PSHADER)
	Texture3D<sh2> SkylightingProbeArray : register(t50);
#endif

#if defined(SKYLIGHTING_SHADOW_VIS)
	Texture3D<float> ShadowVisibilityProbeArray : register(t53);
#endif

	const static sh2 UNIT_SH = float4(sqrt(4.0 * Math::PI), 0, 0, 0);

	const static uint3 ARRAY_DIM = uint3(256, 256, 128);
	const static float3 ARRAY_SIZE = 10000.f * float3(1, 1, 0.5);
	const static float3 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;

	bool IsEnabled()
	{
		return SharedData::skylightingSettings.Enabled != 0;
	}

	bool IsEnabled(SharedData::SkylightingSettings params)
	{
		return params.Enabled != 0;
	}

	sh2 GetUnoccludedProbe()
	{
		return UNIT_SH / 1e-10;
	}

	float GetFadeOutFactor(float3 positionMS)
	{
		if (!IsEnabled())
			return 0.0;

		float3 uvw = saturate(positionMS / ARRAY_SIZE + .5);
		float3 dists = min(uvw, 1 - uvw);
		float edgeDist = min(dists.x, min(dists.y, dists.z));
		return saturate(edgeDist * 20);
	}

	float MixDiffuse(float visibility)
	{
		return lerp(SharedData::skylightingSettings.MinDiffuseVisibility, 1.0, visibility);
	}

	float MixSpecular(float visibility)
	{
		return lerp(SharedData::skylightingSettings.MinSpecularVisibility, 1.0, visibility);
	}

	float CosineLobeVisibility(sh2 skylightingSH, float3 normal, float fadeOutFactor)
	{
		float visibility = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(normal)) / Math::PI;
		return lerp(1.0, saturate(visibility), fadeOutFactor);
	}

	float EvaluateDiffuse(sh2 skylightingSH, float3 normal, float fadeOutFactor = 1.0)
	{
		return MixDiffuse(CosineLobeVisibility(skylightingSH, normal, fadeOutFactor));
	}

	float EvaluateVisibility(sh2 skylightingSH)
	{
		return MixSpecular(CosineLobeVisibility(skylightingSH, float3(0, 0, 1), 1.0));
	}

	float EvaluateSpecular(sh2 skylightingSH, sh2 specularLobe, float fadeOutFactor = 1.0)
	{
		float visibility = SphericalHarmonics::FuncProductIntegral(skylightingSH, specularLobe);
		visibility = lerp(1.0, saturate(visibility), fadeOutFactor);
		return MixSpecular(visibility);
	}

#if defined(PSHADER)
	void ApplySkylighting(inout float3 diffuseColor, inout float3 directionalAmbientColor, float3 albedo, float skylightingDiffuse)
	{
		if (!IsEnabled())
			return;

		float maxScale = 1.0;
		if (directionalAmbientColor.x > 0.0)
			maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
		if (directionalAmbientColor.y > 0.0)
			maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
		if (directionalAmbientColor.z > 0.0)
			maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
		directionalAmbientColor *= maxScale;

		diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);

		float3 linAmbient = Color::IrradianceToLinear(directionalAmbientColor);
		float3 multiBounceSkylighting = MultiBounceAO(albedo, skylightingDiffuse);
		directionalAmbientColor = Color::IrradianceToGamma(linAmbient * multiBounceSkylighting);

		diffuseColor += directionalAmbientColor;
	}
#endif

#if defined(PSHADER) || defined(SKYLIGHTING_PROBE_REGISTER)
	sh2 Sample(float3 positionMS, float3 normalWS)
	{
		sh2 unoccludedProbe = GetUnoccludedProbe();

		if (!IsEnabled() || SharedData::InInterior)
			return unoccludedProbe;

		positionMS.xyz += normalWS * CELL_SIZE * 0.5;  // Receiver normal bias

		float3 positionMSAdjusted = positionMS - SharedData::skylightingSettings.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

		if (any(uvw < 0) || any(uvw > 1))
			return unoccludedProbe;

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;

		sh2 sum = 0;
		float wsum = 0;
		for (int i = 0; i < 2; i++)
			for (int j = 0; j < 2; j++)
				for (int k = 0; k < 2; k++) {
					int3 offset = int3(i, j, k);
					int3 cellID = cell000 + offset;

					if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
						continue;

					float3 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
					cellCentreMS = cellCentreMS * CELL_SIZE;

					// https://handmade.network/p/75/monter/blog/p/7288-engine_work__global_illumination_with_irradiance_probes
					// basic tangent checks
					float tangentWeight = dot(normalize(cellCentreMS - positionMSAdjusted), normalWS) * 0.5 + 0.5;

					float3 trilinearWeights = 1 - abs(offset - trilinearPos);
					float w = trilinearWeights.x * trilinearWeights.y * trilinearWeights.z * tangentWeight;

					uint3 cellTexID = (cellID + SharedData::skylightingSettings.ArrayOrigin.xyz) % ARRAY_DIM;
					sh2 probe = SphericalHarmonics::Scale(SkylightingProbeArray[cellTexID], w);

					sum = SphericalHarmonics::Add(sum, probe);
					wsum += w;
				}

		return SphericalHarmonics::Scale(sum, rcp(wsum + EPSILON_WEIGHT_SUM));
	}

#	if defined(SKYLIGHTING_SHADOW_VIS)
	sh2 Sample(float3 positionMS, float3 normalWS, out float shadowVisibility)
	{
		sh2 unoccludedProbe = GetUnoccludedProbe();
		shadowVisibility = 1.0;

		// Preserve the existing two-argument sampling cost when the current frame
		// has no valid directional-shadow inputs. Callers retain their old shadow
		// fallback and do not read a stale visibility volume.
		if (SharedData::skylightingSettings.ShadowDataAvailable == 0)
			return Sample(positionMS, normalWS);

		if (!IsEnabled() || SharedData::InInterior)
			return unoccludedProbe;

		positionMS.xyz += normalWS * CELL_SIZE * 0.5;
		float3 positionMSAdjusted = positionMS - SharedData::skylightingSettings.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;
		if (any(uvw < 0) || any(uvw > 1))
			return unoccludedProbe;

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;
		sh2 shSum = 0;
		float shWeightSum = 0;
		float shadowSum = 0;
		float shadowWeightSum = 0;

		for (int i = 0; i < 2; i++)
			for (int j = 0; j < 2; j++)
				for (int k = 0; k < 2; k++) {
					int3 cellOffset = int3(i, j, k);
					int3 cellID = cell000 + cellOffset;
					if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
						continue;

					float3 trilinearWeights = 1 - abs(cellOffset - trilinearPos);
					float triWeight = trilinearWeights.x * trilinearWeights.y * trilinearWeights.z;
					uint3 cellTexID = (cellID + SharedData::skylightingSettings.ArrayOrigin.xyz) % ARRAY_DIM;
					float3 cellCentreMS = (cellID + 0.5 - ARRAY_DIM / 2) * CELL_SIZE;
					float tangentWeight = dot(normalize(cellCentreMS - positionMSAdjusted), normalWS) * 0.5 + 0.5;
					float shWeight = triWeight * tangentWeight;

					shSum = SphericalHarmonics::Add(shSum, SphericalHarmonics::Scale(SkylightingProbeArray[cellTexID], shWeight));
					shWeightSum += shWeight;
					shadowSum += ShadowVisibilityProbeArray[cellTexID] * triWeight;
					shadowWeightSum += triWeight;
				}

		shadowVisibility = lerp(1.0, shadowSum / max(shadowWeightSum, EPSILON_WEIGHT_SUM), GetFadeOutFactor(positionMS));
		return SphericalHarmonics::Scale(shSum, rcp(shWeightSum + EPSILON_WEIGHT_SUM));
	}
#	endif

	float GetSkylightingDiffuse(sh2 skylightingSH, float3 positionMS, float3 evalNormal, float vertexAO = 1.0)
	{
		if (!IsEnabled() || SharedData::InInterior)
			return 1.0;

		float3 candidateNormal = float3(evalNormal.xy, max(0.0, evalNormal.z));
		float3 biasedNormal = dot(candidateNormal, candidateNormal) > 1e-6 ? normalize(candidateNormal) : float3(0, 0, 1);
		float skylightingDiffuse = EvaluateDiffuse(skylightingSH, biasedNormal, GetFadeOutFactor(positionMS));
		return saturate(skylightingDiffuse / max(vertexAO, EPSILON_DIVISION));
	}

	// Compute skylighting diffuse for a receiver biased to face upward (grass/foliage).
	// The result is pre-divided by vertexAO so that a subsequent multiply by vertexAO
	// yields min(skylightingDiffuse, vertexAO). Pass vertexAO = 1 to skip this compensation.
	float GetVertexSkylightingDiffuse(float3 positionMS, float3 normalWS, float vertexAO)
	{
		if (!IsEnabled() || SharedData::InInterior)
			return 1.0;

		sh2 skylightingSH = Sample(positionMS, normalWS);
		return GetSkylightingDiffuse(skylightingSH, positionMS, normalWS, vertexAO);
	}

	sh2 SampleNoBias(float3 positionMS)
	{
		sh2 unoccludedProbe = GetUnoccludedProbe();

		if (!IsEnabled() || SharedData::InInterior)
			return unoccludedProbe;

		float3 positionMSAdjusted = positionMS - SharedData::skylightingSettings.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

		if (any(uvw < 0) || any(uvw > 1))
			return unoccludedProbe;

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;

		sh2 sum = 0;
		float wsum = 0;
		[unroll] for (int i = 0; i < 2; i++)
			[unroll] for (int j = 0; j < 2; j++)
				[unroll] for (int k = 0; k < 2; k++)
		{
			int3 offset = int3(i, j, k);
			int3 cellID = cell000 + offset;

			if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
				continue;

			float3 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
			cellCentreMS = cellCentreMS * CELL_SIZE;

			float3 trilinearWeights = 1 - abs(offset - trilinearPos);
			float w = trilinearWeights.x * trilinearWeights.y * trilinearWeights.z;

			uint3 cellTexID = (cellID + SharedData::skylightingSettings.ArrayOrigin.xyz) % ARRAY_DIM;
			sh2 probe = SphericalHarmonics::Scale(SkylightingProbeArray[cellTexID], w);

			sum = SphericalHarmonics::Add(sum, probe);
			wsum += w;
		}

		return SphericalHarmonics::Scale(sum, rcp(wsum + EPSILON_WEIGHT_SUM));
	}
#endif
}

#endif
