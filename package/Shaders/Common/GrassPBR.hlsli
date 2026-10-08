#ifndef CSX_GRASS_PBR_HLSLI
#define CSX_GRASS_PBR_HLSLI

cbuffer PerMaterial : register(b1)
{
	uint PBRFlags : packoffset(c0.x);
	float3 PBRParams1 : packoffset(c0.y);
	float4 PBRParams2 : packoffset(c1);
};

#include "Common/LightingEval.hlsli"
#if defined(WATER_EFFECTS)
#	include "WaterEffects/WaterCaustics.hlsli"
#endif

SamplerState SampNormalSampler : register(s2);
SamplerState SampRMAOSSampler : register(s3);
SamplerState SampSubsurfaceSampler : register(s4);
Texture2D<float4> TexNormalSampler : register(t2);
Texture2D<float4> TexRMAOSSampler : register(t3);
Texture2D<float4> TexSubsurfaceSampler : register(t4);

namespace GrassPBR
{
	static const uint HasRmaosTexture = 1u << 13;
	void GetDirectLight(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material, bool doSpecular)
	{
		lightingOutput = (DirectLightingOutput)0;
		const float3 detailedLightColor = context.lightColor * context.detailedShadow;
		const float3 softLightColor = context.lightColor * context.softShadow;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;

		float NdotL = dot(N, L);
		float VdotL = dot(V, L);

		float satNdotL = clamp(NdotL, EPSILON_DOT_CLAMP, 1);

		float3 F = 0;
		float3 Fr = 0;
		[branch] if (doSpecular)
		{
			const float3 H = context.halfVector;
			float satNdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
			float satNdotH = saturate(dot(N, H));
			float satVdotH = saturate(dot(V, H));
			Fr = PBR::SpecularMicrofacet(material.Roughness, material.F0, satNdotL, satNdotV, satNdotH, satVdotH, F);
		}
		float3 kD = 1 - F;

		const float diffuseWrap = 0.5;
		float wrappedNdotL = saturate((abs(NdotL) + diffuseWrap) / (1.0 + diffuseWrap));
		lightingOutput.diffuse += detailedLightColor * wrappedNdotL * BRDF::Diffuse_Lambert() * kD;
		lightingOutput.specular += Fr * detailedLightColor * satNdotL;

		[branch] if ((PBRFlags & PBR::Flags::Subsurface) != 0)
		{
			const float subsurfacePower = 12.234;
			float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
			float backScatter = saturate(satNdotL * material.Thickness + (1.0 - material.Thickness)) * 0.5;
			float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - material.Thickness);
			lightingOutput.transmission += material.SubsurfaceColor * subsurface * softLightColor * BRDF::Diffuse_Lambert() * kD;
		}
		else if (SharedData::foliageLightingSettings.EnableGrassScattering != 0)
		{
			lightingOutput.transmission += material.BaseColor * softLightColor *
			                               GetFoliageTransmission(dot(context.vertexNormal, L), VdotL) * BRDF::Diffuse_Lambert() * kD;
		}
	}

	void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, bool doSpecular)
	{
		lobeWeights = (IndirectLobeWeights)0;

		lobeWeights.diffuse = material.BaseColor;

		[branch] if ((PBRFlags & PBR::Flags::Subsurface) != 0)
		{
			lobeWeights.diffuse += material.SubsurfaceColor * (1 - material.Thickness) / Math::PI;
		}

		[branch] if (doSpecular)
		{
			float NdotV = saturate(dot(context.worldNormal, context.viewDir));
			float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
			lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;
			lobeWeights.diffuse *= 1 - lobeWeights.specular;

			float alpha = material.Roughness * material.Roughness;
			lobeWeights.specular *= SpecularOcclusion(NdotV, alpha, material.AO);
		}

		lobeWeights.diffuse *= MultiBounceAO(material.BaseColor, material.AO);
	}
}

