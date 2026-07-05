#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

#if defined(TERRAIN_SHADOWS)
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif

#if defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#elif defined(SKYLIGHTING)
// sh2 type is needed for the ExtractLighting overload that accepts a visibility SH
#	include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#endif

// Populated once per frame by Deferred::CopyShadowLightData from BSShadowDirectionalLight.
struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

#if defined(VOLUMETRIC_SHADOWS)
#	include "VolumetricShadows/VolumetricShadows.hlsli"
#endif
namespace ShadowSampling
{
	static const float MinDirectionalLightMultiplier = 1e-5;
	static const float3 LightingSampleNormal = float3(0, 0, 1);
	static const float3 ImageBasedLightingNormal = float3(0, 0, -1);

#if !defined(VOLUMETRIC_SHADOWS)
	Texture2DArray<float4> SharedShadowMap : register(t18);

	struct ShadowData
	{
		float4 VPOSOffset;
		float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
		float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
		float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
		float4 FocusShadowFadeParam;
		float4 DebugColor;
		float4 PropertyColor;
		float4 AlphaTestRef;
		float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
		float4x3 FocusShadowMapProj[4];
		// Since ShadowData is passed between c++ and hlsl, can't have different defines due to strong typing
		float4x3 ShadowMapProj[2][3];
		float4x4 CameraViewProjInverse[2];
	};

	StructuredBuffer<ShadowData> SharedShadowData : register(t19);
#endif

	bool HasDirectionalShadows()
	{
		return SharedData::HasDirectionalShadows != 0;
	}

	float GetShadowDepth(float3 positionWS, uint eyeIndex)
	{
		return SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(positionWS, eyeIndex));
	}

	float GetWorldShadow(float3 positionWS, float3 offset, uint eyeIndex);

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, uint eyeIndex, out float surfaceShadow)
	{
#if defined(VOLUMETRIC_SHADOWS)
		surfaceShadow = 1.0;
#	if defined(EFFECT)
		float viewRayLength = 128.0;
		float3 startPosition = positionWS - viewDirection * viewRayLength;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#	elif defined(UNDERWATER)
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS - viewDirection * viewRayLength;
#	else
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#	endif

		float totalRayLength = distance(endPosition, startPosition);

		const float stepSize = 32.0;
		uint sampleCount = clamp(uint(totalRayLength / stepSize + 0.5), 1, 4);
		float rcpSampleCount = rcp(sampleCount);

		float noise = Random::InterleavedGradientNoise(Stereo::EyeStableNoiseCoord(screenPosition, SharedData::BufferDim.xy), SharedData::FrameCount);

		float worldShadow = 0.0;
		for (uint i = 0; i < sampleCount; i++) {
			float t = (float(i) + noise) * rcpSampleCount;
			float3 sampledPositionWS = lerp(endPosition, startPosition, t);
			float worldShadowSample = GetWorldShadow(sampledPositionWS, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
			surfaceShadow = worldShadowSample;
			worldShadow += worldShadowSample;
		}

		if (worldShadow == 0.0 && surfaceShadow == 0.0)
			return 0.0;

		worldShadow *= rcpSampleCount;

		if (HasDirectionalShadows()) {
			float vsmSurfaceShadow = 1.0;
			float shadow = VolumetricShadows::GetVSMShadow3D(startPosition, endPosition, noise, sampleCount, eyeIndex, vsmSurfaceShadow);
			surfaceShadow *= vsmSurfaceShadow;
			return worldShadow * shadow;
		}

		return worldShadow;
#else
		surfaceShadow = 1.0;
		ShadowData sD = SharedShadowData[0];

		float fadeFactor = 1.0 - pow(saturate(dot(positionWS, positionWS) / sD.ShadowLightParam.z), 8);
		uint sampleCount = ceil(8.0 * (1.0 - saturate(length(positionWS) / sqrt(sD.ShadowLightParam.z))));

		if (sampleCount == 0)
			return 1.0;

		float rcpSampleCount = rcp((float)sampleCount);

		uint3 seed = Random::pcg3d(uint3(screenPosition.xy, screenPosition.x * Math::PI));

		float2 compareValue;
		compareValue.x = mul(transpose(sD.ShadowMapProj[eyeIndex][0]), float4(positionWS, 1)).z - 0.01;
		compareValue.y = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS, 1)).z - 0.01;

		float shadow = 0.0;
		if (sD.EndSplitDistances.z >= GetShadowDepth(positionWS, eyeIndex)) {
			for (uint i = 0; i < sampleCount; i++) {
				float3 rnd = Random::R3Modified(i + SharedData::FrameCount * sampleCount, seed / 4294967295.f);

				// https://stats.stackexchange.com/questions/8021/how-to-generate-uniformly-distributed-points-in-the-3-d-unit-ball
				float phi = rnd.x * Math::TAU;
				float cos_theta = rnd.y * 2 - 1;
				float sin_theta = sqrt(1 - cos_theta);
				float r = rnd.z;
				float4 sincos_phi;
				sincos(phi, sincos_phi.y, sincos_phi.x);
				float3 sampleOffset = viewDirection * (float(i) - float(sampleCount) * 0.5) * 64 * rcpSampleCount;
				sampleOffset += float3(r * sin_theta * sincos_phi.x, r * sin_theta * sincos_phi.y, r * cos_theta) * 64;

				uint cascadeIndex = sD.EndSplitDistances.x < GetShadowDepth(positionWS.xyz + viewDirection * (sampleOffset.x + sampleOffset.y), eyeIndex);  // Stochastic cascade sampling

				float3 positionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIndex]), float4(positionWS + sampleOffset, 1));

				float4 depths = SharedShadowMap.GatherRed(LinearSampler, float3(saturate(positionLS.xy), cascadeIndex), 0);
				shadow += dot(depths > compareValue[cascadeIndex], 0.25);
			}
		} else {
			shadow = 1.0;
		}

		surfaceShadow = lerp(1.0, shadow * rcpSampleCount, fadeFactor);
		return surfaceShadow;
