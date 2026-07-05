#ifndef __VOLUMETRIC_SHADOWS_HLSLI__
#define __VOLUMETRIC_SHADOWS_HLSLI__

// Variance Shadow Maps (VSM)
// Chebyshev's inequality on filtered depth moments.

namespace VolumetricShadows
{
	Texture2D<float2> SharedShadowMap : register(t18);

	static const float VSM_MIN_VARIANCE = 0.00001;
	static const float VSM_BLEEDING_REDUCTION = 0.2;

	float ComputeVSM(float2 moments, float depth)
	{
		float variance = max(moments.y - moments.x * moments.x, VSM_MIN_VARIANCE);
		float d = depth - moments.x;
		float pMax = variance / (variance + d * d);
		return (depth <= moments.x) ? 1.0 : pMax;
	}

	float ReduceBleeding(float shadow, float amount)
	{
		return saturate((shadow - amount) / (1.0 - amount));
	}

	float GetCascadeFade(float shadowMapDepth, float maxDistance)
	{
		return saturate(shadowMapDepth / max(maxDistance, 1e-4));
	}

	void SelectCascade(float shadowMapDepth, in DirectionalShadowLightData directionalShadowLightData, out uint primaryCascade, out bool needsBlending, out float blendFactor)
	{
		float blendSpan = directionalShadowLightData.EndSplitDistances.x - directionalShadowLightData.StartSplitDistances.y;
		if (blendSpan > 1e-4) {
			float cascadeSelect = saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / blendSpan);
			primaryCascade = uint(cascadeSelect);
			needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);
			blendFactor = smoothstep(0, 1, cascadeSelect);
			return;
		}

		primaryCascade = shadowMapDepth >= directionalShadowLightData.EndSplitDistances.x ? 1u : 0u;
		needsBlending = false;
		blendFactor = 0.0;
	}

	float SampleVSMCascade3D(
		uint cascadeIndex,
		float noise,
		uint sampleCount,
		float rcpSampleCount,
		float3 startPositionLS,
		float3 endPositionLS,
		out float firstSample)
	{
		float shadow = 0.0;
		firstSample = 1.0;

		[loop] for (uint k = 0; k < sampleCount; k++)
		{
			float t = (float(k) + noise) * rcpSampleCount;
			float3 samplePosLS = lerp(endPositionLS, startPositionLS, t);

			float2 moments = SharedShadowMap.SampleLevel(LinearSampler, samplePosLS.xy, 1u - cascadeIndex);
			float lit = ComputeVSM(moments, samplePosLS.z);

			// Last to set firstSample is start position.
			firstSample = lit;

			shadow += lit;
		}

		return shadow * rcpSampleCount;
	}

	float GetVSMShadow3D(float3 startPosition, float3 endPosition, float noise, uint baseSampleCount, uint eyeIndex, out float surfaceShadow)
	{
		DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];

		// View-space z matches the linear cascade split distances from BSShadowDirectionalLight.
		float3 midPosition = (startPosition + endPosition) * 0.5;
		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(midPosition, eyeIndex));

		// Cascade projections are world-space; positions come in camera-relative.
		startPosition += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
		endPosition += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

		if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y) {
			surfaceShadow = 1.0;
			return 1.0;
		}

		float fade = GetCascadeFade(shadowMapDepth, directionalShadowLightData.EndSplitDistances.y);

		uint sampleCount = max(1, ceil(float(baseSampleCount) * (1.0 - fade)));
		float rcpSampleCount = rcp(sampleCount);

		uint primaryCascade;
		bool needsBlending;
		float blendFactor;
		SelectCascade(shadowMapDepth, directionalShadowLightData, primaryCascade, needsBlending, blendFactor);

		float4x4 shadowProj = directionalShadowLightData.ShadowProj[primaryCascade];
		float3 startLS = mul(shadowProj, float4(startPosition, 1)).xyz;
		float3 endLS = mul(shadowProj, float4(endPosition, 1)).xyz;
		startLS.xy = saturate(startLS.xy);
		endLS.xy = saturate(endLS.xy);

		float primaryFirstSample;
		float shadow = SampleVSMCascade3D(primaryCascade, noise, sampleCount, rcpSampleCount, startLS, endLS, primaryFirstSample);
		surfaceShadow = primaryFirstSample;

		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			shadowProj = directionalShadowLightData.ShadowProj[secondaryCascade];
			startLS = mul(shadowProj, float4(startPosition, 1)).xyz;
			endLS = mul(shadowProj, float4(endPosition, 1)).xyz;
			startLS.xy = saturate(startLS.xy);
			endLS.xy = saturate(endLS.xy);

			float secondaryFirstSample;
			float shadowBlend = SampleVSMCascade3D(secondaryCascade, noise, sampleCount, rcpSampleCount, startLS, endLS, secondaryFirstSample);
			shadow = lerp(shadow, shadowBlend, blendFactor);
			surfaceShadow = lerp(surfaceShadow, secondaryFirstSample, blendFactor);
		}

		float fadeFactor = 1.0 - pow(fade * fade, 8);
		surfaceShadow = lerp(1.0, surfaceShadow, fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}

	float SampleVSMCascade2D(uint cascadeIndex, float3 positionLS)
	{
		float2 moments = SharedShadowMap.SampleLevel(LinearSampler, positionLS.xy, 1u - cascadeIndex);
		return ComputeVSM(moments, positionLS.z);
	}

	float GetVSMShadow2D(float3 position, float3 positionWS, uint eyeIndex, out float detailedShadow)
	{
		DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];

		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(position, eyeIndex));

		if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y) {
			detailedShadow = 1.0;
			return 1.0;
		}

		float fade = GetCascadeFade(shadowMapDepth, directionalShadowLightData.EndSplitDistances.y);
		uint primaryCascade;
		bool needsBlending;
		float blendFactor;
		SelectCascade(shadowMapDepth, directionalShadowLightData, primaryCascade, needsBlending, blendFactor);

		float3 positionLS = mul(directionalShadowLightData.ShadowProj[primaryCascade], float4(positionWS, 1)).xyz;
		positionLS.xy = saturate(positionLS.xy);

		float shadow = SampleVSMCascade2D(primaryCascade, positionLS);

		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			positionLS = mul(directionalShadowLightData.ShadowProj[secondaryCascade], float4(positionWS, 1)).xyz;
			positionLS.xy = saturate(positionLS.xy);

			float shadowBlend = SampleVSMCascade2D(secondaryCascade, positionLS);
			shadow = lerp(shadow, shadowBlend, blendFactor);
		}

		float fadeFactor = 1.0 - pow(fade * fade, 8);
		detailedShadow = lerp(1.0, ReduceBleeding(shadow, VSM_BLEEDING_REDUCTION), fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}
}

#endif  // __VOLUMETRIC_SHADOWS_HLSLI__