PS_OUTPUT RenderPBRGrass(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout = (PS_OUTPUT)0;
	float4 baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	baseColor.xyz = Color::PBRDiffuse(baseColor.xyz);

#if defined(RENDER_DEPTH)
	float diffuseAlpha = input.Color.w * baseColor.w;
#elif defined(DO_ALPHA_TEST)
	float diffuseAlpha = input.Color.w * baseColor.w;
#endif
#if defined(RENDER_DEPTH) || defined(DO_ALPHA_TEST)
	if ((diffuseAlpha - AlphaTestRefRS) < 0)
		discard;
#endif
#if defined(RENDER_DEPTH)
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
	return psout;
#else
	if (SharedData::ShouldDisableTerrainVertexColors())
		input.Color.xyz = 1;

	if (SharedData::grassLightingSettings.OverrideComplexGrassSettings)
		baseColor.xyz *= SharedData::grassLightingSettings.BasicGrassBrightness;

	uint eyeIndex = Stereo::GetEyeIndexPS(input.HPosition, VPOSOffset);
	psout.MotionVectors = float4(MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex), 0, 1);

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float3 vertexNormal = normalize(input.VertexNormal.xyz);
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::GrassSphereNormal) && dot(vertexNormal, viewDirection) < 0.0)
		vertexNormal = -vertexNormal;

	const bool pbrDetail = GRASS_DETAILED(true);
	float3 normal = vertexNormal;
	float4 rawRMAOS = float4(PBRParams1.x, 0, 1, PBRParams1.y);
	if (pbrDetail) {
		float4 normalSample = TexNormalSampler.SampleBias(SampNormalSampler, input.TexCoord.xy, SharedData::MipBias);
		float3x3 tbn = GrassLighting::CalculateTBN(vertexNormal, -input.WorldPosition.xyz, input.TexCoord.xy);
		normal = normalize(mul(GrassLighting::TransformNormal(normalSample.xyz), tbn));
		[branch] if ((PBRFlags & GrassPBR::HasRmaosTexture) != 0)
			rawRMAOS = TexRMAOSSampler.SampleBias(SampRMAOSSampler, input.TexCoord.xy, SharedData::MipBias) *
		               float4(PBRParams1.x, 1, 1, PBRParams1.y);
	}
	MaterialProperties material = (MaterialProperties)0;
	material.Roughness = clamp(rawRMAOS.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
	material.Metallic = saturate(rawRMAOS.y);
	material.AO = saturate(rawRMAOS.z);

	float vertexAO = max(max(input.Color.r, input.Color.g), input.Color.b);
	float3 vertexColor = Color::ColorToLinear(input.Color.xyz / max(vertexAO, EPSILON_DIVISION));
	material.BaseColor = ApplyGrassWetDarkening(baseColor.xyz) * vertexColor;
	material.F0 = lerp(saturate(rawRMAOS.w), material.BaseColor, material.Metallic);
	material.BaseColor *= 1 - material.Metallic;
	material.SubsurfaceColor = PBRParams2.xyz;
	material.Thickness = PBRParams2.w;
	[branch] if (pbrDetail && (PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
	{
		float4 subsurface = TexSubsurfaceSampler.Sample(SampSubsurfaceSampler, input.TexCoord.xy);
		material.SubsurfaceColor *= Color::PBRDiffuse(subsurface.xyz);
		material.Thickness *= subsurface.w;
	}

	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
	float screenNoise = Random::InterleavedGradientNoise(Stereo::EyeStableNoiseCoord(input.HPosition.xy, SharedData::BufferDim.xy), SharedData::FrameCount);
	float llDirLightMult = (Color::UseLinearLightingColorAdjustments() && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * Color::PBRLightingCompensation * llDirLightMult;
#	if defined(WATER_EFFECTS)
	dirLightColor *= WaterEffects::ComputeCaustics(SharedData::GetWaterData(input.WorldPosition.xyz), input.WorldPosition.xyz, eyeIndex);
#	endif

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));
	float dirDetailedShadow = 1.0;
	if (ShadowSampling::HasDirectionalShadows()) {
#	if defined(GRASS_OPTIMIZATIONS)
		dirDetailedShadow = shadowColor.x;
		if (pbrDetail)
#	endif
			dirDetailedShadow = shadowColor.x * ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
	}

#	if defined(SCREEN_SPACE_SHADOWS)
	if (GRASS_DETAILED(ShadowSampling::HasDirectionalShadows() && dot(normal, SharedData::DirLightDirection.xyz) >= 0))
		dirDetailedShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise, eyeIndex);
#	endif  // SCREEN_SPACE_SHADOWS

	float dirSoftShadow = dirDetailedShadow;
	float skylightingShadowVisibility = 1.0;
#	if defined(SKYLIGHTING)
#		if defined(VR)
	float3 positionMSSkylight = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#		else
	float3 positionMSSkylight = input.WorldPosition.xyz;
#		endif
	Skylighting::ShadowedSample skylightingSample = Skylighting::SampleWithShadow(positionMSSkylight, normal);
	sh2 skylightingSH = skylightingSample.Probe;
	skylightingShadowVisibility = skylightingSample.Visibility;
	if (Skylighting::IsEnabled() && SharedData::skylightingSettings.ShadowDataAvailable != 0)
		dirSoftShadow = min(dirSoftShadow, skylightingShadowVisibility);
#	endif

	DirectLightingOutput totalLighting = (DirectLightingOutput)0;
	DirectContext dirContext = CreateDirectLightingContext(normal, normal, vertexNormal, viewDirection, viewDirection,
		SharedData::DirLightDirection.xyz, SharedData::DirLightDirection.xyz, dirLightColor, dirDetailedShadow, dirSoftShadow);
	DirectLightingOutput dirOutput;
	GrassPBR::GetDirectLight(dirOutput, dirContext, material, pbrDetail);
	totalLighting.diffuse += dirOutput.diffuse;
	totalLighting.specular += dirOutput.specular;
	totalLighting.transmission += dirOutput.transmission;

#	if defined(LIGHT_LIMIT_FIX)
	uint clusterIndex = 0;
	uint lightCount = 0;
	if (GRASS_DETAILED(LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex))) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
			[loop] for (uint i = 0; i < lightCount; ++i)
			{
				LightLimitFix::Light light = LightLimitFix::lights[LightLimitFix::lightList[lightOffset + i]];
				float3 lightVector = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
				float lightDist = length(lightVector);
#		if defined(ISL)
				float attenuation = InverseSquareLighting::GetAttenuation(lightDist, light);
				if (attenuation < 1e-5)
					continue;
#		else
				float distanceFactor = saturate(lightDist / light.radius);
				if (distanceFactor == 1)
					continue;
				float attenuation = 1 - distanceFactor * distanceFactor;
#		endif
				const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
				float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear, light.lightFlags) * Color::PBRLightingCompensation * attenuation * light.fade;
				float lightShadow = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow)
					lightShadow = shadowColor[light.shadowLightIndex];
				float3 lightDirection = lightVector / max(lightDist, EPSILON_DIVISION);
				DirectContext pointContext = CreateDirectLightingContext(normal, normal, vertexNormal, viewDirection, viewDirection,
					lightDirection, lightDirection, lightColor, lightShadow, lightShadow);
				DirectLightingOutput pointOutput;
				GrassPBR::GetDirectLight(pointOutput, pointContext, material, pbrDetail);
				totalLighting.diffuse += pointOutput.diffuse;
				totalLighting.specular += pointOutput.specular;
				totalLighting.transmission += pointOutput.transmission;
			}
		}
	}