#endif
	}

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, uint eyeIndex)
	{
		float surfaceShadow;
		return Get3DFilteredShadow(positionWS, viewDirection, screenPosition, eyeIndex, surfaceShadow);
	}

#if !defined(VOLUMETRIC_SHADOWS)
	float Get2DFilteredShadowCascade(float noise, float2x2 rotationMatrix, float sampleOffsetScale, float2 baseUV, float cascadeIndex, float compareValue, uint eyeIndex)
	{
		const uint sampleCount = 16;

		float layerIndexRcp = rcp(1 + cascadeIndex);

		float visibility = 0.0;

#	if defined(WATER)
		sampleOffsetScale *= 2.0;
#	endif

		for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
			float2 sampleOffset = mul(Random::PoissonSampleOffsets16[sampleIndex], rotationMatrix);

			float2 sampleUV = layerIndexRcp * sampleOffset * sampleOffsetScale + baseUV;

			float4 depths = SharedShadowMap.GatherRed(LinearSampler, float3(saturate(sampleUV), cascadeIndex), 0);
			visibility += dot(depths > compareValue, 0.25);
		}

		return visibility * rcp((float)sampleCount);
	}

	float Get2DFilteredShadow(float noise, float2x2 rotationMatrix, float3 positionWS, uint eyeIndex)
	{
		ShadowData sD = SharedShadowData[0];

		float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

		if (sD.EndSplitDistances.z >= shadowMapDepth) {
			float fadeFactor = 1 - pow(saturate(dot(positionWS.xyz, positionWS.xyz) / sD.ShadowLightParam.z), 8);

			float4x3 lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][0];
			float cascadeIndex = 0;

			if (sD.EndSplitDistances.x < shadowMapDepth) {
				lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][1];
				cascadeIndex = 1;
			}

			float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;

			float shadowVisibility = Get2DFilteredShadowCascade(noise, rotationMatrix, sD.ShadowSampleParam.z, positionLS.xy, cascadeIndex, positionLS.z, eyeIndex);

			if (cascadeIndex < 1 && sD.StartSplitDistances.y < shadowMapDepth) {
				float3 cascade1PositionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS.xyz, 1)).xyz;

				float cascade1ShadowVisibility = Get2DFilteredShadowCascade(noise, rotationMatrix, sD.ShadowSampleParam.z, cascade1PositionLS.xy, 1, cascade1PositionLS.z, eyeIndex);

				float cascade1BlendFactor = smoothstep(0, 1, (shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));
				shadowVisibility = lerp(shadowVisibility, cascade1ShadowVisibility, cascade1BlendFactor);
			}

			return lerp(1.0, shadowVisibility, fadeFactor);
		}

		return 1.0;
	}