#	endif

	IndirectLobeWeights indirectLobes;
	IndirectContext indirectContext = CreateIndirectLightingContext(normal, vertexNormal, viewDirection);
	GrassPBR::GetIndirectLobeWeights(indirectLobes, indirectContext, material, pbrDetail);

	float3 directColor = totalLighting.diffuse * material.BaseColor + totalLighting.transmission;
	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));
#	if defined(SKYLIGHTING)
	float skylightingDiffuse = Skylighting::GetSkylightingDiffuse(skylightingSH, positionMSSkylight, normal, vertexAO);
#	endif
#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
#		if defined(SKYLIGHTING)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -normal, skylightingDiffuse);
#		else
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
#		endif
	}
#	endif
	directionalAmbientColor = Color::ApplyAmbientBalance(directionalAmbientColor);
	directColor += indirectLobes.diffuse * directionalAmbientColor;
	float3 outputAlbedo = indirectLobes.diffuse;
	directionalAmbientColor *= outputAlbedo;
#	if defined(SKYLIGHTING)
#		if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#		endif
		Skylighting::ApplySkylighting(directColor, directionalAmbientColor, outputAlbedo, skylightingDiffuse);
#	endif

	float3 outputColor = directColor;
#	if defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode < 2) {
			outputColor = Color::TurboColormap(0);
		} else {
			outputColor = Color::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
		}
	}
#	endif

	float3 normalVS = normalize(FrameBuffer::WorldToView(normal, false, eyeIndex));
	psout.Diffuse = float4(outputColor, 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), 1 - material.Roughness, 1);
	psout.Albedo = float4(outputAlbedo, 1);
	psout.Specular = float4(totalLighting.specular, 1);
	psout.Reflectance = float4(indirectLobes.specular, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 1);

	return psout;
#endif
}
#endif