#endif

	float GetWorldShadow(float3 positionWS, float3 offset, uint eyeIndex)
	{
		if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
			return 1.0;

		float worldShadow = 1.0;
#if defined(TERRAIN_SHADOWS)
		worldShadow = TerrainShadows::GetTerrainShadow(positionWS + offset, LinearSampler);
#endif

#if defined(CLOUD_SHADOWS)
		worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif

		return worldShadow;
	}

#if defined(VOLUMETRIC_SHADOWS)
	float GetLightingShadow(float3 worldPosition, uint eyeIndex, out float detailedShadow)
	{
		if (!HasDirectionalShadows()) {
			detailedShadow = 1.0;
			return 1.0;
		}

		return VolumetricShadows::GetVSMShadow2D(worldPosition, worldPosition + FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex, detailedShadow);
	}

	float GetLightingShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		float detailedShadow;
		return GetLightingShadow(worldPosition, eyeIndex, detailedShadow);
	}
#else
	float GetLightingShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);
		return Get2DFilteredShadow(noise, rotationMatrix, worldPosition, eyeIndex);
	}

	float GetWaterShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		float worldShadow = GetWorldShadow(worldPosition, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
		if (worldShadow != 0.0) {
			float2 rotation;
			sincos(Math::TAU * noise, rotation.y, rotation.x);
			float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);
			float shadow = Get2DFilteredShadow(noise, rotationMatrix, worldPosition, eyeIndex);
			return worldShadow * shadow;
		}

		return worldShadow;
	}
#endif

	float3 GetRawAmbientLighting(float3 normal)
	{
		return max(0, SharedData::GetAmbient(normal));
	}

	float3 GetAmbientLighting(float3 normal)
	{
		float3 ambientColor = GetRawAmbientLighting(normal);

#if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			ambientColor = ImageBasedLighting::GetDiffuseIBL(ambientColor, ImageBasedLightingNormal);
		}
#endif

		return ambientColor;
	}

#if defined(SKYLIGHTING) && !defined(INTERIOR)
	float3 GetAmbientLighting(float3 normal, float skylightingDiffuse)
	{
		float3 ambientColor = GetRawAmbientLighting(normal);

#	if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			ambientColor = ImageBasedLighting::GetDiffuseIBLOccluded(ambientColor, ImageBasedLightingNormal, skylightingDiffuse);
		}
#	endif

		return ambientColor;
	}
#endif

	float3 GetDirectionalLighting()
	{
		float llDirLightMult = (Color::UseLinearLightingColorAdjustments() && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
		return Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, MinDirectionalLightMultiplier), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;
	}

	float3 GetSceneLightingColor()
	{
		return GetAmbientLighting(LightingSampleNormal) + GetDirectionalLighting();
	}

#if defined(SKYLIGHTING) && !defined(INTERIOR)
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor, float skylightingDiffuse)
#else
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor)
#endif
	{
#if defined(SKYLIGHTING) && !defined(INTERIOR)
		float3 ambientColorAmb = GetAmbientLighting(LightingSampleNormal, skylightingDiffuse);
#else
		float3 ambientColorAmb = GetAmbientLighting(LightingSampleNormal);
#endif

		float3 dirLightColorDir = GetDirectionalLighting();

		float inputLuma = Color::RGBToLuminance(inputColor);
		float ambientLuma = Color::RGBToLuminance(ambientColorAmb);
		float dirLightLuma = Color::RGBToLuminance(dirLightColorDir);
		float totalLuma = ambientLuma + dirLightLuma;

		if (totalLuma > 0.0 && ambientLuma > 0.0)
			ambientColorAmb *= inputLuma / totalLuma;

		float3 dirLightColorAmb = max(0.0, inputColor - ambientColorAmb);

		dirColor = dirLightColorAmb;
		ambientColor = ambientColorAmb;
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__
