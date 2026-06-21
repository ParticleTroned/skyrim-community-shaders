#define LIGHTING

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/LodLandscape.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Skinned.hlsli"
#include "Common/Triplanar.hlsli"

#if defined(FACEGEN) || defined(FACEGEN_RGB_TINT)
#	define SKIN
#endif

#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

#if (defined(TREE_ANIM) || defined(LANDSCAPE)) && !defined(VC)
#	define VC
#endif  // TREE_ANIM || LANDSCAPE || !VC

#if defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(LODLANDNOISE) || defined(WORLD_MAP)
#	define LOD
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord0: TEXCOORD0;
#if !defined(MODELSPACENORMALS)
	float4 Normal: NORMAL0;
	float4 Bitangent: BINORMAL0;
#endif  // !MODELSPACENORMALS

#if defined(VC)
	float4 Color: COLOR0;
#	if defined(LANDSCAPE)
	float4 LandBlendWeights1: TEXCOORD2;
	float4 LandBlendWeights2: TEXCOORD3;
#	endif  // LANDSCAPE
#endif      // VC
#if defined(SKINNED)
	float4 BoneWeights: BLENDWEIGHT0;
	float4 BoneIndices: BLENDINDICES0;
#endif  // SKINNED
#if defined(EYE)
	float EyeParameter: TEXCOORD2;
#endif  // EYE
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
#if (defined(PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
	float4
#else
	float2
#endif  // (defined (PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
		TexCoord0: TEXCOORD0;

#if defined(WORLD_MAP)
	float3 InputPosition: TEXCOORD4;
#endif

#if defined(SKINNED) || !defined(MODELSPACENORMALS)
	float3 TBN0: TEXCOORD1;
	float3 TBN1: TEXCOORD2;
	float3 TBN2: TEXCOORD3;
#endif  // defined(SKINNED) || !defined(MODELSPACENORMALS)
#if defined(EYE)
	float3 EyeNormal: TEXCOORD6;
#elif defined(LANDSCAPE)
	float4 LandBlendWeights1: TEXCOORD6;
	float4 LandBlendWeights2: TEXCOORD7;
#elif defined(PROJECTED_UV) && !defined(SKINNED)
	float3 TexProj: TEXCOORD7;
#endif  // EYE

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float4 Color: COLOR0;
	float4 FogParam: COLOR1;

	float3 ModelPosition: TEXCOORD12;
};
#ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
	float4 HighDetailRange : packoffset(c0);  // loaded cells center in xy, size in zw
	float4 FogParam : packoffset(c1);
	float4 FogNearColor : packoffset(c2);
	float4 FogFarColor : packoffset(c3);
};

cbuffer PerMaterial : register(b1)
{
	float4 LeftEyeCenter : packoffset(c0);
	float4 RightEyeCenter : packoffset(c1);
	float4 TexcoordOffset : packoffset(c2);
};

cbuffer PerGeometry : register(b2)
{
	row_major float3x4 World : packoffset(c0);
	row_major float3x4 PreviousWorld : packoffset(c3);
	float4 EyePosition : packoffset(c6);
	float4 LandBlendParams : packoffset(c7);  // offset in xy, gridPosition in yw
	float4 TreeParams : packoffset(c8);       // wind magnitude in y, amplitude in z, leaf frequency in w
	float2 WindTimers : packoffset(c9);
	row_major float3x4 TextureProj : packoffset(c10);
	float IndexScale : packoffset(c13);
	float4 WorldMapOverlayParameters : packoffset(c14);
};

cbuffer VS_PerFrame : register(b12)
{
	row_major float3x3 ScreenProj : packoffset(c0);
	row_major float4x4 ViewProj : packoffset(c8);
#	if defined(SKINNED)
	float3 BonesPivot : packoffset(c40);
	float3 PreviousBonesPivot : packoffset(c41);
#	endif  // SKINNED
};

#	if defined(TREE_ANIM)
float2 GetTreeShiftVector(float4 position, float4 color)
{
	precise float4 tmp1 = (TreeParams.w * TreeParams.y).xxxx * WindTimers.xxyy;
	precise float4 tmp2 = float4(0.1, 0.25, 0.1, 0.25) * tmp1 + dot(position.xyz, 1.0.xxx).xxxx;
	precise float4 tmp3 = abs(-1.0.xxxx + 2.0.xxxx * frac(0.5.xxxx + tmp2.xyzw));
	precise float4 tmp4 = (tmp3 * tmp3) * (3.0.xxxx - 2.0.xxxx * tmp3);
	return (tmp4.xz + 0.1.xx * tmp4.yw) * (TreeParams.z * color.w).xx;
}
#	endif  // TREE_ANIM

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	precise float4 inputPosition = float4(input.Position.xyz, 1.0);

#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	inputPosition = LodLandscape::AdjustLodLandscapeVertexPositionMS(inputPosition, float4x4(World, float4(0, 0, 0, 1)), HighDetailRange);
#	endif  // defined(LODLANDNOISE) || defined(LODLANDSCAPE)                                                                   \

	precise float4 previousInputPosition = inputPosition;

#	if defined(TREE_ANIM)
	precise float2 treeShiftVector = GetTreeShiftVector(input.Position, input.Color);
	float3 normal = -1.0.xxx + 2.0.xxx * input.Normal.xyz;

	inputPosition.xyz += normal.xyz * treeShiftVector.x;
	previousInputPosition.xyz += normal.xyz * treeShiftVector.y;
#	endif

#	if defined(SKINNED)
	precise int4 actualIndices = 765.01.xxxx * input.BoneIndices.xyzw;

	float3x4 previousWorldMatrix =
		Skinned::GetBoneTransformMatrix(PreviousBones, actualIndices, PreviousBonesPivot, input.BoneWeights);
	precise float4 previousWorldPosition =
		float4(mul(inputPosition, transpose(previousWorldMatrix)), 1);

	float3x4 worldMatrix = Skinned::GetBoneTransformMatrix(Bones, actualIndices, BonesPivot, input.BoneWeights);
	precise float4 worldPosition = float4(mul(inputPosition, transpose(worldMatrix)), 1);

	float4 viewPos = mul(ViewProj, worldPosition);
#	else   // !SKINNED
	precise float4 previousWorldPosition = float4(mul(PreviousWorld, inputPosition), 1);
	precise float4 worldPosition = float4(mul(World, inputPosition), 1);
	precise float4x4 world4x4 = float4x4(World[0], World[1], World[2], float4(0, 0, 0, 1));
	precise float4x4 modelView = mul(ViewProj, world4x4);
	float4 viewPos = mul(modelView, inputPosition);
#	endif  // SKINNED

	vsout.Position = viewPos;

#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	vsout.Position.z += min(1, 1e-4 * max(0, viewPos.z - 70000)) * 0.5;
#	endif

	float2 uv = input.TexCoord0.xy * TexcoordOffset.zw + TexcoordOffset.xy;
#	if defined(LANDSCAPE)
	vsout.TexCoord0.zw = (uv * 0.010416667.xx + LandBlendParams.xy) * float2(1, -1) + float2(0, 1);
#	elif defined(PROJECTED_UV) && !defined(SKINNED)
	vsout.TexCoord0.z = mul(TextureProj[0], inputPosition);
	vsout.TexCoord0.w = mul(TextureProj[1], inputPosition);
#	endif
	vsout.TexCoord0.xy = uv;

#	if defined(WORLD_MAP)
	vsout.InputPosition.xyz = WorldMapOverlayParameters.xyz + worldPosition.xyz;
#	endif

#	if defined(SKINNED)
	float3x3 boneRSMatrix = Skinned::GetBoneRSMatrix(Bones, actualIndices, input.BoneWeights);
#	endif

#	if !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(
		float3(input.Position.w, input.Normal.w * 2 - 1, input.Bitangent.w * 2 - 1),
		input.Bitangent.xyz * 2.0.xxx + -1.0.xxx,
		input.Normal.xyz * 2.0.xxx + -1.0.xxx);
	float3x3 tbnTr = transpose(tbn);

#		if defined(SKINNED)
	float3x3 worldTbnTr = transpose(mul(transpose(tbnTr), transpose(boneRSMatrix)));
	float3x3 worldTbnTrTr = transpose(worldTbnTr);
	worldTbnTrTr[0] = normalize(worldTbnTrTr[0]);
	worldTbnTrTr[1] = normalize(worldTbnTrTr[1]);
	worldTbnTrTr[2] = normalize(worldTbnTrTr[2]);
	worldTbnTr = transpose(worldTbnTrTr);
	vsout.TBN0.xyz = worldTbnTr[0];
	vsout.TBN1.xyz = worldTbnTr[1];
	vsout.TBN2.xyz = worldTbnTr[2];
#		else
	vsout.TBN0.xyz = mul(tbn, World[0].xyz);
	vsout.TBN1.xyz = mul(tbn, World[1].xyz);
	vsout.TBN2.xyz = mul(tbn, World[2].xyz);
	float3x3 tempTbnTr = transpose(float3x3(vsout.TBN0.xyz, vsout.TBN1.xyz, vsout.TBN2.xyz));
	tempTbnTr[0] = normalize(tempTbnTr[0]);
	tempTbnTr[1] = normalize(tempTbnTr[1]);
	tempTbnTr[2] = normalize(tempTbnTr[2]);
	tempTbnTr = transpose(tempTbnTr);
	vsout.TBN0.xyz = tempTbnTr[0];
	vsout.TBN1.xyz = tempTbnTr[1];
	vsout.TBN2.xyz = tempTbnTr[2];
#		endif
#	elif defined(SKINNED)
	float3x3 boneRSMatrixTr = transpose(boneRSMatrix);
	float3x3 worldTbnTr = transpose(float3x3(normalize(boneRSMatrixTr[0]),
		normalize(boneRSMatrixTr[1]), normalize(boneRSMatrixTr[2])));

	vsout.TBN0.xyz = worldTbnTr[0];
	vsout.TBN1.xyz = worldTbnTr[1];
	vsout.TBN2.xyz = worldTbnTr[2];
#	endif

#	if defined(LANDSCAPE)
	vsout.LandBlendWeights1 = input.LandBlendWeights1;

	float2 gridOffset = LandBlendParams.zw - input.Position.xy;
	vsout.LandBlendWeights2.w = 1 - saturate(0.000375600968 * (9625.59961 - length(gridOffset)));
	vsout.LandBlendWeights2.xyz = input.LandBlendWeights2.xyz;
#	elif defined(PROJECTED_UV) && !defined(SKINNED)
	float3x3 texProjWorld3x3 = float3x3(World[0].xyz, World[1].xyz, World[2].xyz);
	vsout.TexProj = mul(texProjWorld3x3, TextureProj[2].xyz);
#	endif

#	if defined(EYE)
	precise float4 modelEyeCenter = float4(LeftEyeCenter.xyz + input.EyeParameter.xxx * (RightEyeCenter.xyz - LeftEyeCenter.xyz), 1);
	vsout.EyeNormal.xyz = normalize(worldPosition.xyz - mul(modelEyeCenter, transpose(worldMatrix)));
#	endif  // EYE

	vsout.WorldPosition = worldPosition;
	vsout.PreviousWorldPosition = previousWorldPosition;

#	if defined(VC)
	vsout.Color = input.Color;
#	else
	vsout.Color = 1.0.xxxx;
#	endif  // VC

	float fogColorParam = min(FogParam.w,
		exp2(FogParam.z * log2(saturate(length(viewPos.xyz) * FogParam.y - FogParam.x))));

	vsout.FogParam.xyz = lerp(FogNearColor.xyz, FogFarColor.xyz, fogColorParam);
	vsout.FogParam.w = fogColorParam;

	vsout.ModelPosition = input.Position.xyz;

	return vsout;
}
#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#if !defined(LANDSCAPE)
#	undef TERRAIN_BLENDING
#endif

#if defined(DEFERRED)
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
	float4 Reflectance: SV_Target5;
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
};
#else
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
};
#endif

#ifdef PSHADER

SamplerState SampTerrainParallaxSampler : register(s1);

#	if defined(LANDSCAPE)

SamplerState SampColorSampler : register(s0);

#		define SampLandColor2Sampler SampColorSampler
#		define SampLandColor3Sampler SampColorSampler
#		define SampLandColor4Sampler SampColorSampler
#		define SampLandColor5Sampler SampColorSampler
#		define SampLandColor6Sampler SampColorSampler
#		define SampNormalSampler SampColorSampler
#		define SampLandNormal2Sampler SampColorSampler
#		define SampLandNormal3Sampler SampColorSampler
#		define SampLandNormal4Sampler SampColorSampler
#		define SampLandNormal5Sampler SampColorSampler
#		define SampLandNormal6Sampler SampColorSampler
#		define SampRMAOSSampler SampColorSampler
#		define SampLandRMAOS2Sampler SampColorSampler
#		define SampLandRMAOS3Sampler SampColorSampler
#		define SampLandRMAOS4Sampler SampColorSampler
#		define SampLandRMAOS5Sampler SampColorSampler
#		define SampLandRMAOS6Sampler SampColorSampler

#	else

SamplerState SampColorSampler : register(s0);

#		define SampNormalSampler SampColorSampler

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
SamplerState SampSpecularSampler : register(s2);
#		endif
#		if defined(FACEGEN)
SamplerState SampTintSampler : register(s3);
SamplerState SampDetailSampler : register(s4);
#		elif defined(PARALLAX)
SamplerState SampParallaxSampler : register(s3);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
SamplerState SampProjDiffuseSampler : register(s3);
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(SNOW_FLAG) || defined(EYE)) && !defined(FACEGEN)
SamplerState SampEnvSampler : register(s4);
SamplerState SampEnvMaskSampler : register(s5);
#		endif

#		if defined(TRUE_PBR) && !defined(FACEGEN)
SamplerState SampParallaxSampler : register(s4);
#		endif
#		if defined(TRUE_PBR)
SamplerState SampRMAOSSampler : register(s5);
#		endif

SamplerState SampGlowSampler : register(s6);

#		if defined(MULTI_LAYER_PARALLAX)
SamplerState SampLayerSampler : register(s8);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
SamplerState SampProjNormalSampler : register(s8);
#		endif

SamplerState SampBackLightSampler : register(s9);

#		if defined(PROJECTED_UV)
SamplerState SampProjDetailSampler : register(s10);
#		endif

SamplerState SampCharacterLightProjNoiseSampler : register(s11);
SamplerState SampRimSoftLightWorldMapOverlaySampler : register(s12);

#		if defined(WORLD_MAP) && (defined(LODLANDSCAPE) || defined(LODLANDNOISE))
SamplerState SampWorldMapOverlaySnowSampler : register(s13);
#		endif

#	endif

#	if defined(LOD_LAND_BLEND)
SamplerState SampLandLodBlend1Sampler : register(s13);
SamplerState SampLandLodBlend2Sampler : register(s15);
#	elif defined(LODLANDNOISE)
SamplerState SampLandLodNoiseSampler : register(s15);
#	endif

SamplerState SampShadowMaskSampler : register(s14);

#	if defined(LANDSCAPE)

Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexLandColor2Sampler : register(t1);
Texture2D<float4> TexLandColor3Sampler : register(t2);
Texture2D<float4> TexLandColor4Sampler : register(t3);
Texture2D<float4> TexLandColor5Sampler : register(t4);
Texture2D<float4> TexLandColor6Sampler : register(t5);
Texture2D<float4> TexNormalSampler : register(t7);
Texture2D<float4> TexLandNormal2Sampler : register(t8);
Texture2D<float4> TexLandNormal3Sampler : register(t9);
Texture2D<float4> TexLandNormal4Sampler : register(t10);
Texture2D<float4> TexLandNormal5Sampler : register(t11);
Texture2D<float4> TexLandNormal6Sampler : register(t12);

Texture2D<float4> TexLandTHDisp0Sampler : register(t92);
Texture2D<float4> TexLandTHDisp1Sampler : register(t93);
Texture2D<float4> TexLandTHDisp2Sampler : register(t94);
Texture2D<float4> TexLandTHDisp3Sampler : register(t95);
Texture2D<float4> TexLandTHDisp4Sampler : register(t96);
Texture2D<float4> TexLandTHDisp5Sampler : register(t97);

#		if defined(TRUE_PBR)

Texture2D<float4> TexLandDisplacement0Sampler : register(t80);
Texture2D<float4> TexLandDisplacement1Sampler : register(t81);
Texture2D<float4> TexLandDisplacement2Sampler : register(t82);
Texture2D<float4> TexLandDisplacement3Sampler : register(t83);
Texture2D<float4> TexLandDisplacement4Sampler : register(t84);
Texture2D<float4> TexLandDisplacement5Sampler : register(t85);

Texture2D<float4> TexRMAOSSampler : register(t86);
Texture2D<float4> TexLandRMAOS2Sampler : register(t87);
Texture2D<float4> TexLandRMAOS3Sampler : register(t88);
Texture2D<float4> TexLandRMAOS4Sampler : register(t89);
Texture2D<float4> TexLandRMAOS5Sampler : register(t90);
Texture2D<float4> TexLandRMAOS6Sampler : register(t91);

#		endif

#	else

Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexNormalSampler : register(t1);  // normal in xyz, glossiness in w if not modelspacenormal

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
Texture2D<float4> TexSpecularSampler : register(t2);
#		endif
#		if defined(FACEGEN)
Texture2D<float4> TexTintSampler : register(t3);
Texture2D<float4> TexDetailSampler : register(t4);
#		elif defined(PARALLAX)
Texture2D<float4> TexParallaxSampler : register(t3);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
Texture2D<float4> TexProjDiffuseSampler : register(t3);
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(SNOW_FLAG) || defined(EYE)) && !defined(FACEGEN)
TextureCube<float4> TexEnvSampler : register(t4);
Texture2D<float4> TexEnvMaskSampler : register(t5);
#		endif

#		if defined(TRUE_PBR) && !defined(FACEGEN)
Texture2D<float4> TexParallaxSampler : register(t4);
#		endif
#		if defined(TRUE_PBR)
Texture2D<float4> TexRMAOSSampler : register(t5);
#		endif

Texture2D<float4> TexGlowSampler : register(t6);

#		if defined(MULTI_LAYER_PARALLAX)
Texture2D<float4> TexLayerSampler : register(t8);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
Texture2D<float4> TexProjNormalSampler : register(t8);
#		endif

Texture2D<float4> TexBackLightSampler : register(t9);

#		if defined(PROJECTED_UV)
Texture2D<float4> TexProjDetail : register(t10);
#		endif

Texture2D<float4> TexCharacterLightProjNoiseSampler : register(t11);
Texture2D<float4> TexRimSoftLightWorldMapOverlaySampler : register(t12);

#		if defined(WORLD_MAP) && (defined(LODLANDSCAPE) || defined(LODLANDNOISE))
Texture2D<float4> TexWorldMapOverlaySnowSampler : register(t13);
#		endif

#	endif

#	if defined(LOD_LAND_BLEND)
Texture2D<float4> TexLandLodBlend1Sampler : register(t13);
Texture2D<float4> TexLandLodBlend2Sampler : register(t15);
#	elif defined(LODLANDNOISE)
Texture2D<float4> TexLandLodNoiseSampler : register(t15);
#	endif

Texture2D<float4> TexShadowMaskSampler : register(t14);

#	if defined(SKIN) && defined(CS_SKIN)
Texture2D<float4> TexSkinExtraSampler : register(t71);
Texture2D<float4> TexSkinWetnessSampler : register(t74);
Texture2D<float4> TexSkinWetnessNormalSampler : register(t75);
#	endif

cbuffer PerTechnique : register(b0)
{
	float4 FogColor : packoffset(c0);           // Color in xyz, invFrameBufferRange in w
	float4 ColourOutputClamp : packoffset(c1);  // fLightingOutputColourClampPostLit in x, fLightingOutputColourClampPostEnv in y, fLightingOutputColourClampPostSpec in z
	float4 VPOSOffset : packoffset(c2);         // ???
};

cbuffer PerMaterial : register(b1)
{
	float4 LODTexParams : packoffset(c0);  // TerrainTexOffset in xy, LodBlendingEnabled in z
#	if !(defined(LANDSCAPE) && defined(TRUE_PBR))
	float4 TintColor : packoffset(c1);
	float4 EnvmapData : packoffset(c2);  // fEnvmapScale in x, 1 or 0 in y depending of if has envmask
	float4 ParallaxOccData : packoffset(c3);
	float4 SpecularColor : packoffset(c4);  // Shininess in w, color in xyz
	float4 SparkleParams : packoffset(c5);
	float4 MultiLayerParallaxData : packoffset(c6);  // Layer thickness in x, refraction scale in y, uv scale in zw
#	else
	float4 LandscapeTexture1GlintParameters : packoffset(c1);
	float4 LandscapeTexture2GlintParameters : packoffset(c2);
	float4 LandscapeTexture3GlintParameters : packoffset(c3);
	float4 LandscapeTexture4GlintParameters : packoffset(c4);
	float4 LandscapeTexture5GlintParameters : packoffset(c5);
	float4 LandscapeTexture6GlintParameters : packoffset(c6);
#	endif
	float4 LightingEffectParams : packoffset(c7);  // fSubSurfaceLightRolloff in x, fRimLightPower in y
	float4 IBLParams : packoffset(c8);

#	if !defined(TRUE_PBR)
	float4 LandscapeTexture1to4IsSnow : packoffset(c9);
	float4 LandscapeTexture5to6IsSnow : packoffset(c10);  // bEnableSnowMask in z, inverse iLandscapeMultiNormalTilingFactor in w
	float4 LandscapeTexture1to4IsSpecPower : packoffset(c11);
	float4 LandscapeTexture5to6IsSpecPower : packoffset(c12);
	float4 SnowRimLightParameters : packoffset(c13);  // fSnowRimLightIntensity in x, fSnowGeometrySpecPower in y, fSnowNormalSpecPower in z, bEnableSnowRimLighting in w
#	endif

#	if defined(TRUE_PBR) && defined(LANDSCAPE)
	float3 LandscapeTexture2PBRParams : packoffset(c9);
	float3 LandscapeTexture3PBRParams : packoffset(c10);
	float3 LandscapeTexture4PBRParams : packoffset(c11);
	float3 LandscapeTexture5PBRParams : packoffset(c12);
	float3 LandscapeTexture6PBRParams : packoffset(c13);
#	endif

	float4 CharacterLightParams : packoffset(c14);

	uint PBRFlags : packoffset(c15.x);
	float3 PBRParams1 : packoffset(c15.y);  // roughness scale, displacement scale, specular level
	float4 PBRParams2 : packoffset(c16);    // subsurface color, subsurface opacity

	float3 MaterialObjectRGBScale : packoffset(c17);  // RGB multipliers for material objects
};

cbuffer PerGeometry : register(b2)
{
	float3 DirLightDirection : packoffset(c0);
	float3 DirLightColor : packoffset(c1);
	float4 ShadowLightMaskSelect : packoffset(c2);
	float4 MaterialData : packoffset(c3);  // envmapLODFade in x, specularLODFade in y, alpha in z
	float AlphaTestRef : packoffset(c4);
	float3 EmitColor : packoffset(c4.y);
	float4 ProjectedUVParams : packoffset(c6);
	float4 SSRParams : packoffset(c7);
	float4 WorldMapOverlayParametersPS : packoffset(c8);
	float4 ProjectedUVParams2 : packoffset(c9);
	float4 ProjectedUVParams3 : packoffset(c10);  // fProjectedUVDiffuseNormalTilingScale in x, fProjectedUVNormalDetailTilingScale in y, EnableProjectedNormals in w
	row_major float3x4 DirectionalAmbient : packoffset(c11);
	float4 AmbientSpecularTintAndFresnelPower : packoffset(c14);  // Fresnel power in z, color in xyz
	float4 PointLightPosition[7] : packoffset(c15);               // point light radius in w
	float4 PointLightColor[7] : packoffset(c22);
	float2 NumLightNumShadowLight : packoffset(c29);
};

cbuffer AlphaTestRefBuffer : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

float GetSoftLightMultiplier(float angle)
{
	float softLightParam = saturate((LightingEffectParams.x + angle) / (1 + LightingEffectParams.x));
	float arg1 = (softLightParam * softLightParam) * (3 - 2 * softLightParam);
	float clampedAngle = saturate(angle);
	float arg2 = (clampedAngle * clampedAngle) * (3 - 2 * clampedAngle);
	float softLigtMul = saturate(arg1 - arg2);
	return softLigtMul;
}

float GetRimLightMultiplier(float3 L, float3 V, float3 N)
{
	float NdotV = saturate(dot(N, V));
	return exp2(LightingEffectParams.y * log2(1 - NdotV)) * saturate(dot(V, -L));
}

#	if !defined(TRUE_PBR)
float ProcessSparkleColor(float color)
{
	return exp2(SparkleParams.y * log2(min(1, abs(color))));
}
#	endif

float3 TransformNormal(float3 normal)
{
	return normal * 2 + -1.0.xxx;
}

float GetLodLandBlendParameter(float3 color)
{
	float result = saturate(1.6666666 * (dot(color, 0.55.xxx) - 0.4));
	result = ((result * result) * (3 - result * 2));
#	if !defined(WORLD_MAP)
	result *= 0.8;
#	endif
	return result;
}

float GetLodLandBlendMultiplier(float parameter, float mask)
{
	return 0.8333333 * (parameter * (0.37 - mask) + mask) + 0.37;
}

float GetLandSnowMaskValue(float alpha)
{
#	if !defined(TRUE_PBR)
	return alpha * LandscapeTexture5to6IsSnow.z + (1 + -LandscapeTexture5to6IsSnow.z);
#	else
	return 0;
#	endif
}

float3 GetLandNormal(float landSnowMask, float3 normal, float2 uv, SamplerState sampNormal, Texture2D<float4> texNormal)
{
	float3 landNormal = TransformNormal(normal);
#	if defined(SNOW) && !defined(TRUE_PBR)
	if (landSnowMask > 1e-5 && LandscapeTexture5to6IsSnow.w != 1.0) {
		float3 snowNormal =
			float3(-1, -1, 1) *
			TransformNormal(texNormal.Sample(sampNormal, LandscapeTexture5to6IsSnow.ww * uv).xyz);
		landNormal.z += 1;
		float normalProjection = dot(landNormal, snowNormal);
		snowNormal = landNormal * normalProjection.xxx - snowNormal * landNormal.z;
		return normalize(snowNormal);
	} else {
		return landNormal;
	}
#	else
	return landNormal;
#	endif
}

#	if defined(SNOW) && !defined(TRUE_PBR)
float3 GetSnowSpecularColor(PS_INPUT input, float3 worldNormal, float3 viewDirection)
{
	if (SnowRimLightParameters.w > 1e-5) {
#		if defined(MODELSPACENORMALS) && !defined(SKINNED)
		float3 modelGeometryNormal = float3(0, 0, 1);
#		else
		float3 modelGeometryNormal = normalize(float3(input.TBN0.z, input.TBN1.z, input.TBN2.z));
#		endif
		float normalFactor = 1 - saturate(dot(worldNormal, viewDirection));
		float geometryNormalFactor = 1 - saturate(dot(modelGeometryNormal, viewDirection));
		return (SnowRimLightParameters.x * (exp2(SnowRimLightParameters.y * log2(geometryNormalFactor)) * exp2(SnowRimLightParameters.z * log2(normalFactor)))).xxx;
	} else {
		return 0.0.xxx;
	}
}
#	endif

#	if defined(FACEGEN)
float3 GetFacegenBaseColor(float3 rawBaseColor, float2 uv)
{
	float3 detailColor = TexDetailSampler.Sample(SampDetailSampler, uv).xyz;
	detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);
	float3 tintColor = TexTintSampler.Sample(SampTintSampler, uv).xyz;
	tintColor = tintColor * rawBaseColor * 2.0.xxx;
	tintColor = tintColor - tintColor * rawBaseColor;
	return (rawBaseColor * rawBaseColor + tintColor) * detailColor;
}
#	endif

#	if defined(FACEGEN_RGB_TINT)
float3 GetFacegenRGBTintBaseColor(float3 rawBaseColor, float2 uv)
{
	float3 tintColor = TintColor.xyz * rawBaseColor * 2.0.xxx;
	tintColor = tintColor - tintColor * rawBaseColor;
	return float3(1.01171875, 0.99609375, 1.01171875) * (rawBaseColor * rawBaseColor + tintColor);
}
#	endif

#	if defined(WORLD_MAP)
float3 GetWorldMapNormal(PS_INPUT input, float3 rawNormal, float3 baseColor)
{
	float3 normal = normalize(rawNormal);
#		if defined(MODELSPACENORMALS)
	float3 worldMapNormalSrc = normal.xyz;
#		else
	float3 worldMapNormalSrc = float3(input.TBN0.z, input.TBN1.z, input.TBN2.z);
#		endif
	float3 worldMapNormal = 7.0.xxx * (-0.2.xxx + abs(normalize(worldMapNormalSrc)));
	worldMapNormal = max(0.01.xxx, worldMapNormal * worldMapNormal * worldMapNormal);
	worldMapNormal /= dot(worldMapNormal, 1.0.xxx);
	float3 worldMapColor1 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.yz).xyz;
	float3 worldMapColor2 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.xz).xyz;
	float3 worldMapColor3 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.xy).xyz;
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float3 worldMapSnowColor1 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.yz).xyz;
	float3 worldMapSnowColor2 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.xz).xyz;
	float3 worldMapSnowColor3 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.xy).xyz;
#		endif
	float3 worldMapColor = worldMapNormal.xxx * worldMapColor1 + worldMapNormal.yyy * worldMapColor2 + worldMapNormal.zzz * worldMapColor3;
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float3 worldMapSnowColor = worldMapSnowColor1 * worldMapNormal.xxx + worldMapSnowColor2 * worldMapNormal.yyy + worldMapSnowColor3 * worldMapNormal.zzz;
	float snowMultiplier = GetLodLandBlendParameter(baseColor);
	worldMapColor = snowMultiplier * (worldMapSnowColor - worldMapColor) + worldMapColor;
#		endif
	worldMapColor = normalize(2.0.xxx * (-0.5.xxx + (worldMapColor)));
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float worldMapLandTmp = saturate(19.9999962 * (rawNormal.z - 0.95));
	worldMapLandTmp = saturate(-(worldMapLandTmp * worldMapLandTmp) * (worldMapLandTmp * -2 + 3) + 1.5);
	float3 worldMapLandTmp1 = normalize(normal.zxy * float3(1, 0, 0) - normal.yzx * float3(0, 0, 1));
	float3 worldMapLandTmp2 = normalize(worldMapLandTmp1.yzx * normal.zxy - worldMapLandTmp1.zxy * normal.yzx);
	float3 worldMapLandTmp3 = normalize(worldMapColor.xxx * worldMapLandTmp1 + worldMapColor.yyy * worldMapLandTmp2 + worldMapColor.zzz * normal.xyz);
	float worldMapLandTmp4 = dot(worldMapLandTmp3, worldMapLandTmp3);
	if (worldMapLandTmp4 > 0.999 && worldMapLandTmp4 < 1.001) {
		normal.xyz = worldMapLandTmp * (worldMapLandTmp3 - normal.xyz) + normal.xyz;
	}
#		else
	normal.xyz = normalize(
		WorldMapOverlayParametersPS.zzz * (rawNormal.xyz - worldMapColor.xyz) + worldMapColor.xyz);
#		endif
	return normal;
}

float3 GetWorldMapBaseColor(float3 originalBaseColor, float3 rawBaseColor, float texProjTmp)
{
#		if defined(LODOBJECTS) && !defined(PROJECTED_UV)
	return rawBaseColor;
#		endif
#		if defined(LODLANDSCAPE) || defined(LODOBJECTSHD) || defined(LODLANDNOISE)
	float lodMultiplier = GetLodLandBlendParameter(originalBaseColor.xyz);
#		elif defined(LODOBJECTS) && defined(PROJECTED_UV)
	float lodMultiplier = saturate(10 * texProjTmp);
#		else
	float lodMultiplier = 1;
#		endif
#		if defined(LODOBJECTS)
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.269999981, 0.281000018, 0.441000015, 0.441000015) + float4(0.0780000091, 0.09799999, -0.0349999964, 0.465000004);
	float4 lodColor = lodColorMul.xyzw * 2.0.xxxx;
	lodColor.xyz = Color::Diffuse(lodColor.xyz);
	bool useLodColorZ = lodColorMul.w > 0.5;
	lodColor.xyz = max(lodColor.xyz, rawBaseColor.xyz);
	lodColor.w = useLodColorZ ? lodColor.z : min(lodColor.w, rawBaseColor.z);
	return (0.5 * lodMultiplier).xxx * (lodColor.xyw - rawBaseColor.xyz) + rawBaseColor;
#		else
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.199999988, 0.441000015, 0.269999981, 0.281000018) + float4(0.300000012, 0.465000004, 0.0780000091, 0.09799999);
	float3 lodColor = lodColorMul.zwy * 2.0.xxx;
	lodColor.xyz = Color::Diffuse(lodColor.xyz);
	lodColor.xy = max(lodColor.xy, rawBaseColor.xy);
	lodColor.z = lodColorMul.y > 0.5 ? max((lodMultiplier * 0.441 + -0.0349999964) * 2, rawBaseColor.z) : min(lodColor.z, rawBaseColor.z);
	return lodColorMul.xxx * (lodColor - rawBaseColor.xyz) + rawBaseColor;
#		endif
}
#	endif

float GetSnowParameterY(float texProjTmp, float alpha)
{
	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::BaseObjectIsSnow) {
		return min(1, texProjTmp + alpha);
	}
	return texProjTmp;
}

#	if defined(LOD)
#		undef EXTENDED_MATERIALS
#		undef WATER_BLENDING
#		undef LIGHT_LIMIT_FIX
#		undef WETNESS_EFFECTS
#		undef DYNAMIC_CUBEMAPS
#		undef WATER_EFFECTS
#	endif

#	if defined(WORLD_MAP)
#		undef CLOUD_SHADOWS
#		undef SKYLIGHTING
#	endif

#	include "Common/LightingCommon.hlsli"

#	if defined(WATER_EFFECTS)
#		include "WaterEffects/WaterCaustics.hlsli"
#	endif

#	if defined(EYE)
#		undef WETNESS_EFFECTS
#	endif

#	if defined(EXTENDED_MATERIALS) && !defined(LOD) && (defined(PARALLAX) || defined(LANDSCAPE) || defined(ENVMAP) || defined(TRUE_PBR))
#		define EMAT
#	endif

#	if defined(EMAT) && (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#		define EMAT_ENVMAP
#	endif

#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(TRUE_PBR)
#		include "Common/PBR.hlsli"
#	endif

#	if defined(EMAT)
#		include "ExtendedMaterials/ExtendedMaterials.hlsli"
#	endif

#	if defined(SCREEN_SPACE_SHADOWS)
#		include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#	endif

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#		include "InverseSquareLighting/InverseSquareLighting.hlsli"
#	endif

#	if defined(TREE_ANIM)
#		undef WETNESS_EFFECTS
#	endif

#	if defined(WETTERNESS)
#		define CS_WETNESS_RUNTIME SharedData::wetternessSettings
#		define CS_WETNESS_SETTINGS SharedData::wetternessSettings.settings
#		define CS_WETNESS_ENABLED (CS_WETNESS_SETTINGS.EnableWetterness != 0)
#		include "Wetterness/Wetterness.hlsli"
#		define CS_TEX_PRECIP_OCCLUSION Wetterness::TexPrecipOcclusion
#		define CS_GET_RAIN_DROPS(worldPos, time, normal, strength) Wetterness::GetRainDrops(worldPos, time, normal, strength)
#		define CS_REORIENT_NORMAL(n1, n2) Wetterness::ReorientNormal(n1, n2)
#	elif defined(WETNESS_EFFECTS)
#		define CS_WETNESS_RUNTIME SharedData::wetnessEffectsSettings
#		define CS_WETNESS_SETTINGS SharedData::wetnessEffectsSettings
#		define CS_WETNESS_ENABLED (CS_WETNESS_SETTINGS.EnableWetnessEffects != 0)
#		include "WetnessEffects/WetnessEffects.hlsli"
#		define CS_TEX_PRECIP_OCCLUSION WetnessEffects::TexPrecipOcclusion
#		define CS_GET_RAIN_DROPS(worldPos, time, normal, strength) WetnessEffects::GetRainDrops(worldPos, time, normal, strength)
#		define CS_REORIENT_NORMAL(n1, n2) ReorientNormal(n1, n2)
#	endif

#	if defined(TERRAIN_BLENDING)
#		include "TerrainBlending/TerrainBlending.hlsli"
#	endif

#	if defined(SSS) && defined(SKIN) && defined(DEFERRED)
#		undef SOFT_LIGHTING
#	endif

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	if defined(HAIR) && defined(CS_HAIR)
#		include "Hair/Hair.hlsli"
#	endif

#	if defined(TERRAIN_VARIATION)
#		include "TerrainVariation/TerrainVariation.hlsli"
#	endif

#	if defined(EXTENDED_TRANSLUCENCY) && !(defined(LOD) || defined(SKIN) || defined(HAIR) || defined(EYE) || defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS) || defined(DEPTH_WRITE_DECALS))
#		include "ExtendedTranslucency/ExtendedTranslucency.hlsli"
#		define ANISOTROPIC_ALPHA
#	endif

#	if defined(CS_SKIN)
#		include "Skin/Skin.hlsli"
#	endif

#	define LinearSampler SampColorSampler

#	include "Common/ShadowSampling.hlsli"

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(EXP_HEIGHT_FOG)
#		include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	endif

#	include "Common/LightingEval.hlsli"

#	if defined(WETTERNESS) || defined(WETNESS_EFFECTS)
#		if defined(WETTERNESS)
#			define CS_EVALUATE_WETNESS_LIGHTING(wetnessNormal, lightContext, wetRoughness, lightOutput) EvaluateWetnessLighting(wetnessNormal, lightContext, wetRoughness, GetWetReflectionModeConfig(CS_WETNESS_SETTINGS.WetIndirectSpecularScale), lightOutput)
#			define CS_GET_WETNESS_INDIRECT(lobeWeights, wetnessNormal, wetRoughness, indirectContext) GetWetnessIndirectLobeWeights(lobeWeights, wetnessNormal, wetRoughness, indirectContext, GetWetReflectionModeConfig(CS_WETNESS_SETTINGS.WetIndirectSpecularScale))
#		else
#			define CS_EVALUATE_WETNESS_LIGHTING(wetnessNormal, lightContext, wetRoughness, lightOutput) EvaluateWetnessLighting(wetnessNormal, lightContext, wetRoughness, lightOutput)
#			define CS_GET_WETNESS_INDIRECT(lobeWeights, wetnessNormal, wetRoughness, indirectContext) GetWetnessIndirectLobeWeights(lobeWeights, wetnessNormal, wetRoughness, indirectContext)
#		endif
#	endif

#	if defined(WETTERNESS)
float3 NormalizeWetnessVector(float3 v, float3 fallback)
{
	float lenSq = dot(v, v);
	return (lenSq > EPSILON_DIVISION && lenSq == lenSq && lenSq < 1.0e16) ? v * rsqrt(lenSq) : fallback;
}
#	endif

#	if defined(WETTERNESS) && defined(DYNAMIC_CUBEMAPS)
float3 ApplyWetternessCubemapClarity(float3 wetCubemapIrradiance, float postRainBlend, float postRainCubemapGlareReductionFromClarity)
{
	float wetCubemapClarityAmount = saturate(postRainBlend * postRainCubemapGlareReductionFromClarity);
	if (wetCubemapClarityAmount > 1e-4) {
		float wetCubemapClarityT = sqrt(smoothstep(0.0, 1.0, wetCubemapClarityAmount));
		float wetCubemapLuma = max(1e-4, dot(max(wetCubemapIrradiance, 0.0.xxx), float3(0.2126, 0.7152, 0.0722)));
		float3 wetCubemapHue = wetCubemapIrradiance / wetCubemapLuma;
		float compressedLuma = wetCubemapLuma / (1.0 + wetCubemapClarityT * 1.8 * wetCubemapLuma);
		float3 glareReducedIrradiance = wetCubemapHue * compressedLuma;
		glareReducedIrradiance *= lerp(1.0, 1.12, wetCubemapClarityT);
		wetCubemapIrradiance = lerp(wetCubemapIrradiance, glareReducedIrradiance, wetCubemapClarityT);
	}

	return wetCubemapIrradiance;
}
#	endif

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout;

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float3 viewDirection = -normalize(input.WorldPosition.xyz);

	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.Position.xy, SharedData::FrameCount);

#	if defined(DEFERRED)
	const bool inWorld = true;
#	else
	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
#	endif
	const bool inReflection = Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection;

	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, viewPosition.z);

#	if defined(SKINNED) || !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(input.TBN0.xyz, input.TBN1.xyz, input.TBN2.xyz);

#		if !defined(TREE_ANIM) && !defined(LOD)
	// Fix incorrect vertex normals on double-sided meshes
	if (!frontFace)
		tbn = lerp(tbn, -tbn, nearFactor);
#		endif

	float3x3 tbnTr = transpose(tbn);

	tbnTr[0] = normalize(tbnTr[0]);
	tbnTr[1] = normalize(tbnTr[1]);
	tbnTr[2] = normalize(tbnTr[2]);

	tbn = transpose(tbnTr);

#	endif  // defined (SKINNED) || !defined (MODELSPACENORMALS)

#	if !defined(TRUE_PBR)
#		if defined(LANDSCAPE)
	float shininess = dot(input.LandBlendWeights1, LandscapeTexture1to4IsSpecPower) + input.LandBlendWeights2.x * LandscapeTexture5to6IsSpecPower.x + input.LandBlendWeights2.y * LandscapeTexture5to6IsSpecPower.y;
#		elif defined(SPECULAR)
	float shininess = SpecularColor.w;
#		else
	float shininess = 0.0;
#		endif  // defined (LANDSCAPE)
#	endif

#	if defined(TERRAIN_BLENDING)
	float blendFactorTerrain = 0.0;
	[flatten] if (SharedData::terrainBlendingSettings.Enabled)
	{
		float depthSampled = TerrainBlending::TerrainBlendingMaskTexture[input.Position.xy].x;

		float depthSampledLinear = SharedData::GetScreenDepth(depthSampled);
		float depthPixelLinear = SharedData::GetScreenDepth(input.Position.z);

		float terrainBlendDepth = max(0.001, 10.0 * SharedData::terrainBlendingSettings.BlendStrength);
		blendFactorTerrain = saturate((depthSampledLinear - depthPixelLinear) / terrainBlendDepth);

		if (input.Position.z == depthSampled)
			blendFactorTerrain = 1;
	}
#	endif

	float2 uv = input.TexCoord0.xy;
	float2 uvOriginal = uv;

#	if defined(EMAT)
	float parallaxShadowQuality = sqrt(1.0 - saturate(viewPosition.z / 2048.0));
#	endif

#	if defined(LANDSCAPE)
	float mipLevels[6];
#	else
	float mipLevel = 0;
#	endif  // LANDSCAPE
	float sh0 = 0;
	float pixelOffset = 0;

#	if defined(EMAT)
#		if defined(LANDSCAPE)
	DisplacementParams displacementParams[6];
	displacementParams[0].DisplacementScale = 1.f;
	displacementParams[0].DisplacementOffset = 0.f;
	displacementParams[0].HeightScale = 1;
	displacementParams[0].FlattenAmount = 0;
#		else
	DisplacementParams displacementParams;
	displacementParams.DisplacementScale = 1.f;
	displacementParams.DisplacementOffset = 0.f;
	displacementParams.HeightScale = 1;
	displacementParams.FlattenAmount = 0;
#		endif

#	endif

	float curvature = 0;
	float normalSmoothness = 0;

#	if !defined(MODELSPACENORMALS)
	float3 vertexNormal = tbnTr[2];
#		if defined(EMAT)

	if (SharedData::extendedMaterialSettings.EnableParallaxWarpingFix) {
		float3 ndx = ddx(vertexNormal);
		float3 ndy = ddy(vertexNormal);
		float3 fdx = ddx(input.WorldPosition.xyz);
		float3 fdy = ddy(input.WorldPosition.xyz);
		float fragSize = rcp(length(max(abs(fdx), abs(fdy))));
		curvature = pow(length(max(abs(ndx), abs(ndy))) * fragSize, 0.5);
		float3 flatWorldNormal = normalize(-cross(ddx(input.WorldPosition.xyz), ddy(input.WorldPosition.xyz)));
		normalSmoothness = (1 - dot(vertexNormal, flatWorldNormal));
#			if defined(LANDSCAPE)
		displacementParams[0].HeightScale = saturate(1 - curvature);
		displacementParams[0].FlattenAmount = (normalSmoothness + curvature);
#			else
		displacementParams.HeightScale = saturate(1 - curvature);
		displacementParams.FlattenAmount = (normalSmoothness + curvature);
#			endif
	}
#		endif
#	endif

	float3 entryNormal = 0;
	float3 entryNormalTS = 0;
	float eta = 1;
	float3 refractedViewDirection = viewDirection;
	float4 sampledCoatColor = PBRParams2;
	float3 complexSpecular = 1.0;  // Declare complexSpecular at a higher scope so it's available throughout the shader (NEEDED FOR STOCH. FIX)

#	if defined(EMAT)
#		if defined(PARALLAX) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
	if (SharedData::extendedMaterialSettings.EnableParallax) {
		mipLevel = ExtendedMaterials::GetMipLevel(uv, TexParallaxSampler, screenNoise);
		uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, TexParallaxSampler, SampParallaxSampler, 0, displacementParams, pixelOffset);
		if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows))
			sh0 = TexParallaxSampler.SampleLevel(SampParallaxSampler, uv, mipLevel).x;
	}
#		endif  // defined(PARALLAX) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

	bool complexMaterial = false;
	bool complexMaterialParallax = false;
	float4 complexMaterialColor = 1.0;

#		if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float4 envMaskSample = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv);
	float envMaskBase = envMaskSample.x;
	if (SharedData::extendedMaterialSettings.EnableComplexMaterial) {
		const float kMaskEpsilon = (4.0 / 255.0);

		const float4 mipSample = TexEnvMaskSampler.SampleLevel(SampEnvMaskSampler, uv, 15);
		complexMaterial = mipSample.w < (1.0 - kMaskEpsilon);

		const bool grayscaleMask = (abs(mipSample.x - mipSample.y) < kMaskEpsilon) &&
		                           (abs(mipSample.x - mipSample.z) < kMaskEpsilon) &&
		                           (abs(mipSample.y - mipSample.z) < kMaskEpsilon);
		// Preserve height-only masks while rejecting grayscale environment masks
		const bool solidBlackHeightMask = all(mipSample.xyz < kMaskEpsilon) &&
		                                  mipSample.w > kMaskEpsilon &&
		                                  mipSample.w < (1.0 - kMaskEpsilon);
		if (grayscaleMask && !solidBlackHeightMask)
			complexMaterial = false;

		if (complexMaterial) {
			if (envMaskSample.w > kMaskEpsilon && envMaskSample.w < (1.0 - kMaskEpsilon)) {
				complexMaterialParallax = true;
				mipLevel = ExtendedMaterials::GetMipLevel(uv, TexEnvMaskSampler, screenNoise);
				uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, TexEnvMaskSampler, SampTerrainParallaxSampler, 3, displacementParams, pixelOffset);
				if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows))
					sh0 = TexEnvMaskSampler.SampleLevel(SampEnvMaskSampler, uv, mipLevel).w;
				complexMaterialColor = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv);
			} else {
				complexMaterialColor = envMaskSample;
			}
			envMaskBase = complexMaterialColor.x;
		}
	}
#		endif  // ENVMAP

#		if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	bool PBRParallax = false;
	[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
	{
		float4 sampledCoatProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
		sampledCoatColor.rgb *= Color::Diffuse(sampledCoatProperties.rgb);
		sampledCoatColor.a *= sampledCoatProperties.a;
	}
#			if !defined(FACEGEN)
	[branch] if (SharedData::extendedMaterialSettings.EnableParallax && (PBRFlags & PBR::Flags::HasDisplacement) != 0)
	{
		PBRParallax = true;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			displacementParams.HeightScale = PBRParams1.y;
			displacementParams.DisplacementScale = 0.5;
			displacementParams.DisplacementOffset = -0.25;

			eta = lerp(1.0, (1 - sqrt(MultiLayerParallaxData.y)) / (1 + sqrt(MultiLayerParallaxData.y)), sampledCoatColor.w);
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				entryNormalTS = normalize(TransformNormal(TexBackLightSampler.Sample(SampBackLightSampler, uvOriginal).xyz));
			}
			else
			{
				entryNormalTS = normalize(TransformNormal(TexNormalSampler.Sample(SampNormalSampler, uvOriginal).xyz));
			}
			entryNormal = normalize(mul(tbn, entryNormalTS));
			refractedViewDirection = -refract(-viewDirection, entryNormal, eta);
		}
		else
		{
			displacementParams.HeightScale *= PBRParams1.y;
		}
		mipLevel = ExtendedMaterials::GetMipLevel(uv, TexParallaxSampler, screenNoise);
		uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, refractedViewDirection, tbnTr, screenNoise, TexParallaxSampler, SampParallaxSampler, 0, displacementParams, pixelOffset);
		if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows))
			sh0 = TexParallaxSampler.SampleLevel(SampParallaxSampler, uv, mipLevel).x;
	}
#			endif  // !FACEGEN
#		endif      // TRUE_PBR

#	elif defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float envMaskBase = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv).x;
#	endif  // EMAT

#	if defined(SNOW)
	bool useSnowSpecular = true;
#	else
	bool useSnowSpecular = false;
#	endif  // SNOW

#	if defined(SPARKLE) || !defined(PROJECTED_UV)
	bool useSnowDecalSpecular = true;
#	else
	bool useSnowDecalSpecular = false;
#	endif  // defined(SPARKLE) || !defined(PROJECTED_UV)

	float2 diffuseUv = uv;

#	if defined(SPARKLE)
	diffuseUv = ProjectedUVParams2.yy * input.TexCoord0.zw;
#	endif  // SPARKLE

#	if defined(LANDSCAPE)
	// Normalise blend weights
	float totalWeight = input.LandBlendWeights1.x + input.LandBlendWeights1.y + input.LandBlendWeights1.z +
	                    input.LandBlendWeights1.w + input.LandBlendWeights2.x + input.LandBlendWeights2.y;
	if (totalWeight > 0.0) {
		input.LandBlendWeights1 /= totalWeight;
		input.LandBlendWeights2.xy /= totalWeight;
	}
	float3 blendedRGB = 0;
	float blendedAlpha = 0;
	float3 blendedNormalRGB = 0;
	float blendedNormalAlpha = 0;

#		if defined(TRUE_PBR)
	float4 blendedRMAOS = 0;
#		endif

	// Compute stochastic offsets and derivatives once for all layers (only when terrain variation is enabled)
#		if defined(TERRAIN_VARIATION)
	bool useTerrainVariation = SharedData::terrainVariationSettings.enableTilingFix;
	// Initialise dx, dy, and sharedOffset for when Terrain Variation is disabled via enableTilingFix but still #defined
	float2 dx = 0, dy = 0;
	StochasticOffsets sharedOffset;
	sharedOffset.offset1 = float2(0, 0);
	sharedOffset.offset2 = float2(0, 0);
	sharedOffset.offset3 = float2(0, 0);
	sharedOffset.weights = float3(0, 0, 0);
	[branch] if (useTerrainVariation)
	{
		dx = ddx(input.TexCoord0.zw);
		dy = ddy(input.TexCoord0.zw);
		sharedOffset = ComputeStochasticOffsets(input.TexCoord0.zw);
	}
#		endif

#		if defined(EMAT)
#			if defined(TRUE_PBR)
	if (SharedData::extendedMaterialSettings.EnableParallax) {
#			else
	if (SharedData::extendedMaterialSettings.EnableTerrainParallax || (SharedData::extendedMaterialSettings.EnableParallax && Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement)) {
#			endif
		mipLevels[0] = ExtendedMaterials::GetMipLevel(uv, TexColorSampler, screenNoise);
		mipLevels[1] = ExtendedMaterials::GetMipLevel(uv, TexLandColor2Sampler, screenNoise);
		mipLevels[2] = ExtendedMaterials::GetMipLevel(uv, TexLandColor3Sampler, screenNoise);
		mipLevels[3] = ExtendedMaterials::GetMipLevel(uv, TexLandColor4Sampler, screenNoise);
		mipLevels[4] = ExtendedMaterials::GetMipLevel(uv, TexLandColor5Sampler, screenNoise);
		mipLevels[5] = ExtendedMaterials::GetMipLevel(uv, TexLandColor6Sampler, screenNoise);

		displacementParams[1] = displacementParams[0];
		displacementParams[2] = displacementParams[0];
		displacementParams[3] = displacementParams[0];
		displacementParams[4] = displacementParams[0];
		displacementParams[5] = displacementParams[0];
#			if defined(TRUE_PBR)
		displacementParams[0].HeightScale *= PBRParams1.y;
		displacementParams[1].HeightScale *= LandscapeTexture2PBRParams.y;
		displacementParams[2].HeightScale *= LandscapeTexture3PBRParams.y;
		displacementParams[3].HeightScale *= LandscapeTexture4PBRParams.y;
		displacementParams[4].HeightScale *= LandscapeTexture5PBRParams.y;
		displacementParams[5].HeightScale *= LandscapeTexture6PBRParams.y;
#			endif

		float weights[6];
		// Initialize weights array
		weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0.0;
#			if defined(TERRAIN_VARIATION)
		uv = ExtendedMaterials::GetParallaxCoords(input, viewPosition.z, uv, mipLevels, viewDirection, tbnTr, screenNoise, displacementParams, sharedOffset, dx, dy, pixelOffset,
			weights);
#			else
		uv = ExtendedMaterials::GetParallaxCoords(input, viewPosition.z, uv, mipLevels, viewDirection, tbnTr, screenNoise, displacementParams, pixelOffset,
			weights);
#			endif
		if (SharedData::extendedMaterialSettings.EnableHeightBlending) {
			input.LandBlendWeights1.x = weights[0];
			input.LandBlendWeights1.y = weights[1];
			input.LandBlendWeights1.z = weights[2];
			input.LandBlendWeights1.w = weights[3];
			input.LandBlendWeights2.x = weights[4];
			input.LandBlendWeights2.y = weights[5];
		}
		if (SharedData::extendedMaterialSettings.EnableShadows && (parallaxShadowQuality > 0.0f || SharedData::extendedMaterialSettings.ExtendShadows)) {
#			if defined(TERRAIN_VARIATION)
			sh0 = ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, parallaxShadowQuality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, weights);
			float shadowMultiplier = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, DirLightDirection, sh0, parallaxShadowQuality, screenNoise, displacementParams, sharedOffset, dx, dy);
#			else
			sh0 = ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, parallaxShadowQuality, input.LandBlendWeights1, input.LandBlendWeights2.xy, weights);
#			endif
		}
	}
#			if defined(TERRAIN_VARIATION)
	else if (useTerrainVariation) {
		// Calculate proper mip levels for terrain variation when parallax is disabled but EMAT is available
		mipLevels[0] = ExtendedMaterials::GetMipLevel(uv, TexColorSampler, screenNoise);
		mipLevels[1] = ExtendedMaterials::GetMipLevel(uv, TexLandColor2Sampler, screenNoise);
		mipLevels[2] = ExtendedMaterials::GetMipLevel(uv, TexLandColor3Sampler, screenNoise);
		mipLevels[3] = ExtendedMaterials::GetMipLevel(uv, TexLandColor4Sampler, screenNoise);
		mipLevels[4] = ExtendedMaterials::GetMipLevel(uv, TexLandColor5Sampler, screenNoise);
		mipLevels[5] = ExtendedMaterials::GetMipLevel(uv, TexLandColor6Sampler, screenNoise);
	}
#			endif
#		else
	// Initialize mip levels for non-EMAT case
	mipLevels[0] = mipLevels[1] = mipLevels[2] = mipLevels[3] = mipLevels[4] = mipLevels[5] = 0.0;
#		endif  // EMAT
#	endif      // LANDSCAPE

#	if defined(SPARKLE)
	diffuseUv = ProjectedUVParams2.yy * (input.TexCoord0.zw + (uv - uvOriginal));
#	else
	diffuseUv = uv;
#	endif  // SPARKLE

	float4 baseColor = 0;
	float4 normal = 0;
	float glossiness = 0;
#	if defined(CS_SKIN)
	const bool skinEnabled = SharedData::skinData.skinParams.w > 0.0f;
#		if defined(SKIN)
	float skinRoughness = 0;
	float skinSpecular = 0;
	float skinFuzzMask = 1;
	float skinWetMask = 1;
	float skinAO = 1;
	bool skinRoughnessSet = false;
#		endif
#	endif

	float4 rawRMAOS = 0;

	float4 glintParameters = 0;

#	if defined(SNOW)  // Earlier snow definition for Terrain Variation rework.
#		if !defined(TRUE_PBR)
	float landSnowMask = 0.0;
#			if defined(LANDSCAPE)
	landSnowMask = GetLandSnowMaskValue(baseColor.w);
#			endif
#		endif
#	endif

#	if defined(LANDSCAPE)
	// Layer 1 (LandBlendWeights1.x)
	if (input.LandBlendWeights1.x > 0.01) {
		float weight = input.LandBlendWeights1.x;

		// Sample diffuse texture for layer 1
#		if defined(TERRAIN_VARIATION)
		float4 landColor1;
		[branch] if (useTerrainVariation)
		{
			landColor1 = StochasticEffect(TexColorSampler, SampColorSampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor1 = TexColorSampler.SampleBias(SampColorSampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor1 = TexColorSampler.SampleBias(SampColorSampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB1 = landColor1.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile0PBR) == 0)
		{
			landColorRGB1 = Color::SrgbToLinear(landColorRGB1 / Color::PBRLightingScale);
		}
#		endif
		float landAlpha1 = landColor1.a;
		float landSnowMask1 = GetLandSnowMaskValue(landColor1.w);

		// Sample normal texture for layer 1
#		if defined(TERRAIN_VARIATION)
		float4 landNormal1;
		[branch] if (useTerrainVariation)
		{
			landNormal1 = StochasticEffect(TexNormalSampler, SampNormalSampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal1 = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal1 = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB1 = landNormal1.rgb;
		float landNormalAlpha1 = landNormal1.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.x * input.LandBlendWeights1.x * landSnowMask1;
#		endif  // SNOW

		// Sample RMAOS texture for layer 1
#		if defined(TRUE_PBR)
		float4 landRMAOS1;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile0PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS1 = StochasticEffect(TexRMAOSSampler, SampRMAOSSampler, uv, sharedOffset, dx, dy) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
			}
			else
			{
				landRMAOS1 = TexRMAOSSampler.SampleBias(SampRMAOSSampler, uv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
			}
#			else
			landRMAOS1 = TexRMAOSSampler.SampleBias(SampRMAOSSampler, uv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile0HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture1GlintParameters;
			}
		}
		else
		{
			landRMAOS1 = input.LandBlendWeights1.x * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS1 * weight;
#		endif
		blendedRGB += landColorRGB1 * weight;
		blendedAlpha += landAlpha1 * weight;
		blendedNormalRGB += landNormalRGB1 * weight;
		blendedNormalAlpha += landNormalAlpha1 * weight;
	}

	// Layer 2 (LandBlendWeights1.y)
	if (input.LandBlendWeights1.y > 0.01) {
		float weight = input.LandBlendWeights1.y;

		// Sample diffuse texture for layer 2
#		if defined(TERRAIN_VARIATION)
		float4 landColor2;
		[branch] if (useTerrainVariation)
		{
			landColor2 = StochasticEffect(TexLandColor2Sampler, SampLandColor2Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor2 = TexLandColor2Sampler.SampleBias(SampLandColor2Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor2 = TexLandColor2Sampler.SampleBias(SampLandColor2Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB2 = landColor2.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1PBR) == 0)
		{
			landColorRGB2 = Color::SrgbToLinear(landColorRGB2 / Color::PBRLightingScale);
		}
#		endif
		float landAlpha2 = landColor2.a;
		float landSnowMask2 = GetLandSnowMaskValue(landColor2.w);

		// Sample normal texture for layer 2
#		if defined(TERRAIN_VARIATION)
		float4 landNormal2;
		[branch] if (useTerrainVariation)
		{
			landNormal2 = StochasticEffect(TexLandNormal2Sampler, SampLandNormal2Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal2 = TexLandNormal2Sampler.SampleBias(SampLandNormal2Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal2 = TexLandNormal2Sampler.SampleBias(SampLandNormal2Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB2 = landNormal2.rgb;
		float landNormalAlpha2 = landNormal2.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.y * input.LandBlendWeights1.y * landSnowMask2;
#		endif  // SNOW

		// Sample RMAOS texture for layer 2
#		if defined(TRUE_PBR)
		float4 landRMAOS2;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS2 = StochasticEffect(TexLandRMAOS2Sampler, SampLandRMAOS2Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture2PBRParams.x, 1, 1, LandscapeTexture2PBRParams.z);
			}
			else
			{
				landRMAOS2 = TexLandRMAOS2Sampler.SampleBias(SampLandRMAOS2Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture2PBRParams.x, 1, 1, LandscapeTexture2PBRParams.z);
			}
#			else
			landRMAOS2 = TexLandRMAOS2Sampler.SampleBias(SampLandRMAOS2Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture2PBRParams.x, 1, 1, LandscapeTexture2PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile1HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture2GlintParameters;
			}
		}
		else
		{
			landRMAOS2 = input.LandBlendWeights1.y * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS2 * weight;
#		endif
		blendedRGB += landColorRGB2 * weight;
		blendedAlpha += landAlpha2 * weight;
		blendedNormalRGB += landNormalRGB2 * weight;
		blendedNormalAlpha += landNormalAlpha2 * weight;
	}

	// Layer 3 (LandBlendWeights1.z)
	if (input.LandBlendWeights1.z > 0.01) {
		float weight = input.LandBlendWeights1.z;
		// Sample diffuse texture for layer 3
#		if defined(TERRAIN_VARIATION)
		float4 landColor3;
		[branch] if (useTerrainVariation)
		{
			landColor3 = StochasticEffect(TexLandColor3Sampler, SampLandColor3Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor3 = TexLandColor3Sampler.SampleBias(SampLandColor3Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor3 = TexLandColor3Sampler.SampleBias(SampLandColor3Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB3 = landColor3.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2PBR) == 0)
		{
			landColorRGB3 = Color::SrgbToLinear(landColorRGB3 / Color::PBRLightingScale);
		}
#		endif
		float landAlpha3 = landColor3.a;
		float landSnowMask3 = GetLandSnowMaskValue(landColor3.w);

		// Sample normal texture for layer 3
#		if defined(TERRAIN_VARIATION)
		float4 landNormal3;
		[branch] if (useTerrainVariation)
		{
			landNormal3 = StochasticEffect(TexLandNormal3Sampler, SampLandNormal3Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal3 = TexLandNormal3Sampler.SampleBias(SampLandNormal3Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal3 = TexLandNormal3Sampler.SampleBias(SampLandNormal3Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB3 = landNormal3.rgb;
		float landNormalAlpha3 = landNormal3.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.z * input.LandBlendWeights1.z * landSnowMask3;
#		endif  // SNOW

		// Sample RMAOS texture for layer 3
#		if defined(TRUE_PBR)
		float4 landRMAOS3;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS3 = StochasticEffect(TexLandRMAOS3Sampler, SampLandRMAOS3Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture3PBRParams.x, 1, 1, LandscapeTexture3PBRParams.z);
			}
			else
			{
				landRMAOS3 = TexLandRMAOS3Sampler.SampleBias(SampLandRMAOS3Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture3PBRParams.x, 1, 1, LandscapeTexture3PBRParams.z);
			}
#			else
			landRMAOS3 = TexLandRMAOS3Sampler.SampleBias(SampLandRMAOS3Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture3PBRParams.x, 1, 1, LandscapeTexture3PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile2HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture3GlintParameters;
			}
		}
		else
		{
			landRMAOS3 = input.LandBlendWeights1.z * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS3 * weight;
#		endif
		blendedRGB += landColorRGB3 * weight;
		blendedAlpha += landAlpha3 * weight;
		blendedNormalRGB += landNormalRGB3 * weight;
		blendedNormalAlpha += landNormalAlpha3 * weight;
	}
	// Layer 4 (LandBlendWeights1.w)
	if (input.LandBlendWeights1.w > 0.01) {
		float weight = input.LandBlendWeights1.w;

		// Sample diffuse texture for layer 4
#		if defined(TERRAIN_VARIATION)
		float4 landColor4;
		[branch] if (useTerrainVariation)
		{
			landColor4 = StochasticEffect(TexLandColor4Sampler, SampLandColor4Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor4 = TexLandColor4Sampler.SampleBias(SampLandColor4Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor4 = TexLandColor4Sampler.SampleBias(SampLandColor4Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB4 = landColor4.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3PBR) == 0)
		{
			landColorRGB4 = Color::SrgbToLinear(landColorRGB4 / Color::PBRLightingScale);
		}
#		endif
		float landAlpha4 = landColor4.a;
		float landSnowMask4 = GetLandSnowMaskValue(landColor4.w);

		// Sample normal texture for layer 4
#		if defined(TERRAIN_VARIATION)
		float4 landNormal4;
		[branch] if (useTerrainVariation)
		{
			landNormal4 = StochasticEffect(TexLandNormal4Sampler, SampLandNormal4Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal4 = TexLandNormal4Sampler.SampleBias(SampLandNormal4Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal4 = TexLandNormal4Sampler.SampleBias(SampLandNormal4Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB4 = landNormal4.rgb;
		float landNormalAlpha4 = landNormal4.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture1to4IsSnow.w * input.LandBlendWeights1.w * landSnowMask4;
#		endif  // SNOW

		// Sample RMAOS texture for layer 4
#		if defined(TRUE_PBR)
		float4 landRMAOS4;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS4 = StochasticEffect(TexLandRMAOS4Sampler, SampLandRMAOS4Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture4PBRParams.x, 1, 1, LandscapeTexture4PBRParams.z);
			}
			else
			{
				landRMAOS4 = TexLandRMAOS4Sampler.SampleBias(SampLandRMAOS4Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture4PBRParams.x, 1, 1, LandscapeTexture4PBRParams.z);
			}
#			else
			landRMAOS4 = TexLandRMAOS4Sampler.SampleBias(SampLandRMAOS4Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture4PBRParams.x, 1, 1, LandscapeTexture4PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile3HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture4GlintParameters;
			}
		}
		else
		{
			landRMAOS4 = input.LandBlendWeights1.w * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS4 * weight;
#		endif
		blendedRGB += landColorRGB4 * weight;
		blendedAlpha += landAlpha4 * weight;
		blendedNormalRGB += landNormalRGB4 * weight;
		blendedNormalAlpha += landNormalAlpha4 * weight;
	}

	// Layer 5 (LandBlendWeights2.x)
	if (input.LandBlendWeights2.x > 0.01) {
		float weight = input.LandBlendWeights2.x;
		// Sample diffuse texture for layer 5
#		if defined(TERRAIN_VARIATION)
		float4 landColor5;
		[branch] if (useTerrainVariation)
		{
			landColor5 = StochasticEffect(TexLandColor5Sampler, SampLandColor5Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor5 = TexLandColor5Sampler.SampleBias(SampLandColor5Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor5 = TexLandColor5Sampler.SampleBias(SampLandColor5Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB5 = landColor5.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4PBR) == 0)
		{
			landColorRGB5 = Color::SrgbToLinear(landColorRGB5 / Color::PBRLightingScale);
		}
#		endif
		float landAlpha5 = landColor5.a;
		float landSnowMask5 = GetLandSnowMaskValue(landColor5.w);

		// Sample normal texture for layer 5
#		if defined(TERRAIN_VARIATION)
		float4 landNormal5;
		[branch] if (useTerrainVariation)
		{
			landNormal5 = StochasticEffect(TexLandNormal5Sampler, SampLandNormal5Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal5 = TexLandNormal5Sampler.SampleBias(SampLandNormal5Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal5 = TexLandNormal5Sampler.SampleBias(SampLandNormal5Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB5 = landNormal5.rgb;
		float landNormalAlpha5 = landNormal5.a;

#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture5to6IsSnow.x * input.LandBlendWeights2.x * landSnowMask5;
#		endif  // SNOW

		// Sample RMAOS texture for layer 5
#		if defined(TRUE_PBR)
		float4 landRMAOS5;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS5 = StochasticEffect(TexLandRMAOS5Sampler, SampLandRMAOS5Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture5PBRParams.x, 1, 1, LandscapeTexture5PBRParams.z);
			}
			else
			{
				landRMAOS5 = TexLandRMAOS5Sampler.SampleBias(SampLandRMAOS5Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture5PBRParams.x, 1, 1, LandscapeTexture5PBRParams.z);
			}
#			else
			landRMAOS5 = TexLandRMAOS5Sampler.SampleBias(SampLandRMAOS5Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture5PBRParams.x, 1, 1, LandscapeTexture5PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile4HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture5GlintParameters;
			}
		}
		else
		{
			landRMAOS5 = input.LandBlendWeights2.x * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS5 * weight;
#		endif
		blendedRGB += landColorRGB5 * weight;
		blendedAlpha += landAlpha5 * weight;
		blendedNormalRGB += landNormalRGB5 * weight;
		blendedNormalAlpha += landNormalAlpha5 * weight;
	}
	// Layer 6 (LandBlendWeights2.y)
	if (input.LandBlendWeights2.y > 0.01) {
		float weight = input.LandBlendWeights2.y;

		// Sample layer 6 textures
#		if defined(TERRAIN_VARIATION)
		float4 landColor6;
		[branch] if (useTerrainVariation)
		{
			landColor6 = StochasticEffect(TexLandColor6Sampler, SampLandColor6Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landColor6 = TexLandColor6Sampler.SampleBias(SampLandColor6Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landColor6 = TexLandColor6Sampler.SampleBias(SampLandColor6Sampler, uv, SharedData::MipBias);
#		endif
		float3 landColorRGB6 = landColor6.rgb;
#		if defined(TRUE_PBR)
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5PBR) == 0)
		{
			landColorRGB6 = Color::SrgbToLinear(landColorRGB6 / Color::PBRLightingScale);
		}
#		endif
		float landAlpha6 = landColor6.a;
		float landSnowMask6 = GetLandSnowMaskValue(landColor6.w);

		// Sample normal texture for layer 6
#		if defined(TERRAIN_VARIATION)
		float4 landNormal6;
		[branch] if (useTerrainVariation)
		{
			landNormal6 = StochasticEffect(TexLandNormal6Sampler, SampLandNormal6Sampler, uv, sharedOffset, dx, dy);
		}
		else
		{
			landNormal6 = TexLandNormal6Sampler.SampleBias(SampLandNormal6Sampler, uv, SharedData::MipBias);
		}
#		else
		float4 landNormal6 = TexLandNormal6Sampler.SampleBias(SampLandNormal6Sampler, uv, SharedData::MipBias);
#		endif
		float3 landNormalRGB6 = landNormal6.rgb;
		float landNormalAlpha6 = landNormal6.a;
#		if defined(SNOW) && !defined(TRUE_PBR)
		landSnowMask += LandscapeTexture5to6IsSnow.y * input.LandBlendWeights2.y * landSnowMask6;
#		endif  // SNOW

		// Sample RMAOS texture for layer 6
#		if defined(TRUE_PBR)
		float4 landRMAOS6;
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5PBR) != 0)
		{
#			if defined(TERRAIN_VARIATION)
			[branch] if (useTerrainVariation)
			{
				landRMAOS6 = StochasticEffect(TexLandRMAOS6Sampler, SampLandRMAOS6Sampler, uv, sharedOffset, dx, dy) * float4(LandscapeTexture6PBRParams.x, 1, 1, LandscapeTexture6PBRParams.z);
			}
			else
			{
				landRMAOS6 = TexLandRMAOS6Sampler.SampleBias(SampLandRMAOS6Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture6PBRParams.x, 1, 1, LandscapeTexture6PBRParams.z);
			}
#			else
			landRMAOS6 = TexLandRMAOS6Sampler.SampleBias(SampLandRMAOS6Sampler, uv, SharedData::MipBias) * float4(LandscapeTexture6PBRParams.x, 1, 1, LandscapeTexture6PBRParams.z);
#			endif
			if ((PBRFlags & PBR::TerrainFlags::LandTile5HasGlint) != 0) {
				glintParameters += weight * LandscapeTexture6GlintParameters;
			}
		}
		else
		{
			landRMAOS6 = input.LandBlendWeights2.y * float4(1 - glossiness.x, 0, 1, 0);
		}
		blendedRMAOS += landRMAOS6 * weight;
#		endif
		blendedRGB += landColorRGB6 * weight;
		blendedAlpha += landAlpha6 * weight;
		blendedNormalRGB += landNormalRGB6 * weight;
		blendedNormalAlpha += landNormalAlpha6 * weight;
	}

	float4 rawBaseColor = float4(blendedRGB, blendedAlpha);
	baseColor = float4(Color::Diffuse(blendedRGB), blendedAlpha);
	normal = float4(blendedNormalRGB, blendedNormalAlpha);
#		if defined(TRUE_PBR)
	rawRMAOS = blendedRMAOS;
#		endif
#	else  // Non-landscape code
	float4 rawBaseColor = TexColorSampler.SampleBias(SampColorSampler, diffuseUv, SharedData::MipBias);
	baseColor = float4(Color::Diffuse(rawBaseColor.rgb), rawBaseColor.a);
	float4 normalColor = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
	normal = normalColor;
#		if defined(TRUE_PBR)
	rawRMAOS = TexRMAOSSampler.SampleBias(SampRMAOSSampler, diffuseUv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
	if ((PBRFlags & PBR::Flags::Glint) != 0) {
		glintParameters = MultiLayerParallaxData;
	}
#		endif
#	endif

#	if defined(LOD_BLENDING)
#		if defined(LODOBJECTS) || defined(LODOBJECTSHD)
	baseColor.xyz = pow(abs(baseColor.xyz), SharedData::lodBlendingSettings.LODObjectGamma) * SharedData::lodBlendingSettings.LODObjectBrightness;
#		elif defined(LODLANDSCAPE)
// First apply terrain variation if enabled
#			if defined(TERRAIN_VARIATION)
	if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix) {
		float2 dx = ddx(uv);
		float2 dy = ddy(uv);
		StochasticOffsets lodOffset = ComputeStochasticOffsetsLOD(uv);
		float4 lodStochasticColor = StochasticSampleLOD(screenNoise, TexColorSampler, SampColorSampler, uv, lodOffset, dx, dy);

		// Apply the stochastic result directly
		baseColor.xyz = Color::Diffuse(lodStochasticColor.rgb);
	}
#			endif
	baseColor.xyz = pow(abs(baseColor.xyz), SharedData::lodBlendingSettings.LODTerrainGamma) * SharedData::lodBlendingSettings.LODTerrainBrightness;
#		endif
#	endif  // LOD_BLENDING

#	if defined(SKIN) && defined(CS_SKIN)
	float4 skinsk = 0;
	float4 skinExtra = 0;
	float4 skinWetnessSample = 0;
	uint2 skinExtraDimensions = uint2(0, 0);
	uint2 wetnessDimensions = uint2(0, 0);
	bool hasSkinExtra = false;
	bool hasSkinWetness = false;
	if (skinEnabled) {
		skinsk = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
		TexSkinExtraSampler.GetDimensions(skinExtraDimensions.x, skinExtraDimensions.y);
		TexSkinWetnessSampler.GetDimensions(wetnessDimensions.x, wetnessDimensions.y);
		hasSkinExtra = skinExtraDimensions.x > 32 && skinExtraDimensions.y > 32;
		hasSkinWetness = wetnessDimensions.x > 32 && wetnessDimensions.y > 32;
	}
	float4 skinWetnessNormal = float4(0.f, 0.f, 0.f, 1.f);

	if (hasSkinExtra && SharedData::skinData.skinParams.x > 0.0f) {
		skinExtra = TexSkinExtraSampler.Sample(SampColorSampler, uv);
		skinRoughness = skinExtra.x;
		skinFuzzMask = skinExtra.y;
		skinAO = skinExtra.z;
		skinSpecular = skinExtra.w;
		skinRoughnessSet = true;
	} else {
		skinRoughnessSet = false;
	}
	if (hasSkinWetness && skinEnabled) {
		skinWetnessSample = TexSkinWetnessSampler.Sample(SampColorSampler, uv);
		if ((skinWetnessSample.y == 0 && skinWetnessSample.z == 0) || (skinWetnessSample.x == skinWetnessSample.y && skinWetnessSample.y == skinWetnessSample.z && skinWetnessSample.w >= 0.99f)) {
			skinWetMask = skinWetnessSample.x;
			skinWetnessNormal.xyz = CalculateNormalFromHeight(skinWetMask, SharedData::skinData.wetParams.w * 0.0001, uv) * 0.5 + 0.5;
		} else {
			skinWetnessNormal.xyz = skinWetnessSample.xyz;
			skinWetMask = skinWetnessSample.w;
		}
	} else {
		skinWetMask = 1.0;
	}
#	endif

	float landSnowMask1 = GetLandSnowMaskValue(baseColor.w);

#	if defined(MODELSPACENORMALS)
#		if defined(LODLANDNOISE)
	normal.xyz = normal.xzy - 0.5.xxx;
	float lodLandNoiseParameter = GetLodLandBlendParameter(baseColor.xyz);
	float noise = TexLandLodNoiseSampler.Sample(SampLandLodNoiseSampler, uv * 3.0.xx).x;
	float lodLandNoiseMultiplier = GetLodLandBlendMultiplier(lodLandNoiseParameter, noise);
	baseColor.xyz *= lodLandNoiseMultiplier;
	normal.xyz *= 2;
	normal.w = 1;
	glossiness = 0;
#		elif defined(LODLANDSCAPE)
	normal.xyz = 2.0.xxx * (-0.5.xxx + normal.xzy);
	normal.w = 1;
	glossiness = 0;
#		else
	normal.xyz = normal.xzy * 2.0.xxx + -1.0.xxx;
	normal.w = 1;
	glossiness = TexSpecularSampler.Sample(SampSpecularSampler, uv).x;
#		endif  // LODLANDNOISE
#	elif (defined(SNOW) && defined(LANDSCAPE))
	normal.xyz = GetLandNormal(landSnowMask1, normal.xyz, uv, SampNormalSampler, TexNormalSampler);
	glossiness = normal.w;
#	else
	normal.xyz = TransformNormal(normal.xyz);
	glossiness = normal.w;
#	endif  // MODELSPACENORMALS

#	if defined(WORLD_MAP)
	normal.xyz = GetWorldMapNormal(input, normal.xyz, rawBaseColor.xyz);
#	endif  // WORLD_MAP

#	if defined(LANDSCAPE)
#		if defined(SNOW) && !defined(TRUE_PBR)
	landSnowMask = LandscapeTexture1to4IsSnow.x * input.LandBlendWeights1.x;
#		endif  // SNOW
#	endif      // LANDSCAPE

#	if defined(EMAT_ENVMAP)
	complexMaterial = complexMaterial && complexMaterialColor.y > (4.0 / 255.0);
	shininess = lerp(shininess, shininess * complexMaterialColor.y, complexMaterial);
	if (complexMaterial) {
		complexSpecular = lerp(1.0, baseColor.xyz, complexMaterialColor.z);
		baseColor.xyz = lerp(baseColor.xyz, 0.0, complexMaterialColor.z);
	}
#	endif  // defined (EMAT) && defined(ENVMAP)

#	if defined(FACEGEN)
	if (!SharedData::linearLightingSettings.enableLinearLighting) {
		baseColor.xyz = GetFacegenBaseColor(baseColor.xyz, uv);
	} else {
		baseColor.xyz = Color::SkyrimGammaToLinear(GetFacegenBaseColor(Color::LinearToSkyrimGamma(baseColor.xyz), uv));
	}
#	elif defined(FACEGEN_RGB_TINT)
	if (!SharedData::linearLightingSettings.enableLinearLighting) {
		baseColor.xyz = GetFacegenRGBTintBaseColor(baseColor.xyz, uv);
	} else {
		baseColor.xyz = Color::SkyrimGammaToLinear(GetFacegenRGBTintBaseColor(Color::LinearToSkyrimGamma(baseColor.xyz), uv));
	}
#	endif  // FACEGEN

#	if defined(SKIN) && defined(CS_SKIN)
	if (skinEnabled) {
		baseColor.xyz = baseColor.xyz * SharedData::skinData.skinParams2.w;
	}
#	endif  // CS_SKIN

#	if defined(HAIR) && defined(CS_HAIR)
	float3 hairTint = 0;

	if (SharedData::hairSpecularSettings.Enabled) {
		hairTint = lerp(1, Color::Diffuse(TintColor.xyz), Color::ColorToLinear(input.Color.y));
		baseColor.xyz *= hairTint;
		baseColor.xyz = Hair::Saturation(baseColor.xyz, SharedData::hairSpecularSettings.HairSaturation);
		baseColor.xyz *= SharedData::hairSpecularSettings.BaseColorMult;
		baseColor.xyz = SharedData::hairSpecularSettings.HairMode == 1 ? baseColor.xyz * baseColor.xyz : baseColor.xyz;  // To match color for Marschner
	}

	float3 sampledHairFlow = 0;
	bool useHairFlowMap = false;
#		if defined(BACK_LIGHTING)
	if (SharedData::hairSpecularSettings.Enabled) {
		uint2 hairFlowDimensions = uint2(0, 0);
		sampledHairFlow = float3(TexBackLightSampler.Sample(SampBackLightSampler, uv).xy, 0.5f);
		TexBackLightSampler.GetDimensions(hairFlowDimensions.x, hairFlowDimensions.y);
		useHairFlowMap = (sampledHairFlow.x > 0.0 || sampledHairFlow.y > 0.0) && hairFlowDimensions.x > 32 && hairFlowDimensions.y > 32;
		sampledHairFlow = useHairFlowMap ? sampledHairFlow * 2.0f - 1.0f : float3(0.5f, 0.5f, 0.5f);
	}
#		endif
#	endif

#	if defined(LOD_LAND_BLEND)
	float4 lodLandColor;

#		if defined(TERRAIN_VARIATION)
	if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix) {
		// Apply stochastic sampling to LOD_LAND_BLEND color texture
		float2 blendColorUV = input.TexCoord0.zw;
		float2 dx = ddx(blendColorUV);
		float2 dy = ddy(blendColorUV);
		StochasticOffsets lodBlendColorOffset = ComputeStochasticOffsetsLOD(blendColorUV);
		lodLandColor = StochasticSampleLOD(screenNoise, TexLandLodBlend1Sampler, SampLandLodBlend1Sampler, blendColorUV, lodBlendColorOffset, dx, dy);
	} else {
		lodLandColor = TexLandLodBlend1Sampler.Sample(SampLandLodBlend1Sampler, input.TexCoord0.zw);
	}
#		else
	lodLandColor = TexLandLodBlend1Sampler.Sample(SampLandLodBlend1Sampler, input.TexCoord0.zw);
#		endif

	lodLandColor.xyz = Color::ColorToLinear(lodLandColor.xyz) * Color::VanillaDiffuseColorMult();
#		if defined(LOD_BLENDING)
	lodLandColor.xyz = pow(abs(lodLandColor.xyz), SharedData::lodBlendingSettings.LODTerrainGamma) * SharedData::lodBlendingSettings.LODTerrainBrightness;
#		endif  // LOD_BLENDING
	float lodBlendParameter = GetLodLandBlendParameter(lodLandColor.xyz);
	float lodBlendMask = TexLandLodBlend2Sampler.Sample(SampLandLodBlend2Sampler, 3.0.xx * input.TexCoord0.zw).x;
	float lodLandFadeFactor = GetLodLandBlendMultiplier(lodBlendParameter, lodBlendMask);
	float lodLandBlendFactor = LODTexParams.z * input.LandBlendWeights2.w;
	normal.xyz = lerp(normal.xyz, float3(0, 0, 1), lodLandBlendFactor);

#		if !defined(TRUE_PBR)
	baseColor.w = 0;
	baseColor = lerp(baseColor, lodLandColor * lodLandFadeFactor, lodLandBlendFactor);
	glossiness = lerp(glossiness, 0, lodLandBlendFactor);
#		endif
#	endif  // LOD_LAND_BLEND

#	if defined(SNOW) && !defined(TRUE_PBR)
	useSnowSpecular = landSnowMask != 0.0;
#	endif  // SNOW

#	if defined(BACK_LIGHTING)
	float4 backLightColor = TexBackLightSampler.Sample(SampBackLightSampler, uv);
#		if defined(HAIR) && defined(CS_HAIR)
	if (useHairFlowMap) {
		backLightColor = 0.0f;
	}
#		endif
#	endif  // BACK_LIGHTING

#	if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING) || defined(LOAD_SOFT_LIGHTING))
	float4 rimSoftLightColor = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
#	endif  // RIM_LIGHTING || SOFT_LIGHTING

	uint numLights = min(7, uint(NumLightNumShadowLight.x));
	uint numShadowLights = min(4, uint(NumLightNumShadowLight.y));

#	if defined(MODELSPACENORMALS) && !defined(SKINNED)
	float3 worldNormal = normal.xyz;
	float3x3 tbnTr = ReconstructTBN(input.WorldPosition.xyz, worldNormal, screenUV);
#	else
	float3 worldNormal = normalize(mul(tbn, normal.xyz));
#		if defined(TREE_ANIM)
	float3 viewNormal = normalize(FrameBuffer::WorldToView(worldNormal, false));
	viewNormal = float3(viewNormal.xy, -abs(viewNormal.z));
	worldNormal = normalize(FrameBuffer::ViewToWorld(viewNormal, false));
#		endif

#		if defined(SPARKLE)
	float3 projectedNormal = normalize(mul(tbn, float3(ProjectedUVParams2.xx * normal.xy, normal.z)));
#		endif  // SPARKLE
#	endif      // defined (MODELSPACENORMALS) && !defined (SKINNED)

#	if defined(SKIN) && defined(CS_SKIN)
#		if defined(WETTERNESS) || defined(WETNESS_EFFECTS)
	float3 skinWetNormal = worldNormal.xyz;
#			if defined(FACEGEN)
	float2 wetUV = uv;
#			else
	float2 wetUV = uv * SharedData::skinData.skinDetailParams.y;
#			endif
	float2 dynamicWet = Skin::GetWetness(input.WorldPosition.z + FrameBuffer::CameraPosAdjust.z, worldNormal.xyz);
	float skinWetness = Skin::PerlinNoise(wetUV, SharedData::skinData.wetParams.x, SharedData::skinData.wetParams.y, SharedData::skinData.wetParams.z, clamp(dynamicWet.x + dynamicWet.y + SharedData::skinData.skinParams2.y, 0.f, 2.f) * (hasSkinWetness ? 1.0 : 0.5));
	if ((SharedData::skinData.skinDetailParams.w > 0.0f || skinWetness > 0.0f) && skinEnabled)
#		else
	if (SharedData::skinData.skinDetailParams.w > 0.0f && skinEnabled)
#		endif
	{
#		if defined(FACEGEN)
		float2 detailUV = input.TexCoord0.xy * SharedData::skinData.skinDetailParams.x;
#		else
		float2 detailUV = input.TexCoord0.xy * SharedData::skinData.skinDetailParams.x * SharedData::skinData.skinDetailParams.y;
#		endif  // FACEGEN
#		if defined(MODELSPACENORMALS)
		const float3x3 tbnTr = ReconstructTBN(input.WorldPosition.xyz, worldNormal, screenUV);
		const float3x3 tbn = transpose(tbnTr);
		const float3 tangentNormal = mul(tbnTr, worldNormal.xyz);
#		else
		const float3 tangentNormal = normal.xyz;
#		endif  // MODELSPACENORMALS
		float3 detailNormal = float3(Skin::TexSkinDetailNormal.SampleBias(SampNormalSampler, detailUV, SharedData::MipBias - 1.0f).xy, 0.5f);
		skinAO *= Skin::TexSkinDetailNormal.Sample(SampNormalSampler, detailUV).w;
		detailNormal = (detailNormal * 2.0 - 1.0) * SharedData::skinData.skinDetailParams.z;
		float3 combinedTangentNormal = normalize(float3(ReorientNormal(detailNormal, tangentNormal).xy, tangentNormal.z));
		float3 combinedNormal = normalize(mul(tbn, combinedTangentNormal));
		if (SharedData::skinData.skinDetailParams.w > 0.0f)
			worldNormal.xyz = combinedNormal;
#		if defined(WETTERNESS) || defined(WETNESS_EFFECTS)
		if (skinWetness > 0.0f) {
			float3 wetNormal = CalculateNormalFromHeight(skinWetness, SharedData::skinData.wetParams.w * 0.0005, uv);
			if (hasSkinWetness) {
				float3 wetMaskNormal = (skinWetnessNormal.xyz * 2.0 - 1.0);
				wetNormal = ReorientNormal(wetMaskNormal, wetNormal);
			}
			if (SharedData::skinData.skinParams2.y > 1.0f) {
				wetNormal = lerp(wetNormal, tangentNormal, saturate(SharedData::skinData.skinParams2.y - 1.0f));
			}
			float3 combinedWetNormal = skinWetMask ? wetNormal : combinedTangentNormal;
			skinWetNormal = normalize(mul(tbn, combinedWetNormal));
			skinWetNormal = lerp(worldNormal.xyz, skinWetNormal, skinWetness > 0 ? 1 : 0);
		}
#		endif
	}
#	endif  // CS_SKIN

	float2 baseShadowUV = 1.0.xx;
	float4 shadowColor = 1.0;
	if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir) || inWorld) || numShadowLights > 0) {
		baseShadowUV = input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy;
		float2 adjustedShadowUV = baseShadowUV * VPOSOffset.xy + VPOSOffset.zw;
		float2 shadowUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(adjustedShadowUV);
		shadowColor = TexShadowMaskSampler.Sample(SampShadowMaskSampler, shadowUV);
	}

	float projectedMaterialWeight = 0;

	float projWeight = 0;

#	if defined(PROJECTED_UV)
	float3 projWorldPos = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
	float3 triFaceNormal = normalize(-cross(ddx(input.WorldPosition.xyz), ddy(input.WorldPosition.xyz)));
	float3 triWeights = Triplanar::GetWeights(tbnTr[2], triFaceNormal);
	float projNoise = Triplanar::Sample(TexCharacterLightProjNoiseSampler, SampCharacterLightProjNoiseSampler, projWorldPos, triWeights, ProjectedUVParams.z).x;
	float3 texProj = normalize(input.TexProj);
#		if defined(TREE_ANIM) || defined(LODOBJECTSHD)
	float vertexAlpha = 1;
#		else
	float vertexAlpha = input.Color.w;
#		endif  // defined (TREE_ANIM) || defined (LODOBJECTSHD)
	projWeight = -ProjectedUVParams.x * projNoise + (dot(worldNormal.xyz, texProj) * vertexAlpha - ProjectedUVParams.w);
#		if defined(LODOBJECTSHD)
	projWeight += (-0.5 + input.Color.w) * 2.5;
#		endif  // LODOBJECTSHD
#		if defined(SPARKLE)
	if (projWeight < 0)
		discard;

	rawBaseColor = Triplanar::SampleStochasticBias(TexColorSampler, SampColorSampler, projWorldPos, triWeights, ProjectedUVParams2.y, SharedData::MipBias, screenNoise);
	baseColor = float4(Color::Diffuse(rawBaseColor.rgb), rawBaseColor.a);
	worldNormal.xyz = projectedNormal;
#		elif !defined(FACEGEN) && !defined(MULTI_LAYER_PARALLAX) && !defined(PARALLAX) && !defined(SPARKLE)
	if (ProjectedUVParams3.w > 0.5) {
		float diffuseNormalScale = ProjectedUVParams3.x * ProjectedUVParams.z;
		float3 projNormal = TransformNormal(Triplanar::SampleStochastic(TexProjNormalSampler, SampProjNormalSampler, projWorldPos, triWeights, diffuseNormalScale, screenNoise).xyz);
		float detailNormalScale = ProjectedUVParams3.y * ProjectedUVParams.z;
		float3 projDetailNormal = Triplanar::SampleStochastic(TexProjDetail, SampProjDetailSampler, projWorldPos, triWeights, detailNormalScale, screenNoise).xyz;
		float3 finalProjNormal = normalize(TransformNormal(projDetailNormal) * float3(1, 1, projNormal.z) + float3(projNormal.xy, 0));
		float3 projBaseColor = Color::ColorToLinear(Triplanar::SampleStochastic(TexProjDiffuseSampler, SampProjDiffuseSampler, projWorldPos, triWeights, diffuseNormalScale, screenNoise).xyz) * Color::ColorToLinear(ProjectedUVParams2.xyz);
		projectedMaterialWeight = smoothstep(0, 1, 5 * (0.1 + projWeight));
#			if defined(TRUE_PBR)
		projBaseColor = max(0, projBaseColor.xyz * MaterialObjectRGBScale);
		rawRMAOS.xyw = lerp(rawRMAOS.xyw, float3(ParallaxOccData.x, 0, ParallaxOccData.y), projectedMaterialWeight);
		float4 projectedGlintParameters = 0;
		if ((PBRFlags & PBR::Flags::ProjectedGlint) != 0) {
			projectedGlintParameters = SparkleParams;
		}
		glintParameters = lerp(glintParameters, projectedGlintParameters, projectedMaterialWeight);
#			else
		projBaseColor *= Color::VanillaDiffuseColorMult();
#			endif  // TRUE_PBR
#			if defined(LOD_BLENDING) && (defined(LODOBJECTS) || defined(LODOBJECTSHD))
		projBaseColor.xyz = pow(abs(projBaseColor.xyz), SharedData::lodBlendingSettings.LODObjectSnowGamma) * SharedData::lodBlendingSettings.LODObjectSnowBrightness;
#			endif  // LOD_BLENDING
		normal.xyz = lerp(normal.xyz, finalProjNormal, projectedMaterialWeight);
		baseColor.xyz = lerp(baseColor.xyz, projBaseColor, projectedMaterialWeight);

#			if defined(SNOW)
		useSnowDecalSpecular = true;
#			endif  // SNOW
	} else {
		if (projWeight > 0) {
			baseColor.xyz = Color::Diffuse(ProjectedUVParams2.xyz);
#			if defined(SNOW)
			useSnowDecalSpecular = true;
#			endif  // SNOW
		}
	}

#			if defined(SPECULAR)
	useSnowSpecular = useSnowDecalSpecular;
#			endif  // SPECULAR
#		endif      // SPARKLE

#	endif  // SNOW

#	if defined(WORLD_MAP)
	baseColor.xyz = GetWorldMapBaseColor(rawBaseColor.xyz, baseColor.xyz, projWeight);
#	endif  // WORLD_MAP

#	if defined(MODELSPACENORMALS)
	float3 vertexNormal = worldNormal;
#	endif

	float3 screenSpaceNormal = normalize(FrameBuffer::WorldToView(worldNormal, false));

#	if defined(HAIR) && defined(CS_HAIR)
	float3 Bitangent = normalize(float3(input.TBN0.y, input.TBN1.y, input.TBN2.y));
	float3 hairT = 0;
#		if defined(BACK_LIGHTING)
	hairT = useHairFlowMap ? normalize(mul(tbn, sampledHairFlow)) : Bitangent;
#		else
	hairT = Bitangent;
#		endif
	hairT = Hair::ReorientTangent(hairT, worldNormal);

	if (SharedData::hairSpecularSettings.Enabled) {
		if (SharedData::hairSpecularSettings.EnableTangentShift && SharedData::hairSpecularSettings.HairMode != 1) {
			float3 shiftedNormal = Hair::ShiftWorldNormal(hairT, worldNormal, 0, uv);
			screenSpaceNormal = normalize(FrameBuffer::WorldToView(shiftedNormal, false));
		}
	}
#	endif

	MaterialProperties material = (MaterialProperties)0;

	material.F0 = 0;
	material.Roughness = 1;

#	if defined(TRUE_PBR)
	material.Noise = screenNoise;

	material.Roughness = clamp(rawRMAOS.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
	material.Metallic = saturate(rawRMAOS.y);
	material.AO = rawRMAOS.z;

	// Apply vertex color to base color so PBR metals use it. On LANDSCAPE,
	// honor DisableTerrainVertexColors (as the non-PBR path does) by
	// neutralizing the source color so terrain vertex colors don't tint PBR;
	// a white source yields VertexAO == 1, i.e. no AO darkening either.
	float3 pbrVertexColorSrc = input.Color.xyz;
#		if defined(LANDSCAPE)
	if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
		pbrVertexColorSrc = 1;
#		endif
	float3 pbrVertexColor = Color::SrgbToLinear(pbrVertexColorSrc);
	float pbrVertexAO = max(max(pbrVertexColor.x, pbrVertexColor.y), pbrVertexColor.z);
	pbrVertexColor = pbrVertexAO == 0.0f ? 1.0f : pbrVertexColor * lerp(1 / max(pbrVertexAO, 0.001), 1, SharedData::truePBRSettings.VertexAOStrength);

	if (!SharedData::linearLightingSettings.enableLinearLighting) {
		baseColor.xyz = Color::SrgbToLinear(baseColor.xyz) * pbrVertexColor;
		material.F0 = lerp(rawRMAOS.w, baseColor.xyz, material.Metallic);
		baseColor.xyz = Color::LinearToSrgb(baseColor.xyz);
	} else {
		baseColor.xyz *= pbrVertexColor;
		material.F0 = lerp(rawRMAOS.w, baseColor.xyz, material.Metallic);
	}

	material.GlintScreenSpaceScale = max(1, glintParameters.x);
	material.GlintLogMicrofacetDensity = clamp(PBR::Constants::MaxGlintDensity - glintParameters.y, PBR::Constants::MinGlintDensity, PBR::Constants::MaxGlintDensity);
	material.GlintMicrofacetRoughness = clamp(glintParameters.z, PBR::Constants::MinGlintRoughness, PBR::Constants::MaxGlintRoughness);
	material.GlintDensityRandomization = clamp(glintParameters.w, PBR::Constants::MinGlintDensityRandomization, PBR::Constants::MaxGlintDensityRandomization);

#		if defined(GLINT)
	float glintNoise = Random::R1Modified(float(SharedData::FrameCount), (Random::pcg2d(uint2(input.Position.xy)) / 4294967296.0).x);
	Glints::PrecomputeGlints(glintNoise, uvOriginal, ddx(uvOriginal), ddy(uvOriginal), material.GlintScreenSpaceScale, material.GlintCache);
#		endif

	baseColor.xyz *= 1 - material.Metallic;

	material.BaseColor = baseColor.xyz;

	float3 coatWorldNormal = worldNormal;

#		if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::Subsurface) != 0)
	{
		material.SubsurfaceColor = PBRParams2.xyz;
		material.Thickness = PBRParams2.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
		{
			float4 sampledSubsurfaceProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);

			// If LL is off, Diffuse returns sRGB
			material.SubsurfaceColor *= Color::Diffuse(sampledSubsurfaceProperties.xyz);

			if (!SharedData::linearLightingSettings.enableLinearLighting) {
				material.SubsurfaceColor = Color::LinearToSrgb(
					Color::SrgbToLinear(material.SubsurfaceColor) * pbrVertexColor);
			} else {
				material.SubsurfaceColor *= pbrVertexColor;
			}

			material.Thickness *= sampledSubsurfaceProperties.w;
		}
		material.Thickness = lerp(material.Thickness, 1, projectedMaterialWeight);
	}
	else if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		material.CoatColor = sampledCoatColor.xyz;
		material.CoatStrength = sampledCoatColor.w;
		material.CoatRoughness = MultiLayerParallaxData.x;
		material.CoatF0 = MultiLayerParallaxData.y;

		float2 coatUv = uv;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			coatUv = uvOriginal;
		}
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledCoatProperties = TexBackLightSampler.Sample(SampBackLightSampler, coatUv);
			material.CoatRoughness *= sampledCoatProperties.w;
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				coatWorldNormal = normalize(mul(tbn, TransformNormal(sampledCoatProperties.xyz)));
			}
		}
		material.CoatStrength = lerp(material.CoatStrength, 0, projectedMaterialWeight);
	}

	[branch] if ((PBRFlags & PBR::Flags::Fuzz) != 0)
	{
		material.FuzzColor = MultiLayerParallaxData.xyz;
		material.FuzzWeight = MultiLayerParallaxData.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledFuzzProperties = TexBackLightSampler.Sample(SampBackLightSampler, uv);
			material.FuzzColor *= Color::Diffuse(sampledFuzzProperties.xyz);
			material.FuzzWeight *= sampledFuzzProperties.w;
		}
		material.FuzzWeight = lerp(material.FuzzWeight, 0, projectedMaterialWeight);
	}
#		endif
#	else
	material.BaseColor = baseColor.xyz;
#		if defined(SPECULAR)
	material.Shininess = shininess;
	material.Glossiness = glossiness;
	material.SpecularColor = SpecularColor.xyz;
#		else
	material.Shininess = 0;
	material.Glossiness = 0;
	material.SpecularColor = 0;
#		endif
#		if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING) || defined(LOAD_SOFT_LIGHTING))
	material.rimSoftLightColor = rimSoftLightColor.xyz;
#		endif
#		if defined(BACK_LIGHTING)
	material.backLightColor = backLightColor.xyz;
#		endif
#	endif  // TRUE_PBR

#	if defined(SKIN) && defined(CS_SKIN)
	const float ExtraRoughness = BRDF::F_Schlick(0.04, saturate(dot(worldNormal.xyz, viewDirection))) * SharedData::skinData.fuzzParams.w;
	material.Roughness = SharedData::skinData.skinParams.x;
	material.Roughness = saturate(SharedData::skinData.skinParams.x - SharedData::skinData.skinParams.z * material.Glossiness);
	material.RoughnessSecondary = SharedData::skinData.skinParams.y;
	if (skinRoughnessSet) {
		material.Roughness = skinRoughness * SharedData::skinData.physicalParams.x;
		material.RoughnessSecondary = skinRoughness * SharedData::skinData.physicalParams.y;
	}
	material.Roughness = min(1.0, material.Roughness + ExtraRoughness);
	material.RoughnessSecondary = min(1.0, material.RoughnessSecondary + ExtraRoughness);
	material.SecondarySpecIntensity = SharedData::skinData.skinParams2.x;
	material.Thickness = 1 - skinsk.x;
	material.SubsurfaceColor = skinsk.xyz;
	material.F0 = SharedData::skinData.skinParams2.zzz;
	material.AO = skinAO;
	material.Curvature = Skin::CalculateCurvature(worldNormal.xyz);

	material.FuzzWeight = SharedData::skinData.fuzzParams.x;
	material.FuzzRoughness = SharedData::skinData.fuzzParams.y;
	material.FuzzColor = SharedData::skinData.fuzzParams.zzz;

	if (skinRoughnessSet) {
		material.F0 = 0.08f * skinSpecular * SharedData::skinData.physicalParams.z;
		material.FuzzWeight *= skinFuzzMask;
	}
#	endif  // CS_SKIN

#	if defined(SKIN)
	material.BaseColor = max(material.BaseColor, EPSILON_SKIN_ALBEDO);
#	endif

#	if defined(CS_HAIR) && defined(HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		material.Shininess = SharedData::hairSpecularSettings.HairGlossiness;
		material.F0 = Hair::HairF0();
		if (SharedData::hairSpecularSettings.HairMode == 1) {
			material.Roughness = 1;
		} else {
			material.Roughness = ShininessToRoughness(material.Shininess * 0.75);
		}
	}
#	endif

	bool dynamicCubemap = false;

#	if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float envMask = EnvmapData.x * MaterialData.x;

	float viewNormalAngle = dot(worldNormal.xyz, viewDirection);
	float3 envSamplingPoint = (viewNormalAngle * 2) * worldNormal.xyz - viewDirection;

	if (envMask > 0.0) {
		if (EnvmapData.y) {
			envMask *= envMaskBase;
		} else {
			envMask *= glossiness;
		}
	}

	float3 envColor = 0.0;

	if (envMask > 0.0) {
#		if defined(DYNAMIC_CUBEMAPS)
		uint2 envSize;
		TexEnvSampler.GetDimensions(envSize.x, envSize.y);

#			if defined(EMAT)
		if (envSize.x == 1 && envSize.y == 1 || complexMaterial) {
#			else
		if (envSize.x == 1 && envSize.y == 1) {
#			endif

			dynamicCubemap = true;

#			if defined(EMAT)
			if (!complexMaterial)
#			endif
			{
				// Dynamic Cubemap Creator sets this value to black, if it is anything but black it is wrong
				float3 envColorTest = TexEnvSampler.SampleLevel(SampEnvSampler, float3(0.0, 1.0, 0.0), 15).xyz;
				dynamicCubemap = all(envColorTest == 0.0);
			}

#			if defined(CREATOR)
			if (SharedData::cubemapCreatorSettings.Enabled) {
				dynamicCubemap = true;
			}
#			endif

			if (dynamicCubemap) {
				float4 envColorBase = TexEnvSampler.SampleLevel(SampEnvSampler, float3(1.0, 0.0, 0.0), 15);

				if (envColorBase.a < 1.0) {
					material.F0 = Color::SkyrimGammaToLinear(envColorBase.rgb);
					material.Roughness = envColorBase.a;
				} else {
					material.F0 = 1.0;
					material.Roughness = 1.0 / 7.0;
				}

#			if defined(CREATOR)
				if (SharedData::cubemapCreatorSettings.Enabled) {
					material.F0 = SharedData::cubemapCreatorSettings.CubemapColor.rgb;
					material.Roughness = SharedData::cubemapCreatorSettings.CubemapColor.a;
				}
#			endif

#			if defined(EMAT)
				float complexMaterialRoughness = 1.0 - complexMaterialColor.y;
				material.Roughness = lerp(material.Roughness, complexMaterialRoughness, complexMaterial);
				material.F0 = lerp(material.F0, complexSpecular, complexMaterial);
#			endif
			}
		}
#		endif

		if (!dynamicCubemap) {
			float3 envColorBase = Color::SkyrimGammaToLinear(TexEnvSampler.Sample(SampEnvSampler, envSamplingPoint).xyz);
			envColor = envColorBase.xyz * envMask;
		}
	}

#	endif  // defined (ENVMAP) || defined (MULTI_LAYER_PARALLAX) || defined(EYE)

	float porosity = 1.0;

#	if defined(SKYLIGHTING)
	float3 positionMSSkylight = input.WorldPosition.xyz;
#		if defined(DEFERRED)
	sh2 skylightingSH = Skylighting::Sample(positionMSSkylight, worldNormal);
#		else
	sh2 skylightingSH = inWorld ? Skylighting::Sample(positionMSSkylight, worldNormal) : Skylighting::UNIT_SH;
#		endif

#	endif

	float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
	float waterHeight = waterData.w;

	float waterRoughnessSpecular = 1;

#	if defined(WETTERNESS)
	// Initialize wetness parameters.
	float rainWetness = 0.0;
	float puddleWetness = 0.0;
	float raindropWetness = 0.0;
	float rainContactVisible = 0.0;
	float wetSurfaceDarkeningMask = 0.0;
	float nonPuddleFilmMask = 1.0;
	float postRainBlend = 0.0;
	float postRainOverridePhase = 0.0;
	float postRainPuddleReflectionOverrideScale = 0.0;
	float3 wetnessNormal = worldNormal.xyz;
	float wetnessGlossinessAlbedo = 0.0;
	float wetnessGlossinessSpecular = 0.0;
	float wetHighlightReflectanceScale = 1.0;
	float wetDirectSpecularScale = 1.0;
	float wetRainCubemapSuppression = 0.0;
	float wetPuddleSkyReflectionScale = 1.0;
	float terrainWetIndirectNormalStability = 0.0;
	float rainDarkeningAbsorption = 1.0;
	float shoreDarkeningAbsorption = 1.0;
	const bool wetnessEnabled = CS_WETNESS_ENABLED;
	float postRainPuddleWaterStrength = max(0.0, CS_WETNESS_SETTINGS.PostRainPuddleWaterStrength);
	// Packed control lane layout:
	// - PackedPostRainControl[0..9]: post-rain spec boost (derived from Post-Rain Water Clarity)
	// - PackedPostRainControl[10..19]: puddle sky/cubemap reflection scale
	// - PackedPostRainControl[20..29]: faster wet-film rain phase
	// - PackedRainReflectionControl[0..9]: post-rain cubemap glare reduction (derived from Post-Rain Water Clarity)
	// - PackedRainReflectionControl[10..19]: in-rain cubemap suppression (derived from Rain Reflection Balance)
	// - PackedRainReflectionControl[20..29]: in-rain wet-film specular boost (derived from Rain Reflection Balance)
	// - PackedRainReflectionControl[30]: occlusion projection validity flag
	uint packedPostRainControl = CS_WETNESS_RUNTIME.PackedPostRainControl;
	float postRainSpecBoostFromClarity = saturate((float)(packedPostRainControl & 0x3FFu) * (1.0 / 1023.0));
	float puddleSkyReflectionScale = saturate((float)((packedPostRainControl >> 10) & 0x3FFu) * (1.0 / 1023.0));
	float wetFilmRainingAmount = saturate((float)((packedPostRainControl >> 20) & 0x3FFu) * (1.0 / 1023.0));
	uint packedRainReflectionControl = CS_WETNESS_RUNTIME.PackedRainReflectionControl;
	float postRainCubemapGlareReductionFromClarity = saturate((float)(packedRainReflectionControl & 0x3FFu) * (1.0 / 1023.0));
	float inRainCubemapSuppressionFromBalance = saturate((float)((packedRainReflectionControl >> 10) & 0x3FFu) * (1.0 / 1023.0));
	float inRainFilmSpecularBoostFromBalance = saturate((float)((packedRainReflectionControl >> 20) & 0x3FFu) * (1.0 / 1023.0));
	const bool hasOcclusionViewProj = (packedRainReflectionControl & (1u << 30)) != 0;
	float wetnessDistanceFadeRange = max(1.0, asfloat(CS_WETNESS_RUNTIME.WetnessDistanceFadeRangePacked));
	float wetnessDistanceFade = lerp(1.0, 0.0, saturate(viewPosition.z / wetnessDistanceFadeRange));
	float rainContactWetnessScale = max(0.0, CS_WETNESS_RUNTIME.RainContactWetnessScale);
	float shoreHeightDelta = input.WorldPosition.z - waterHeight;
	const bool wetSurfaceAllowed = shoreHeightDelta > 0.5;
	// Keep puddle spawning farther away from the waterline than generic wet film.
	// This prevents standing-water puddle patterns from appearing on water surfaces.
	const float puddleWaterlineExclusionHeight = 12.0;
	const bool puddleSurfaceAllowed = shoreHeightDelta > puddleWaterlineExclusionHeight;
	float wetnessDistToWater = abs(shoreHeightDelta);
	float shoreRangeSafe = max(1.0, (float)CS_WETNESS_SETTINGS.ShoreRange);
	// Keep shore wetness continuous at the waterline. A hard height gate here creates
	// a near-1 to 0 transition that shows up as a thin dark contour on shore geometry.
	float shoreFactor = saturate(1.0 - (wetnessDistToWater / shoreRangeSafe));
	float shoreFactorAlbedo = (input.WorldPosition.z < waterHeight) ? 1.0 : shoreFactor;
	float shoreWetnessAlbedo = saturate(shoreFactorAlbedo * CS_WETNESS_SETTINGS.MaxShoreWetness);
	float shoreWetnessDarkeningMask = shoreWetnessAlbedo * shoreWetnessAlbedo;

	[branch] if (wetnessEnabled) {
		// Calculate wetness angle and occlusion.
		float minWetnessValue = CS_WETNESS_SETTINGS.MinRainWetness;
		float minWetnessAngle = saturate(max(minWetnessValue, worldNormal.z));
#		if !defined(MODELSPACENORMALS)
		float rainSurfaceUpness = saturate(vertexNormal.z);
#		else
		float rainSurfaceUpness = saturate(worldNormal.z);
#		endif
		const float rainingAmount = saturate(CS_WETNESS_RUNTIME.Raining);
		const float inRainBlend = smoothstep(0.05, 0.35, rainingAmount);
		const float wetFilmInRainBlend = max(inRainBlend, smoothstep(0.05, 0.35, wetFilmRainingAmount));
		const float wetFilmPostRainBlend = 1.0 - smoothstep(0.02, 0.20, max(rainingAmount, wetFilmRainingAmount));

		// Surface classification used for both in-rain response and post-rain drying.
		float vegetationFactor = 0.0;
		float stoneFactor = 0.0;
		float dirtFactor = 1.0;
#		if !defined(SKIN) && !defined(HAIR)
		float3 surfaceBaseColor = saturate(material.BaseColor);
		float surfaceRoughness = material.Roughness;
#			if !defined(TRUE_PBR)
		surfaceRoughness = 1.0 - saturate(material.Glossiness);
#			endif
		vegetationFactor = smoothstep(0.06, 0.30, surfaceBaseColor.g - max(surfaceBaseColor.r, surfaceBaseColor.b));
		stoneFactor = saturate((0.62 - surfaceRoughness) * 2.4) * (1.0 - vegetationFactor);
		dirtFactor = saturate(1.0 - vegetationFactor - stoneFactor);
		float surfaceBrightness = dot(surfaceBaseColor, float3(0.299, 0.587, 0.114));
		float surfaceMaxChannel = max(surfaceBaseColor.r, max(surfaceBaseColor.g, surfaceBaseColor.b));
		float surfaceMinChannel = min(surfaceBaseColor.r, min(surfaceBaseColor.g, surfaceBaseColor.b));
		float surfaceChroma = surfaceMaxChannel - surfaceMinChannel;
		float surfaceWarmth = saturate(surfaceBaseColor.r - surfaceBaseColor.b + 0.20 * surfaceBaseColor.g);
		float grassDarkeningFactor = vegetationFactor;
		float remainingDarkeningSurface = saturate(1.0 - grassDarkeningFactor);
		float sandDarkeningFactor =
			smoothstep(0.42, 0.78, surfaceBrightness) *
			smoothstep(0.02, 0.18, surfaceWarmth) *
			(1.0 - smoothstep(0.14, 0.32, surfaceChroma)) *
			saturate(surfaceRoughness * 1.15) *
			remainingDarkeningSurface;
		remainingDarkeningSurface = saturate(remainingDarkeningSurface - sandDarkeningFactor);
		float woodDarkeningFactor =
			smoothstep(0.06, 0.24, surfaceWarmth) *
			smoothstep(0.08, 0.28, surfaceChroma) *
			(1.0 - smoothstep(0.58, 0.86, surfaceBrightness)) *
			saturate(1.0 - surfaceBaseColor.g * 0.75) *
			remainingDarkeningSurface;
		remainingDarkeningSurface = saturate(remainingDarkeningSurface - woodDarkeningFactor);
		float stoneDarkeningFactor = saturate(stoneFactor) * remainingDarkeningSurface;
		remainingDarkeningSurface = saturate(remainingDarkeningSurface - stoneDarkeningFactor);
		float dirtDarkeningFactor = remainingDarkeningSurface;
		rainDarkeningAbsorption = saturate(
			stoneDarkeningFactor * 0.45 +
			woodDarkeningFactor * 0.62 +
			grassDarkeningFactor * 0.78 +
			sandDarkeningFactor * 0.90 +
			dirtDarkeningFactor);
		shoreDarkeningAbsorption = rainDarkeningAbsorption;
#		endif
#		if defined(SKYLIGHTING)
		float wetnessOcclusion = 0.0;
		float openSkyVisibility = 0.0;
		if (inWorld) {
			float wetnessOcclusionBase = saturate(SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1)));
			openSkyVisibility = wetnessOcclusionBase;
			float wetnessOcclusionStable = max(wetnessOcclusionBase, 0.90);
			wetnessOcclusion = lerp(1.0, wetnessOcclusionStable, 0.12);
		}
#		else
		float wetnessOcclusion = inWorld;
		float openSkyVisibility = inWorld;
#		endif
		wetnessOcclusion = lerp(wetnessOcclusion, max(wetnessOcclusion, 0.82), inRainBlend);

		postRainBlend = 1.0 - smoothstep(0.02, 0.20, rainingAmount);
		float surfaceDryingPower = 1.0;
		float stoneDryingHours = clamp(CS_WETNESS_SETTINGS.StoneDryingMultiplier, 1.0, 24.0);
		float dirtDryingHours = clamp(CS_WETNESS_SETTINGS.DirtDryingMultiplier, 1.0, 24.0);
		float grassDryingHours = clamp(CS_WETNESS_SETTINGS.GrassDryingMultiplier, 1.0, 24.0);
		float stoneDryingPower = 24.0 / stoneDryingHours;
		float dirtDryingPower = 24.0 / dirtDryingHours;
		float grassDryingPower = 24.0 / grassDryingHours;
		float dryingOverrideAmount = abs(stoneDryingPower - 1.0) +
			abs(dirtDryingPower - 1.0) +
			abs(grassDryingPower - 1.0);
		[branch] if (postRainBlend > 0.0 && dryingOverrideAmount > 1e-3) {
			float surfaceDryingMultiplier =
				stoneFactor * stoneDryingPower +
				dirtFactor * dirtDryingPower +
				vegetationFactor * grassDryingPower;

#		if defined(SKIN) || defined(HAIR)
			surfaceDryingMultiplier = dirtDryingPower;
#		endif

			surfaceDryingMultiplier = max(surfaceDryingMultiplier, 0.05);
			surfaceDryingPower = lerp(1.0, surfaceDryingMultiplier, postRainBlend);
		}

		float4 raindropInfo = float4(0, 0, 1, 0);
		float raindropRange = max(1.0, CS_WETNESS_SETTINGS.RaindropFxRange);
		float maxRainDropDistance = raindropRange * raindropRange * 3.0;
		float rainDropDistance = dot(viewPosition, viewPosition);
		float distanceFadeout = saturate((1.0 - saturate(rainDropDistance / maxRainDropDistance)) * 3.0);
		float localRainOcclusion = 1.0;
		const float puddleOcclusionPhase = saturate(max(inRainBlend, postRainBlend));
		float roofLikeMask = smoothstep(0.35, 0.92, 1.0 - saturate(openSkyVisibility));
		const bool raindropFxActive =
			rainSurfaceUpness > 0.05 &&
			CS_WETNESS_RUNTIME.Raining > 0.0f &&
			(CS_WETNESS_SETTINGS.EnableRaindropFx != 0) &&
			(rainDropDistance < maxRainDropDistance);
		const bool raindropOcclusionNeeded = raindropFxActive && inRainBlend > 0.0;
		const bool shelterOcclusionNeeded = puddleOcclusionPhase * roofLikeMask > 1e-3;
		if ((raindropOcclusionNeeded || shelterOcclusionNeeded) && hasOcclusionViewProj) {
			float4 precipOcclusionTexCoord = mul(CS_WETNESS_RUNTIME.OcclusionViewProj, float4(input.WorldPosition.xyz, 1));
			precipOcclusionTexCoord.y = -precipOcclusionTexCoord.y;
			float2 precipOcclusionUV = precipOcclusionTexCoord.xy * 0.5 + 0.5;
			float2 clampedPrecipOcclusionUV = saturate(precipOcclusionUV);
			float2 occlusionOutsideDelta = abs(precipOcclusionUV - clampedPrecipOcclusionUV);
			float occlusionDomainBlend = saturate(1.0 - max(occlusionOutsideDelta.x, occlusionOutsideDelta.y) * 8.0);
			float precipOcclusionZ = CS_TEX_PRECIP_OCCLUSION.SampleLevel(SampColorSampler, clampedPrecipOcclusionUV, 0).x;
			float2 occlusionUvMargin = min(clampedPrecipOcclusionUV, 1.0 - clampedPrecipOcclusionUV);
			float occlusionEdgeFade = saturate(min(occlusionUvMargin.x, occlusionUvMargin.y) * 16.0);
			float occlusionValidity = occlusionDomainBlend * occlusionEdgeFade;
			float occlusionPass = (precipOcclusionTexCoord.z < precipOcclusionZ + 0.05) ? 1.0 : 0.0;
			localRainOcclusion = lerp(1.0, occlusionPass, occlusionValidity);
		}
		float puddleOcclusionSoft = lerp(0.72, 1.0, localRainOcclusion);
		float puddleOcclusion = lerp(puddleOcclusionSoft, localRainOcclusion, roofLikeMask);
		float puddleOcclusionInfluence = roofLikeMask * roofLikeMask;
		float puddleRainExposure = lerp(1.0, puddleOcclusion, puddleOcclusionPhase * puddleOcclusionInfluence);
		[branch] if (inRainBlend > 0.0) {
			float openSkyUpness = smoothstep(0.55, 0.96, rainSurfaceUpness);
			float openSkyAmbient = smoothstep(0.35, 0.75, saturate(openSkyVisibility));
			float openSkyMask = openSkyUpness * openSkyAmbient;
			puddleRainExposure = lerp(puddleRainExposure, 1.0, openSkyMask);
		}
		if (raindropFxActive) {
			float raindropFade = localRainOcclusion * distanceFadeout;
			if (raindropFade > 0.0) {
#		if defined(SKINNED)
				raindropInfo = CS_GET_RAIN_DROPS(input.ModelPosition.xyz, CS_WETNESS_RUNTIME.Time, worldNormal.xyz, raindropFade);
#		elif defined(DEFERRED)
				raindropInfo = CS_GET_RAIN_DROPS(input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz, CS_WETNESS_RUNTIME.Time, worldNormal.xyz, raindropFade);
#		else
				raindropInfo = CS_GET_RAIN_DROPS(!FrameBuffer::FrameParams.y ? input.ModelPosition.xyz : input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz, CS_WETNESS_RUNTIME.Time, worldNormal.xyz, raindropFade);
#		endif
			}
			raindropInfo.w *= raindropFade;
		}
		raindropWetness = raindropInfo.w;

		rainWetness = CS_WETNESS_RUNTIME.Wetness * minWetnessAngle * CS_WETNESS_SETTINGS.MaxRainWetness;
		puddleWetness = CS_WETNESS_RUNTIME.PuddleWetness * minWetnessAngle;

		const float stoneRainStage = smoothstep(0.02, 0.12, rainingAmount);
		const float dirtRainStage = smoothstep(0.10, 0.30, rainingAmount);
		const float grassRainStage = smoothstep(0.22, 0.50, rainingAmount);
		float surfaceRainCoverage = stoneFactor * stoneRainStage + dirtFactor * dirtRainStage + vegetationFactor * grassRainStage;
		surfaceRainCoverage = max(surfaceRainCoverage, 0.30 * inRainBlend);
		float inRainWetnessScale = lerp(0.90, 1.20, surfaceRainCoverage);
		rainWetness *= lerp(1.0, inRainWetnessScale, inRainBlend);
		puddleWetness *= lerp(1.0, inRainWetnessScale * 1.10, inRainBlend);
		puddleWetness *= puddleRainExposure;

#		if defined(SKIN) || defined(HAIR)
		float characterWetnessScale = 1.0f;
#			if defined(HAIR)
		characterWetnessScale = 0.8f;
#			endif
		rainWetness = CS_WETNESS_SETTINGS.SkinWetness * CS_WETNESS_RUNTIME.Wetness * characterWetnessScale;
		float characterWetnessSpecular = saturate(rainWetness);
#		endif

		float wetnessOcclusionMix = lerp(1.0, wetnessOcclusion, 0.35);
		rainWetness *= wetnessOcclusionMix;
		puddleWetness *= wetnessOcclusionMix;

		rainWetness = pow(saturate(rainWetness), surfaceDryingPower);
		puddleWetness = saturate(puddleWetness);
		float postRainDryCut = lerp(0.0, 0.08, postRainBlend);
		rainWetness = saturate((rainWetness - postRainDryCut) / max(1e-3, 1.0 - postRainDryCut));
		float rainFilmWetness = saturate(max(rainWetness, raindropWetness * inRainBlend));
		if (!wetSurfaceAllowed) {
			rainWetness = 0.0;
			rainFilmWetness = 0.0;
			puddleWetness = 0.0;
#		if defined(SKIN) || defined(HAIR)
			characterWetnessSpecular = 0.0;
#		endif
		}
		if (!puddleSurfaceAllowed) {
			puddleWetness = 0.0;
		}

		float shoreWetness = shoreFactor * CS_WETNESS_SETTINGS.MaxShoreWetness;
		float rainContactFilmScale = sqrt(max(0.0, rainContactWetnessScale)) * 1.35;
		float rainContactMask = saturate(rainFilmWetness * wetnessDistanceFade);
		float rainContactSlopeMask = smoothstep(0.32, 0.95, rainSurfaceUpness);
		float rainContactBase = saturate(rainContactMask * rainContactSlopeMask);
		rainContactVisible = saturate(rainContactBase * rainContactFilmScale);

		const bool puddleAllowed = puddleSurfaceAllowed;
		float puddle = 0.0;
		float puddleFootprintMask = 0.0;
		const float gameUnitsPerMeter = (1.0 / 0.01428);
		const float puddleRadiusMin = 0.15 * gameUnitsPerMeter;
		const float puddleRadiusMax = 50.0 * gameUnitsPerMeter;
#		if !defined(SKINNED)
		const bool puddleRuntimeActive = puddleAllowed && puddleWetness > 0.0 && wetnessDistanceFade > 0.0;
		if (puddleRuntimeActive) {
			float puddleMaxAngleSafe = max(CS_WETNESS_SETTINGS.PuddleMaxAngle, 1e-3);
			float puddleSlopeStart = max(0.0, puddleMaxAngleSafe * 0.45);
			float puddleSlopeEnd = min(1.0, max(puddleSlopeStart + 1e-3, puddleMaxAngleSafe));
			float puddleSlopeMask = smoothstep(puddleSlopeStart, puddleSlopeEnd, saturate(worldNormal.z));
			if (puddleSlopeMask > 0.0) {
				float puddleRadiusSafe = max(CS_WETNESS_SETTINGS.PuddleRadius, 1e-3);
				float puddleRadiusT = saturate((puddleRadiusSafe - puddleRadiusMin) / max(1e-3, puddleRadiusMax - puddleRadiusMin));
				float puddleFootprintT = sqrt(puddleRadiusT);
				float puddleFootprintThreshold = lerp(0.72, 0.20, puddleFootprintT);
				float puddleFootprintSoftness = lerp(0.08, 0.22, puddleFootprintT);

				float puddleLayoutSafe = clamp(CS_WETNESS_SETTINGS.PuddleLayout, 0.3, 10.0);
				float3 puddleCoordsBase = ((input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz) * 0.5 + 0.5) * 0.01;
				float layoutT = saturate((puddleLayoutSafe - 0.3) / 9.7);
				float layoutFrequency = lerp(0.75, 2.10, layoutT);
				float3 layoutSeed = float3(12.7, 19.1, 23.3) * puddleLayoutSafe;
				float layoutWarpStrength = lerp(0.0, 0.38, layoutT);
				float layoutWarp = 0.0;
				if (layoutWarpStrength > 0.0) {
					layoutWarp = Random::perlinNoise(puddleCoordsBase * lerp(0.22, 0.72, layoutT) + layoutSeed) * 2.0 - 1.0;
				}
				float3 puddlePatternOffset = float3(31.0, 17.0, 43.0) * layoutT;
				float3 puddleCoords = puddleCoordsBase * layoutFrequency + layoutWarp * float3(0.20, 0.14, 0.18) * layoutWarpStrength + puddlePatternOffset;
				float puddleNoiseSignal = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
				float puddleSignal = puddleNoiseSignal;
				float puddleRadiusGate = smoothstep(
					puddleFootprintThreshold - puddleFootprintSoftness,
					puddleFootprintThreshold + puddleFootprintSoftness,
					puddleNoiseSignal);
				puddleSignal = puddleSignal * ((minWetnessAngle / puddleMaxAngleSafe) * CS_WETNESS_SETTINGS.MaxPuddleWetness * 0.25) + 0.5;
				float puddleBlend = puddleWetness;
				puddleFootprintMask = puddleRadiusGate * puddleSlopeMask;
				puddle = puddleSignal * puddleBlend * puddleSlopeMask * puddleRadiusGate;
			}
		}
#		endif
		puddle *= puddleRainExposure;
		puddle *= saturate(wetnessOcclusion * 2.0);
		puddle *= wetnessDistanceFade;
		float puddleDepthSignal = saturate(puddle);
		float puddleSpecularBase = puddleDepthSignal;

		float puddleSpecularBaseSaturated = saturate(puddleSpecularBase);
		float wetPuddleSpecular = puddleSpecularBaseSaturated;
		wetPuddleSpecular = lerp(wetPuddleSpecular, wetPuddleSpecular * shoreFactor, input.WorldPosition.z < waterHeight);
		float wetFilmSource = rainContactVisible;
		float wetFilmFloorScale = max(0.0, CS_WETNESS_SETTINGS.WetFilmSpecularFloorScale);
		float wetFilmFloorResponse = lerp(0.50, 1.45, saturate(wetFilmFloorScale * (1.0 / 3.0)));
		float wetFilmRainExposure = lerp(1.0, puddleRainExposure, wetFilmInRainBlend * 0.10 * rainContactSlopeMask);
		wetFilmRainExposure = max(wetFilmRainExposure, lerp(1.0, 0.84, wetFilmInRainBlend));
		float wetFilmSpecular = saturate(wetFilmSource * lerp(0.42, 0.78, wetFilmInRainBlend) * wetFilmRainExposure * wetFilmFloorResponse);
		wetFilmSpecular *= rainContactSlopeMask;
		wetFilmSpecular *= lerp(1.0, 1.25, inRainFilmSpecularBoostFromBalance * wetFilmInRainBlend);
		wetFilmSpecular *= lerp(1.0, 0.28, wetFilmPostRainBlend);
		float shoreWetnessSpecular = saturate(shoreWetness);
		float puddleWaterThreshold = lerp(CS_WETNESS_SETTINGS.PuddleMinWetness, CS_WETNESS_SETTINGS.PuddleMinWetness * 0.8, inRainBlend);
		float puddleWaterStart = min(puddleWaterThreshold, 0.98);
		float puddleWaterFull = lerp(0.78, 0.88, inRainBlend);
		puddleWaterFull = min(max(puddleWaterFull, puddleWaterStart + 0.08), 1.0);
		float deepPuddleMask = smoothstep(puddleWaterStart, puddleWaterFull, puddleSpecularBaseSaturated);
		wetSurfaceDarkeningMask = saturate(max(max(rainContactBase, puddleSpecularBaseSaturated), shoreWetnessDarkeningMask));
		float puddleCoverageMask = saturate(puddleFootprintMask * smoothstep(0.55, 0.85, puddleSpecularBaseSaturated));
		nonPuddleFilmMask = 1.0 - puddleCoverageMask;
		wetPuddleSkyReflectionScale = lerp(1.0, puddleSkyReflectionScale, puddleCoverageMask);
		float wetFilmSpecularNonPuddle = wetFilmSpecular * nonPuddleFilmMask;
		float shoreWetnessSpecularNonPuddle = shoreWetnessSpecular * nonPuddleFilmMask;
		wetnessGlossinessAlbedo = max(puddle, shoreWetnessAlbedo);
		wetnessGlossinessAlbedo *= wetnessGlossinessAlbedo;
		float rainPuddlePhase = saturate(inRainBlend * deepPuddleMask);
		float postRainOverridePhaseCandidate = saturate(postRainBlend * smoothstep(0.36, 0.88, puddleDepthSignal));
		const bool postRainVisible =
			postRainBlend > 1e-4 &&
			max(max(postRainOverridePhaseCandidate, wetFilmSpecular), puddleSpecularBaseSaturated) > 1e-4;
		float postRainDirectScale = 1.0;
		float postRainGlossScale = 1.0;
		float postRainSpecBoostCurve = 0.0;
		float postRainMirrorMix = 1.0;
		if (postRainVisible) {
			float postRainShineControl = clamp(postRainPuddleWaterStrength, 0.0, 5.0);
			const float postRainShineNeutral = 2.5;
			float postRainShineReduceT = saturate(postRainShineControl / postRainShineNeutral);
			float postRainShineBoostT = saturate((postRainShineControl - postRainShineNeutral) / max(1e-3, 5.0 - postRainShineNeutral));
			float postRainShineCubemapScale = 1.0;
			postRainDirectScale = 0.82;
			postRainGlossScale = 0.95;
			if (postRainShineControl <= postRainShineNeutral) {
				postRainShineCubemapScale = lerp(0.06, 1.0, postRainShineReduceT);
				postRainDirectScale = lerp(0.06, 0.82, postRainShineReduceT);
				postRainGlossScale = lerp(0.60, 0.95, postRainShineReduceT);
			} else {
				postRainShineCubemapScale = lerp(1.0, 3.20, postRainShineBoostT);
				postRainDirectScale = lerp(0.82, 1.15, postRainShineBoostT);
				postRainGlossScale = lerp(0.95, 1.28, postRainShineBoostT);
			}

			float postRainSpecularPower = lerp(1.0, 0.75, saturate(postRainBlend * postRainShineBoostT));
			wetPuddleSpecular = lerp(puddleSpecularBaseSaturated, saturate(pow(puddleSpecularBaseSaturated, postRainSpecularPower)), postRainBlend);
			wetPuddleSpecular = lerp(wetPuddleSpecular, wetPuddleSpecular * shoreFactor, input.WorldPosition.z < waterHeight);

			postRainOverridePhase = postRainOverridePhaseCandidate;
			postRainSpecBoostCurve = smoothstep(0.0, 1.0, postRainSpecBoostFromClarity);
			postRainMirrorMix = 1.0 - postRainSpecBoostCurve;
			float postRainClarityCubemapScale = lerp(1.15, 0.65, postRainSpecBoostCurve);
			postRainPuddleReflectionOverrideScale = clamp(postRainClarityCubemapScale * postRainShineCubemapScale, 0.25, 1.6);
		}

		wetnessGlossinessSpecular = max(wetPuddleSpecular, max(wetFilmSpecularNonPuddle, shoreWetnessSpecularNonPuddle));
		float wetFilmNonPuddleSpecular = max(wetFilmSpecularNonPuddle, shoreWetnessSpecularNonPuddle);
#		if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)
		if (useTerrainVariation) {
			terrainWetIndirectNormalStability = 0.18 * smoothstep(0.03, 0.22, wetFilmNonPuddleSpecular) * (1.0 - deepPuddleMask);
		}
#		endif
		float wetFilmDominance = saturate(wetFilmNonPuddleSpecular / max(wetnessGlossinessSpecular, 1e-3));
		float wetFilmDirectScale = lerp(0.55, 0.78, inRainFilmSpecularBoostFromBalance);
		wetDirectSpecularScale = lerp(1.0, wetFilmDirectScale, wetFilmDominance * saturate(wetFilmFloorScale * 0.85));
		wetDirectSpecularScale *= lerp(1.0, 0.24, rainPuddlePhase);
		wetnessGlossinessSpecular *= lerp(1.0, 0.88, rainPuddlePhase);
		[branch] if (postRainVisible) {
			wetDirectSpecularScale *= lerp(1.0, postRainDirectScale, postRainOverridePhase);
			wetnessGlossinessSpecular *= lerp(1.0, postRainGlossScale, postRainOverridePhase);
			float postRainDirectMirrorScale = lerp(1.0, 0.82, postRainMirrorMix);
			wetDirectSpecularScale *= lerp(1.0, postRainDirectMirrorScale, postRainOverridePhase * 0.90);
			float postRainSpecBoostScale = lerp(1.0, 2.45, postRainSpecBoostCurve);
			wetnessGlossinessSpecular *= lerp(1.0, postRainSpecBoostScale, postRainOverridePhase);
			float postRainDirectSpecCompensation = lerp(1.0, 0.82, postRainSpecBoostCurve);
			wetDirectSpecularScale *= lerp(1.0, postRainDirectSpecCompensation, postRainOverridePhase * 0.75);
		}
		[branch] if (postRainVisible) {
			postRainPuddleReflectionOverrideScale = clamp(postRainPuddleReflectionOverrideScale, 0.25, 1.9);
		}
		wetnessGlossinessSpecular = saturate(wetnessGlossinessSpecular * lerp(1.0, 1.90, deepPuddleMask));

		float flatnessAmount = smoothstep(max(CS_WETNESS_SETTINGS.PuddleMaxAngle, 0.0), 1.0, minWetnessAngle);
		float puddleMinWetness = puddleWaterThreshold;
		float puddleFlatness = smoothstep(puddleMinWetness, 1.0, wetPuddleSpecular);
		float rainFilmFlatness = smoothstep(0.04, 0.22, wetFilmSpecularNonPuddle) * wetFilmInRainBlend * 0.16;
		float shoreFilmFlatness = shoreWetnessSpecularNonPuddle * 0.10;
		flatnessAmount *= max(puddleFlatness, max(rainFilmFlatness, shoreFilmFlatness));
		flatnessAmount = saturate(lerp(flatnessAmount, 1.0, deepPuddleMask * 0.92));

		wetnessNormal = normalize(lerp(wetnessNormal, float3(0, 0, 1), flatnessAmount));

		if (any(raindropInfo.xyz != float3(0, 0, 1))) {
			float3 rippleNormal = normalize(lerp(float3(0, 0, 1), raindropInfo.xyz, lerp(1.0, flatnessAmount, 0.8)));
			wetnessNormal = CS_REORIENT_NORMAL(rippleNormal, wetnessNormal);
		}

		float wetnessViewNdotV = saturate(abs(dot(wetnessNormal, viewDirection)));
		float wetHighlightMask = smoothstep(0.03, 0.18, wetFilmNonPuddleSpecular);
		float wetHighlightReduction = max(0.25, CS_WETNESS_SETTINGS.WetHighlightReduction);
		float highlightReductionT = saturate((wetHighlightReduction - 1.0) / 9.0);
		float highlightReductionCurve = (highlightReductionT * highlightReductionT) * wetHighlightMask;
		float highlightRelaxT = saturate((1.0 - wetHighlightReduction) / 0.75) * wetHighlightMask;
		float grazingMask = 1.0 - smoothstep(0.10, 0.60, wetnessViewNdotV);
		float grazingMinAttenuation = 0.45;
		grazingMinAttenuation = lerp(grazingMinAttenuation, 0.01, highlightReductionCurve);
		grazingMinAttenuation = lerp(grazingMinAttenuation, 0.80, highlightRelaxT);
		float wetnessGrazingAttenuation = saturate(lerp(1.0, grazingMinAttenuation, grazingMask));
		wetnessGrazingAttenuation = lerp(1.0, wetnessGrazingAttenuation, wetHighlightMask);
		wetnessGlossinessSpecular *= wetnessGrazingAttenuation;
		wetnessGlossinessSpecular = max(wetnessGlossinessSpecular, shoreWetnessSpecularNonPuddle);
#		if defined(SKIN) || defined(HAIR)
		wetnessGlossinessSpecular = max(wetnessGlossinessSpecular, characterWetnessSpecular);
#		endif
		wetHighlightReflectanceScale = lerp(1.0, wetnessGrazingAttenuation, saturate(0.30 * wetHighlightMask + highlightReductionCurve));
		float wetHighlightViewDistance = abs(viewPosition.z);
		float farWhiteDistanceMask = smoothstep(2048.0, 8192.0, wetHighlightViewDistance);
		float farGrazingWhiteMask = saturate(grazingMask * farWhiteDistanceMask);
		float farWhiteSuppression = farGrazingWhiteMask * highlightReductionCurve;
		wetDirectSpecularScale *= lerp(1.0, 0.18, farWhiteSuppression);
		wetHighlightReflectanceScale *= lerp(1.0, 0.65, farWhiteSuppression);
		float rainWetSurfaceMask = smoothstep(0.03, 0.20, max(rainContactVisible * nonPuddleFilmMask, shoreWetnessSpecularNonPuddle));
		float rainReflectionBalanceCurve = inRainCubemapSuppressionFromBalance * inRainCubemapSuppressionFromBalance;
		float rainCubemapSuppressionStrength = lerp(0.03, 0.32, rainReflectionBalanceCurve);
		wetRainCubemapSuppression = saturate(wetFilmInRainBlend * rainWetSurfaceMask * rainCubemapSuppressionStrength);
		wetHighlightReflectanceScale *= (1.0 - wetRainCubemapSuppression);

		float wetRoughnessScale = lerp(0.97, 0.995, postRainOverridePhase * postRainSpecBoostCurve);
		waterRoughnessSpecular = 1.0 - wetnessGlossinessSpecular * wetRoughnessScale;
	}
	const bool wetSpecularEnabled = wetnessEnabled;
#	elif defined(WETNESS_EFFECTS)
	// Initialize wetness parameters.
	float wetness = 0.0;
	float3 wetnessNormal = vertexNormal.xyz;

	// Calculate shore wetness factors
	float wetnessDistToWater = abs(input.WorldPosition.z - waterHeight);
	float shoreRangeSafe = max(1.0, (float)CS_WETNESS_SETTINGS.ShoreRange);
	float shoreFactor = saturate(1.0 - (wetnessDistToWater / shoreRangeSafe));
	float shoreFactorAlbedo = (input.WorldPosition.z < waterHeight) ? 1.0 : shoreFactor;

	// Calculate wetness angle and occlusion
	float minWetnessValue = CS_WETNESS_SETTINGS.MinRainWetness;
	float minWetnessAngle = saturate(max(minWetnessValue, vertexNormal.z));
#		if defined(SKYLIGHTING)
	float wetnessOcclusion = inWorld ? saturate(SphericalHarmonics::Unproject(skylightingSH, float3(0, 0, 1))) : 0.0;
#		else
	float wetnessOcclusion = inWorld;
#		endif
	float flatnessAmount = smoothstep(CS_WETNESS_SETTINGS.PuddleMaxAngle, 1.0, minWetnessAngle);
	// Calculate raindrop effects
	float4 raindropInfo = float4(0, 0, 1, 0);
	bool shouldCalculateRaindrops = (worldNormal.z > 0.0) &&
	                                (CS_WETNESS_RUNTIME.Raining > 0.0) &&
	                                (CS_WETNESS_SETTINGS.EnableRaindropFx) &&
	                                (wetnessOcclusion > 0.5);

	if (shouldCalculateRaindrops) {
#		if defined(SKINNED)
		float3 ripplePosition = input.ModelPosition.xyz;
#		elif defined(DEFERRED)
		float3 ripplePosition = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
#		else
		float3 ripplePosition = !FrameBuffer::FrameParams.y ? input.ModelPosition.xyz : input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
#		endif
		raindropInfo = CS_GET_RAIN_DROPS(ripplePosition, CS_WETNESS_RUNTIME.Time, wetnessNormal, flatnessAmount);
	}

	// Calculate different wetness types
	float rainWetness = CS_WETNESS_RUNTIME.Wetness * minWetnessAngle * CS_WETNESS_SETTINGS.MaxRainWetness;
	rainWetness = max(rainWetness, raindropInfo.w);

#		if defined(SKIN) || defined(HAIR)
	rainWetness = CS_WETNESS_SETTINGS.SkinWetness * CS_WETNESS_RUNTIME.Wetness;
#		endif

#		if defined(CS_SKIN) && !defined(SKIN)
	if (skinEnabled) {
		float2 dynamicWetness = Skin::GetWetness(input.WorldPosition.z + FrameBuffer::CameraPosAdjust.z, worldNormal.xyz);
#			if defined(TRUE_PBR)
		dynamicWetness.x = lerp(dynamicWetness.x, 0.0f, material.Metallic);
#			endif
		float dynamicWetnessValue = clamp(dynamicWetness.x + dynamicWetness.y, 0.f, 2.f);
#			if defined(HAIR)
		dynamicWetnessValue = min(SharedData::skinData.skinParams2.y + dynamicWetnessValue, 2.0f);
#			endif
		rainWetness += min(dynamicWetnessValue, 1.f);
	}
#		endif
	float shoreWetness = shoreFactor * CS_WETNESS_SETTINGS.MaxShoreWetness;
	wetness = max(shoreWetness, rainWetness);

	// Calculate puddle effects
	float puddleWetness = CS_WETNESS_RUNTIME.PuddleWetness * minWetnessAngle;
	float puddle = wetness;

#		if !defined(SKINNED) && !(defined(SKIN) && defined(CS_SKIN))
	if (wetness > 0.0 || puddleWetness > 0.0) {
		float puddleRadiusSafe = max(CS_WETNESS_SETTINGS.PuddleRadius, 1e-3);
		float puddleMaxAngleSafe = max(CS_WETNESS_SETTINGS.PuddleMaxAngle, 1e-3);
		float3 puddleCoords = ((input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz) * 0.5 + 0.5) * 0.01 / puddleRadiusSafe;
		puddle = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
		puddle = puddle * ((minWetnessAngle / puddleMaxAngleSafe) * CS_WETNESS_SETTINGS.MaxPuddleWetness * 0.25) + 0.5;
		puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
	}
#		endif

	// Apply occlusion and distance factors
	puddle *= saturate(wetnessOcclusion * 2.0) * nearFactor;
	wetnessNormal = lerp(worldNormal.xyz, wetnessNormal, saturate(puddle));

	// Calculate wetness glossiness factors
	float wetnessGlossinessAlbedo = max(puddle, shoreFactorAlbedo * CS_WETNESS_SETTINGS.MaxShoreWetness);
	wetnessGlossinessAlbedo *= wetnessGlossinessAlbedo;

	float wetnessGlossinessSpecular = puddle;
	if (input.WorldPosition.z < waterHeight) {
		wetnessGlossinessSpecular *= shoreFactor;
	}

	// Update flatness and normal calculations
	flatnessAmount *= smoothstep(CS_WETNESS_SETTINGS.PuddleMinWetness, 1.0, wetnessGlossinessSpecular);

	// Apply ripple normal effects
	float3 rippleNormal = normalize(lerp(float3(0, 0, 1), raindropInfo.xyz, lerp(flatnessAmount, 1.0, 0.5)));
	wetnessNormal = CS_REORIENT_NORMAL(rippleNormal, wetnessNormal);

#		if defined(SKIN) && defined(CS_SKIN)
	if (skinEnabled && (skinWetness > 0.0f)) {
		wetnessNormal = skinWetNormal;
		wetnessGlossinessSpecular = saturate(max(wetnessGlossinessSpecular, skinWetness));
	}
#		endif

	// Minimum roughness prevents an extreme retroreflective peak (NdotH→1) for near-zero
	// roughness puddles. Real water has ripples and surface tension that keep it from being
	// optically perfect; the ripple normal map adds micro-variation but GGX still peaks
	// sharply without this floor.
	static const float wetnessMinPuddleRoughness = 0.05;
	waterRoughnessSpecular = max(saturate(1.0 - wetnessGlossinessSpecular), wetnessMinPuddleRoughness);
#	endif

	float llDirLightMult = SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear && (inWorld || inReflection) && !SharedData::InInterior ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;

#	if defined(EXP_HEIGHT_FOG)
	if (SharedData::exponentialHeightFogSettings.enabled) {
		dirLightColor *= ExponentialHeightFog::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#	endif

#	if defined(WATER_EFFECTS)
	dirLightColor *= WaterEffects::ComputeCaustics(waterData, input.WorldPosition.xyz);
#	endif

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (inWorld || inReflection)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirLightAngle = dot(worldNormal.xyz, DirLightDirection.xyz);

	float3 refractedDirLightDirection = DirLightDirection;
#	if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		if (dot(DirLightDirection, coatWorldNormal) > 0)
			refractedDirLightDirection = -refract(-DirLightDirection, coatWorldNormal, eta);
	}
#	endif

	float dirSoftShadow = 1.0;
	float dirVSMDetailedShadow = 1.0;

#	if defined(VOLUMETRIC_SHADOWS)
	if (inWorld && !inReflection && ShadowSampling::HasDirectionalShadows())
		dirSoftShadow = ShadowSampling::GetLightingShadow(input.WorldPosition.xyz, dirVSMDetailedShadow);
#	endif

	float dirDetailedShadow = 1.0;

	if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir)) {
		dirDetailedShadow *= shadowColor.x;

#	if !defined(VOLUMETRIC_SHADOWS)
		dirSoftShadow = dirDetailedShadow;
#	endif
	} else {
		dirDetailedShadow = dirVSMDetailedShadow;
	}

#	if defined(SCREEN_SPACE_SHADOWS) && defined(DEFERRED)
	if (!SharedData::InInterior && dirLightAngle >= 0.0)
		dirDetailedShadow *= ScreenSpaceShadows::GetScreenSpaceShadow(input.Position.xyz, screenUV, screenNoise);
#	endif

#	if defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
	[branch] if (inWorld && SharedData::extendedMaterialSettings.EnableShadows)
	{
		float3 dirLightDirectionTS = mul(refractedDirLightDirection, tbn).xyz;
#		if defined(LANDSCAPE)
#			if defined(TRUE_PBR)
		if (SharedData::extendedMaterialSettings.EnableParallax) {
#			else
		if (SharedData::extendedMaterialSettings.EnableTerrainParallax || (SharedData::extendedMaterialSettings.EnableParallax && Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement)) {
#			endif
#			if defined(TERRAIN_VARIATION)
			float weights[6];
			// Initialize weights array
			weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0.0;

			float sh0 = ExtendedMaterials::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, parallaxShadowQuality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, weights);

			dirDetailedShadow *= ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, dirLightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams, sharedOffset, dx, dy);
#			else
			// Standard terrain parallax shadow without stochastic sampling
			dirDetailedShadow *= ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, dirLightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams);
#			endif
		}
#		elif defined(PARALLAX)
		[branch] if (SharedData::extendedMaterialSettings.EnableParallax)
			dirDetailedShadow *= ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, lerp(parallaxShadowQuality, 1.0, SharedData::extendedMaterialSettings.ExtendShadows), screenNoise, displacementParams);
#		elif defined(EMAT_ENVMAP)
		[branch] if (complexMaterialParallax)
			dirDetailedShadow *= ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexEnvMaskSampler, SampEnvMaskSampler, 3, lerp(parallaxShadowQuality, 1.0, SharedData::extendedMaterialSettings.ExtendShadows), screenNoise, displacementParams);
#		elif defined(TRUE_PBR) && !defined(LODLANDSCAPE) && !defined(FACEGEN)
		[branch] if (PBRParallax)
			dirDetailedShadow *= ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, lerp(parallaxShadowQuality, 1.0, SharedData::extendedMaterialSettings.ExtendShadows), screenNoise, displacementParams);
#		endif  // LANDSCAPE
	}
#	endif  // defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

#	if defined(CS_HAIR) && defined(HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		vertexNormal.xyz = worldNormal.xyz;
		worldNormal.xyz = hairT;
	}
#	endif

	float3 diffuseColor = 0.0.xxx;
	float3 specularColor = 0.0.xxx;
	float3 transmissionColor = 0.0.xxx;

	float3 lightsDiffuseColor = 0.0.xxx;
	float3 coatLightsDiffuseColor = 0.0.xxx;
	float3 lightsSpecularColor = 0.0.xxx;

	float3 lodLandDiffuseColor = 0;

#	if defined(WETTERNESS)
	float3 wetIndirectNormal = wetnessNormal;
#		if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)
	if (terrainWetIndirectNormalStability > 1e-4) {
		wetIndirectNormal = NormalizeWetnessVector(lerp(wetIndirectNormal, vertexNormal, terrainWetIndirectNormalStability), wetnessNormal);
	}
#		endif
	float3 wetReflectionModeConfig = 0.0.xxx;
	float3 wetReflectionModeConfigDirect = 0.0.xxx;
	WetnessDirectLightingParams wetDirectLightingParams = (WetnessDirectLightingParams)0;
	const bool wetLightingVisible = wetSpecularEnabled && waterRoughnessSpecular < 0.999 && wetnessGlossinessSpecular > 1e-4;
	bool wetDirectLightingVisible = false;
	bool wetIndirectLightingVisible = false;
	[branch] if (wetLightingVisible) {
		wetReflectionModeConfig = GetWetReflectionModeConfig(CS_WETNESS_SETTINGS.WetIndirectSpecularScale);
		if (postRainOverridePhase > 1e-4) {
			// Post-rain puddles keep the general Wet Reflection scale as the baseline.
			// The post-rain controls only bias that baseline up or down.
			wetReflectionModeConfig.z = lerp(
				wetReflectionModeConfig.z,
				wetReflectionModeConfig.z * postRainPuddleReflectionOverrideScale,
				postRainOverridePhase);
		}
		wetReflectionModeConfigDirect = wetReflectionModeConfig;
		wetReflectionModeConfigDirect.z *= wetDirectSpecularScale;
		wetDirectLightingVisible = wetReflectionModeConfigDirect.z > 1e-4;
		wetIndirectLightingVisible = wetReflectionModeConfig.z > 1e-4 && wetHighlightReflectanceScale > 1e-4;
	}
#	endif

	// Directiontal Lighting
	DirectContext dirLightContext;
	DirectLightingOutput dirLightOutput;
#	if defined(TRUE_PBR)
	dirLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedDirLightDirection, DirLightDirection, dirLightColor, dirDetailedShadow, dirSoftShadow);
#	else
	dirLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, DirLightDirection, dirLightColor, dirDetailedShadow, dirSoftShadow);
#		if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, DirLightDirection, screenNoise);
		dirLightContext.hairShadow = hairShadow;
	}
#		endif
#	endif

#	if defined(WETTERNESS)
	if (wetDirectLightingVisible) {
		wetDirectLightingParams = CreateWetnessDirectLightingParams(wetnessNormal, dirLightContext.viewDir, waterRoughnessSpecular, wetReflectionModeConfigDirect);
		wetDirectLightingVisible = wetDirectLightingParams.enabled > 0.0;
	}
#	endif

	float2 uvOriginal_ddx = ddx(uvOriginal);
	float2 uvOriginal_ddy = ddy(uvOriginal);
	EvaluateLighting(dirLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, dirLightOutput);
#	if defined(WETTERNESS)
	if (wetDirectLightingVisible)
		EvaluateWetnessLighting(wetnessNormal, dirLightContext, waterRoughnessSpecular, wetDirectLightingParams, dirLightOutput);
#	elif defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1)
		CS_EVALUATE_WETNESS_LIGHTING(wetnessNormal, dirLightContext, waterRoughnessSpecular, dirLightOutput);
#	endif

	lightsDiffuseColor += dirLightOutput.diffuse;
	lightsSpecularColor += dirLightOutput.specular;
#	if defined(TRUE_PBR)
	coatLightsDiffuseColor += dirLightOutput.coatDiffuse;
#		if defined(LOD_LAND_BLEND)
	lodLandDiffuseColor += dirLightColor / Math::PI * saturate(dirLightAngle) * dirDetailedShadow;
#		endif
#	endif
	transmissionColor += dirLightOutput.transmission;

#	if !defined(LOD)
#		if !defined(LIGHT_LIMIT_FIX)
	[loop] for (uint lightIndex = 0; lightIndex < numLights; lightIndex++)
	{
		float3 lightDirection = PointLightPosition[lightIndex].xyz - input.WorldPosition.xyz;
		float lightDist = length(lightDirection);
		float intensityFactor = saturate(lightDist / PointLightPosition[lightIndex].w);
		if (intensityFactor == 1)
			continue;

		float intensityMultiplier = 1 - intensityFactor * intensityFactor;
		float3 lightColor = Color::PointLight(PointLightColor[lightIndex].xyz) * intensityMultiplier;
		float lightShadow = 1.f;
		if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) {
			if (lightIndex < numShadowLights) {
				lightShadow *= shadowColor[ShadowLightMaskSelect[lightIndex]];
			}
		}

		float3 normalizedLightDirection = normalize(lightDirection);

		DirectContext pointLightContext;
		DirectLightingOutput pointLightOutput;
#			if defined(TRUE_PBR)
		{
			float3 refractedLightDirection = normalizedLightDirection;
#				if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
			{
				if (dot(normalizedLightDirection, coatWorldNormal) > 0)
					refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
			}
#				endif
			pointLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightColor, lightShadow, lightShadow);
		}
#			else
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, normalizedLightDirection, lightColor, lightShadow, lightShadow);
#				if defined(HAIR) && defined(CS_HAIR)
		if (SharedData::hairSpecularSettings.Enabled) {
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, normalizedLightDirection, screenNoise);
			pointLightContext.hairShadow = hairShadow;
		}
#				endif
#			endif
		EvaluateLighting(pointLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, pointLightOutput);
#			if defined(WETTERNESS)
		if (wetDirectLightingVisible)
			EvaluateWetnessLighting(wetnessNormal, pointLightContext, waterRoughnessSpecular, wetDirectLightingParams, pointLightOutput);
#			elif defined(WETNESS_EFFECTS)
		if (waterRoughnessSpecular < 1)
			CS_EVALUATE_WETNESS_LIGHTING(wetnessNormal, pointLightContext, waterRoughnessSpecular, pointLightOutput);
#			endif
		lightsDiffuseColor += pointLightOutput.diffuse;
		lightsSpecularColor += pointLightOutput.specular;
#			if defined(TRUE_PBR)
		coatLightsDiffuseColor += pointLightOutput.coatDiffuse;
#			endif
		transmissionColor += pointLightOutput.transmission;
	}

#		else

	uint numClusteredLights = 0;
	uint totalLightCount = LightLimitFix::NumStrictLights;
	uint clusterIndex = 0;
	uint lightOffset = 0;
#			if defined(DEFERRED)
	const bool contactShadowsEnabled = SharedData::lightLimitFixSettings.ContactShadowFlags != 0;
	uint contactShadowOffset = 0;
	uint contactShadowCount = 0;
	float contactShadowNoise = 0.0;
	bool hasContactShadowNoise = false;
	float2 contactShadowScreenDim = 0.0;
	[branch] if (contactShadowsEnabled)
		contactShadowScreenDim = LightLimitFix::GetContactShadowScreenDim();
#			endif
	if (inWorld && LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		numClusteredLights = LightLimitFix::lightGrid[clusterIndex].lightCount;
		totalLightCount += numClusteredLights;
		lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
#			if defined(DEFERRED)
		[branch] if (contactShadowsEnabled)
			LightLimitFix::GetContactShadowClusterRange(clusterIndex, contactShadowOffset, contactShadowCount);
#			endif
	}

	[loop] for (uint lightIndex = 0; lightIndex < totalLightCount; lightIndex++)
	{
		LightLimitFix::Light light;
		uint clusteredLightIndex = 0;
		bool isClusteredLight = false;
		if (lightIndex < LightLimitFix::NumStrictLights) {
			light = LightLimitFix::StrictLights[lightIndex];
		} else {
			clusteredLightIndex = LightLimitFix::lightList[lightOffset + (lightIndex - LightLimitFix::NumStrictLights)];
			light = LightLimitFix::lights[clusteredLightIndex];
			isClusteredLight = true;

			if (LightLimitFix::IsLightIgnored(light))
				continue;
		}

		float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
		float lightDist = length(lightDirection);

#			if defined(ISL)
		float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
		if (intensityMultiplier < 1e-5)
			continue;
#			else
		float intensityFactor = saturate(lightDist / light.radius);
		if (intensityFactor == 1)
			continue;
		float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#			endif

		const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
		float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * intensityMultiplier * light.fade;
		float lightShadow = 1.0;

		float shadowComponent = 1.0;
		if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) {
			if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
				shadowComponent = shadowColor[light.shadowLightIndex];
				lightShadow *= shadowComponent;
			}
		}

		float3 normalizedLightDirection = normalize(lightDirection);
		float lightAngle = dot(worldNormal.xyz, normalizedLightDirection.xyz);
#			if defined(DEFERRED)
		const bool isParticleLight = (light.lightFlags & LightLimitFix::LightFlags::Particle) != 0;
		const bool canUseContactShadow = contactShadowsEnabled && LightLimitFix::CanUseContactShadows(light, isParticleLight);
		bool isContactShadowCandidate = false;
		[branch] if (canUseContactShadow)
		{
			if (isClusteredLight) {
				isContactShadowCandidate = contactShadowCount > 0 && LightLimitFix::IsContactShadowCandidate(clusteredLightIndex, contactShadowOffset, contactShadowCount);
			} else {
				isContactShadowCandidate = !isParticleLight && lightIndex < LightLimitFix::GetStrictContactShadowBudget();
			}
		}
#			endif

		float3 refractedLightDirection = normalizedLightDirection;
#			if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			if (dot(normalizedLightDirection, coatWorldNormal) > 0)
				refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
		}
#			endif

		float parallaxShadow = 1;

#			if defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
		[branch] if (
			SharedData::extendedMaterialSettings.EnableShadows &&
			!(light.lightFlags & LightLimitFix::LightFlags::Simple) &&
			lightAngle > 0.0 &&
			shadowComponent != 0.0)
		{
			float3 lightDirectionTS = normalize(mul(refractedLightDirection, tbn).xyz);
#				if defined(PARALLAX)
			[branch] if (SharedData::extendedMaterialSettings.EnableParallax)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#				elif defined(LANDSCAPE)
#					if defined(TRUE_PBR)
			if (SharedData::extendedMaterialSettings.EnableParallax)
#					else
			if (SharedData::extendedMaterialSettings.EnableTerrainParallax || (SharedData::extendedMaterialSettings.EnableParallax && Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement))
#					endif
#					if defined(TERRAIN_VARIATION)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, lightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams, sharedOffset, dx, dy);
#					else
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input, uv, mipLevels, lightDirectionTS, sh0, parallaxShadowQuality, screenNoise, displacementParams);
#					endif
#				elif defined(EMAT_ENVMAP)
			[branch] if (complexMaterialParallax)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexEnvMaskSampler, SampEnvMaskSampler, 3, parallaxShadowQuality, screenNoise, displacementParams);
#				elif defined(TRUE_PBR) && !defined(LODLANDSCAPE) && !defined(FACEGEN)
			[branch] if (PBRParallax)
				parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#				endif
		}
#			endif  // defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

		float contactShadow = 1.0;
#			if defined(DEFERRED)
		[branch] if (
			isContactShadowCandidate &&
			lightAngle > 0.0 &&
			shadowComponent != 0.0 &&
			intensityMultiplier > 1e-5)
		{
			if (!hasContactShadowNoise) {
				contactShadowNoise = Random::InterleavedGradientNoise(
					LightLimitFix::GetContactShadowNoiseCoord(input.Position.xy, screenUV, contactShadowScreenDim),
					SharedData::FrameCount);
				hasContactShadowNoise = true;
			}

			contactShadow = LightLimitFix::ContactShadows(viewPosition, screenUV, light.positionWS.xyz, contactShadowNoise, contactShadowScreenDim, isParticleLight);
		}
#			endif

		DirectContext pointLightContext;
		DirectLightingOutput pointLightOutput;
#			if defined(TRUE_PBR)
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightColor, lightShadow * contactShadow, parallaxShadow);
#			else
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, normalizedLightDirection, lightColor, lightShadow * contactShadow, parallaxShadow);
#				if defined(HAIR) && defined(CS_HAIR)
		if (SharedData::hairSpecularSettings.Enabled) {
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, normalizedLightDirection, screenNoise);
			pointLightContext.hairShadow = hairShadow;
		}
#				endif
#			endif
		EvaluateLighting(pointLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, pointLightOutput);
#			if defined(WETTERNESS)
		if (wetDirectLightingVisible)
			EvaluateWetnessLighting(wetnessNormal, pointLightContext, waterRoughnessSpecular, wetDirectLightingParams, pointLightOutput);
#			elif defined(WETNESS_EFFECTS)
		if (waterRoughnessSpecular < 1)
			CS_EVALUATE_WETNESS_LIGHTING(wetnessNormal, pointLightContext, waterRoughnessSpecular, pointLightOutput);
#			endif

		lightsDiffuseColor += pointLightOutput.diffuse;
		lightsSpecularColor += pointLightOutput.specular;
#			if defined(TRUE_PBR)
		coatLightsDiffuseColor += pointLightOutput.coatDiffuse;
#			endif
		transmissionColor += pointLightOutput.transmission;
	}
#		endif
#	endif

	diffuseColor += lightsDiffuseColor;
	specularColor += lightsSpecularColor;

#	if !defined(LANDSCAPE)
	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::CharacterLight) {
		float charLightMul = saturate(dot(viewDirection, worldNormal.xyz)) * CharacterLightParams.x + CharacterLightParams.y * saturate(dot(float2(0.164398998, -0.986393988), worldNormal.yz));
		float charLightColor = min(CharacterLightParams.w, max(0, CharacterLightParams.z * TexCharacterLightProjNoiseSampler.Sample(SampCharacterLightProjNoiseSampler, baseShadowUV).x));
		diffuseColor += (charLightMul * charLightColor).xxx;
	}
#	endif

#	if defined(EYE) && defined(VANILLA_EYE_NORMAL)
	worldNormal.xyz = input.EyeNormal;
#	endif  // EYE

	// sRGB by default, linear if LL on
	float3 emitColor = Color::EmitColor(EmitColor);
#	if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	bool hasEmissive = (0x3F & (Permutation::PixelShaderDescriptor >> 24)) == Permutation::LightingTechnique::Glowmap;
#		if defined(TRUE_PBR)
	hasEmissive = hasEmissive || (PBRFlags & PBR::Flags::HasEmissive != 0);
#		endif
	[branch] if (hasEmissive)
	{
		// Input TexGlowSampler = linear by default, but Color::Glowmap returns in sRGB if LL disabled
		float3 glowColor = Color::Glowmap(TexGlowSampler.Sample(SampGlowSampler, uv).xyz);

#		if defined(TRUE_PBR)
		float3 emitVertexColor = Color::SrgbToLinear(input.Color.xyz);
		float emitVertexAO = max(max(emitVertexColor.r, emitVertexColor.g), emitVertexColor.b);
		emitVertexColor = emitVertexAO == 0.0f ? 1.0f : emitVertexColor * lerp(1 / max(emitVertexAO, 1e-4), 1, SharedData::truePBRSettings.VertexAOStrength);

		if (!SharedData::linearLightingSettings.enableLinearLighting) {
			emitColor = Color::SrgbToLinear(emitColor);
			glowColor = Color::SrgbToLinear(glowColor);
			emitColor *= glowColor;
			emitColor *= emitVertexColor;
			emitColor = Color::LinearToSrgb(emitColor);
		} else {
			emitColor *= glowColor;
			emitColor *= emitVertexColor;
		}
#		else
		if (!SharedData::linearLightingSettings.enableLinearLighting) {
			emitColor = Color::LinearToSrgb(Color::SrgbToLinear(emitColor) * Color::SrgbToLinear(glowColor));
		} else {
			emitColor *= glowColor;
		}
#		endif  // TRUE_PBR
	}
#	endif

#	if !defined(TRUE_PBR)
	diffuseColor += emitColor.xyz;
#	endif

	IndirectContext indirectContext = (IndirectContext)0;
	IndirectLobeWeights indirectLobeWeights;

	float3 ambientNormal = worldNormal.xyz;
#	if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		if (SharedData::hairSpecularSettings.HairMode == 1)
			ambientNormal = normalize(viewDirection - hairT * dot(viewDirection, hairT));
		else
			ambientNormal = vertexNormal.xyz;
		screenSpaceNormal = normalize(FrameBuffer::WorldToView(ambientNormal, false));
	}
#	endif

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(ambientNormal)));

#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		if (SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection) {
			directionalAmbientColor = ImageBasedLighting::GetStaticDiffuseIBL(ambientNormal, SampColorSampler);
		}
	}
#	endif

#	if defined(SKYLIGHTING)
	float skylightingDiffuse = 1;
	float skylightingFadeOutFactor = 1.0;
	if (!SharedData::InInterior) {
		skylightingFadeOutFactor = Skylighting::GetFadeOutFactor(input.WorldPosition.xyz);
		skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, ambientNormal, skylightingFadeOutFactor);
	}
#	endif

#	if defined(LANDSCAPE)
	if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;
	else
		input.Color.xyz /= max(max(max(input.Color.x, input.Color.y), input.Color.z), EPSILON_DIVISION);
#	endif

#	if defined(HAIR)
	float3 vertexColor = lerp(1, Color::ColorToLinear(TintColor.xyz), Color::ColorToLinear(input.Color.y));
	float vertexAO = 1;
#		if defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled)
		vertexColor = 1;
#		endif
#	elif defined(SKYLIGHTING)
	float3 vertexColor = input.Color.xyz;
	float vertexAO = Color::ColorToLinear(max(max(vertexColor.r, vertexColor.g), vertexColor.b).xxx).x;
#		if defined(TRUE_PBR)
	vertexAO = lerp(1, vertexAO, SharedData::truePBRSettings.VertexAOStrength);
	vertexColor = 1;
#		endif
	// Modify skylightingDiffuse such that skylightingDiffuse * vertexAO = min(skylightingDiffuse, vertexAO)
	skylightingDiffuse = saturate(skylightingDiffuse / max(vertexAO, 1e-5));
#	else
#		if defined(TRUE_PBR)
	float3 vertexColor = 1;
#		else
	float3 vertexColor = input.Color.xyz;
#		endif
	float vertexAO = Color::ColorToLinear(max(max(vertexColor.r, vertexColor.g), vertexColor.b).xxx).x;
#	endif  // defined (HAIR)

#	if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		if (!(SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection)) {
#		if defined(SKYLIGHTING)
			directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -ambientNormal, skylightingDiffuse);
#		else
			directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -ambientNormal);
#		endif
		}
	}
#	endif

	float3 reflectionDiffuseColor = diffuseColor + directionalAmbientColor;

#	if defined(TRUE_PBR) && defined(LOD_LAND_BLEND) && !defined(DEFERRED)
	lodLandDiffuseColor += directionalAmbientColor;
#	endif

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

#	if defined(WETTERNESS)
#		if !(defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(EYE)) || defined(TREE_ANIM)
	// Persistent dry shoreline darkening uses the same shore wetness/range mask
	// as legacy Wetness Effects, but remains available when rain runtime is dry.
	float shorePersistentDarkeningMask = shoreWetnessDarkeningMask;
	float shorePersistentDarkeningStrength = max(0.0, CS_WETNESS_SETTINGS.ShorePersistentDarkeningStrength);
	shorePersistentDarkeningStrength = (shorePersistentDarkeningStrength < 1e-5) ? 0.0 : shorePersistentDarkeningStrength;
	const bool shorePersistentDarkeningEnabled = shorePersistentDarkeningStrength > 1e-4 && shorePersistentDarkeningMask > 1e-4;
	[branch] if (wetnessEnabled || shorePersistentDarkeningEnabled) {
#			if defined(TRUE_PBR)
#				if !defined(LANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		porosity = 0;
	}
	else
#				endif
	{
		porosity = lerp(porosity, 0.0, saturate(sqrt(material.Metallic)));
	}
#			elif defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX)
	porosity = lerp(porosity, 0.0, saturate(sqrt(envMask)));
#			endif
	float wetDarkeningStrength = max(0.0, CS_WETNESS_SETTINGS.WetDarkeningStrength);
	float lodSafeWetDarkeningFade = 1.0;
	float viewDistance = abs(viewPosition.z);
	lodSafeWetDarkeningFade = 1.0 - smoothstep(4096.0, 12288.0, viewDistance);
	wetDarkeningStrength *= lerp(0.65, 1.0, lodSafeWetDarkeningFade);
	float rainDrivenWetness = wetSurfaceDarkeningMask;
	float wetnessDarkeningAmount = porosity * rainDarkeningAbsorption * rainDrivenWetness * wetDarkeningStrength;
	float rainWetVisualMask = smoothstep(0.02, 0.08, rainDrivenWetness);
	float wetDarkeningBlend = saturate(0.8 * wetDarkeningStrength) * rainWetVisualMask;
	wetDarkeningBlend *= lerp(0.75, 1.0, lodSafeWetDarkeningFade);
	if (wetDarkeningBlend > 0.0 && (wetnessDarkeningAmount != 0.0 || any(material.BaseColor < 0.0))) {
		float3 wetDarkenedBaseColor = pow(abs(material.BaseColor), 1.0 + wetnessDarkeningAmount);
		material.BaseColor = lerp(material.BaseColor, wetDarkenedBaseColor, wetDarkeningBlend);
	}
#			if !defined(SKIN) && !defined(HAIR)
	[branch] if (shorePersistentDarkeningEnabled) {
		float shoreDarkeningMask = shorePersistentDarkeningMask;
		float shoreDarkeningAmount = porosity * shoreDarkeningAbsorption * shoreDarkeningMask * shorePersistentDarkeningStrength;
		float shoreDarkeningBlend = saturate(0.5 * shorePersistentDarkeningStrength) * shoreDarkeningMask;
		shoreDarkeningBlend *= lerp(0.75, 1.0, lodSafeWetDarkeningFade);
		if (shoreDarkeningBlend > 0.0 && (shoreDarkeningAmount != 0.0 || any(material.BaseColor < 0.0))) {
			float3 shoreDarkenedBaseColor = pow(abs(material.BaseColor), 1.0 + shoreDarkeningAmount);
			material.BaseColor = lerp(material.BaseColor, shoreDarkenedBaseColor, shoreDarkeningBlend);
		}
	}
#			endif
	}
#		endif
#	elif defined(WETNESS_EFFECTS)
#		if !(defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(EYE)) || defined(TREE_ANIM)
#			if defined(TRUE_PBR)
#				if !defined(LANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		porosity = 0;
	}
	else
#				endif
	{
		porosity = lerp(porosity, 0.0, saturate(sqrt(material.Metallic)));
	}
#			elif defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX)
	porosity = lerp(porosity, 0.0, saturate(sqrt(envMask)));
#			endif
	float wetnessDarkeningAmount = porosity * wetnessGlossinessAlbedo;
	material.BaseColor = lerp(material.BaseColor, pow(abs(material.BaseColor), 1.0 + wetnessDarkeningAmount), 0.5);
#		endif
#	endif

	float4 color = 0;

	indirectContext = CreateIndirectLightingContext(ambientNormal, vertexNormal.xyz, viewDirection);

	GetIndirectLobeWeights(indirectLobeWeights, indirectContext, material, uvOriginal);

#	if defined(WETTERNESS)
#		if defined(DYNAMIC_CUBEMAPS)
	float3 wetnessReflectance = 0.0;
	bool wetCubemapReflectanceVisible = false;
	if (wetIndirectLightingVisible) {
		wetnessReflectance = GetWetnessIndirectLobeWeights(indirectLobeWeights, wetIndirectNormal, waterRoughnessSpecular, indirectContext, wetReflectionModeConfig);
		wetnessReflectance *= wetHighlightReflectanceScale * wetPuddleSkyReflectionScale;
		wetCubemapReflectanceVisible = any(wetnessReflectance > 0.0);
	}
#		else
	float3 wetnessReflectance = 0.0;
#		endif
#	elif defined(WETNESS_EFFECTS)
#		if defined(DYNAMIC_CUBEMAPS)
	float3 wetnessReflectance = CS_GET_WETNESS_INDIRECT(indirectLobeWeights, wetnessNormal, waterRoughnessSpecular, indirectContext);
#		else
	float3 wetnessReflectance = 0.0;
#		endif
#	endif
#	if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	indirectLobeWeights.specular *= envMask;
#	endif

#	if defined(SPECULAR) && !defined(TRUE_PBR)
	indirectLobeWeights.specular *= MaterialData.yyy;
	specularColor *= MaterialData.yyy;
#	endif

#	if defined(TRUE_PBR)
	{
		float3 directLightsDiffuseInput = diffuseColor * material.BaseColor;
		[branch] if ((PBRFlags & PBR::Flags::ColoredCoat) != 0)
		{
			directLightsDiffuseInput = lerp(directLightsDiffuseInput, material.CoatColor * coatLightsDiffuseColor, material.CoatStrength);
		}

		color.xyz += directLightsDiffuseInput;
	}

	[branch] if ((PBRFlags & PBR::Flags::HasEmissive) != 0)
	{
		color.xyz += emitColor.xyz;
	}
#	else
	color.xyz += diffuseColor * material.BaseColor;
#	endif

	color.xyz += indirectLobeWeights.diffuse * directionalAmbientColor;
	color.xyz += transmissionColor;

	color.xyz *= vertexColor;

#	if defined(MULTI_LAYER_PARALLAX)
	float layerValue = MultiLayerParallaxData.x * TexLayerSampler.Sample(SampLayerSampler, uv).w;
	float3 tangentViewDirection = mul(viewDirection, tbn);
	float3 layerNormal = MultiLayerParallaxData.yyy * (normalColor.xyz * 2.0.xxx + float3(-1, -1, -2)) + float3(0, 0, 1);
	float layerViewAngle = dot(-tangentViewDirection.xyz, layerNormal.xyz) * 2;
	float3 layerViewProjection = -layerNormal.xyz * layerViewAngle.xxx - tangentViewDirection.xyz;
	float2 layerUv = uv * MultiLayerParallaxData.zw + (0.0009765625 * (layerValue / abs(layerViewProjection.z))).xx * layerViewProjection.xy;

	float3 layerColor = TexLayerSampler.Sample(SampLayerSampler, layerUv).xyz;

	float mlpBlendFactor = saturate(viewNormalAngle) * (1.0 - baseColor.w);

#		if defined(SKYLIGHTING)
	color.xyz = lerp(color.xyz, (diffuseColor + directionalAmbientColor * skylightingDiffuse) * vertexColor * layerColor, mlpBlendFactor);
#		else
	color.xyz = lerp(color.xyz, (diffuseColor + directionalAmbientColor) * vertexColor * layerColor, mlpBlendFactor);
#		endif

	indirectLobeWeights.diffuse *= 1.0 - mlpBlendFactor;
#	endif  // MULTI_LAYER_PARALLAX

#	if defined(SNOW)
	if (useSnowSpecular)
		specularColor = 0;
#	endif

	diffuseColor = reflectionDiffuseColor;

#	if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#		if defined(DYNAMIC_CUBEMAPS)
	if (!dynamicCubemap)
#		endif
		specularColor += envColor * Color::IrradianceToLinear(diffuseColor);
	indirectLobeWeights.diffuse += envColor;
#	endif

#	if defined(EMAT_ENVMAP)
	specularColor *= complexSpecular;
#	endif  // defined (EMAT) && defined(ENVMAP)

#	if defined(LOD_LAND_BLEND) && defined(TRUE_PBR)
	{
		lodLandDiffuseColor += directionalAmbientColor;
		float3 litLodLandColor = vertexColor * lodLandColor.xyz * lodLandFadeFactor * lodLandDiffuseColor;
		color.xyz = lerp(color.xyz * Color::PBRLightingScale, litLodLandColor, lodLandBlendFactor);

		specularColor = lerp(specularColor * Color::PBRLightingScale, 0, lodLandBlendFactor);
		indirectLobeWeights.diffuse = lerp(indirectLobeWeights.diffuse * Color::PBRLightingScale, vertexColor * lodLandColor.xyz * lodLandFadeFactor, lodLandBlendFactor);
		indirectLobeWeights.specular = lerp(indirectLobeWeights.specular, 0, lodLandBlendFactor);
		material.Roughness = lerp(material.Roughness, 1, lodLandBlendFactor);
	}
#	elif defined(TRUE_PBR)
	color.xyz *= Color::PBRLightingScale;
	specularColor *= Color::PBRLightingScale;
	indirectLobeWeights.diffuse *= Color::PBRLightingScale;
#	endif

	float3 outputAlbedo = indirectLobeWeights.diffuse * vertexColor.xyz;

	directionalAmbientColor *= outputAlbedo;

#	if defined(SKYLIGHTING)
#		if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#		endif
	{
		Skylighting::ApplySkylighting(color.xyz, directionalAmbientColor, outputAlbedo, skylightingDiffuse);
	}
#	endif

#	if !defined(DEFERRED)
	color.xyz = Color::IrradianceToLinear(color.xyz);
	color.xyz += specularColor;

#		if defined(WETTERNESS)
	float3 baseMaterialCubemapSpecular = indirectLobeWeights.specular;
	baseMaterialCubemapSpecular *= (1.0 - wetRainCubemapSuppression);
#		endif

	if (any(indirectLobeWeights.specular > 0)
#		if defined(WETTERNESS) || defined(WETNESS_EFFECTS)
		|| any(wetnessReflectance > 0)
#		endif
	)
#		if defined(DYNAMIC_CUBEMAPS)
#			if defined(SKYLIGHTING)
		color.xyz +=
#				if defined(WETTERNESS)
			baseMaterialCubemapSpecular *
#				else
			indirectLobeWeights.specular *
#				endif
			DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(worldNormal, viewDirection, material.Roughness, skylightingSH);
#				if defined(WETTERNESS)
	if (wetCubemapReflectanceVisible) {
		float3 wetCubemapIrradiance = DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(wetIndirectNormal, viewDirection, waterRoughnessSpecular, skylightingSH);
		wetCubemapIrradiance = ApplyWetternessCubemapClarity(wetCubemapIrradiance, postRainBlend, postRainCubemapGlareReductionFromClarity);
		color.xyz += wetnessReflectance * wetCubemapIrradiance;
	}
#				elif defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1)
		color.xyz += wetnessReflectance * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(wetnessNormal, viewDirection, waterRoughnessSpecular, skylightingSH);
#				endif
#			else
		color.xyz +=
#				if defined(WETTERNESS)
			baseMaterialCubemapSpecular *
#				else
			indirectLobeWeights.specular *
#				endif
			DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(worldNormal, viewDirection, material.Roughness);
#				if defined(WETTERNESS)
	if (wetCubemapReflectanceVisible) {
		float3 wetCubemapIrradiance = DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(wetIndirectNormal, viewDirection, waterRoughnessSpecular);
		wetCubemapIrradiance = ApplyWetternessCubemapClarity(wetCubemapIrradiance, postRainBlend, postRainCubemapGlareReductionFromClarity);
		color.xyz += wetnessReflectance * wetCubemapIrradiance;
	}
#				elif defined(WETNESS_EFFECTS)
	if (waterRoughnessSpecular < 1)
		color.xyz += wetnessReflectance * DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(wetnessNormal, viewDirection, waterRoughnessSpecular);
#				endif
#			endif
#		else
		color.xyz += indirectLobeWeights.specular * directionalAmbientColor;
#		endif

	color.xyz = Color::IrradianceToGamma(color.xyz);
	float3 fogColor = Color::Fog(input.FogParam.xyz);
	float fogFactor = Color::FogAlpha(input.FogParam.w);
#		if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
		fogColor = ImageBasedLighting::GetFogIBLColor(fogColor);
	}
#		endif
#		if defined(EXP_HEIGHT_FOG)
	float3 vanillaFogColor = fogColor;
	float vanillaFogFactor = fogFactor;
	if (SharedData::exponentialHeightFogSettings.enabled) {
		float4 exponentialHeightFog;
		if (inReflection) {
			exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFogNoVolumetric(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		} else {
			exponentialHeightFog = ExponentialHeightFog::GetExponentialHeightFog(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		}
		fogColor = exponentialHeightFog.xyz;
		fogFactor = exponentialHeightFog.w;
	}
#		endif
	if ((FrameBuffer::FrameParams.y && FrameBuffer::FrameParams.z) || inReflection) {
#		if defined(EXP_HEIGHT_FOG)
		if (SharedData::exponentialHeightFogSettings.enabled) {
			if (!ExponentialHeightFog::ShouldDisableVanillaFog()) {
				color.xyz = lerp(color.xyz, vanillaFogColor, vanillaFogFactor);
			}
			color.xyz = lerp(color.xyz, fogColor, fogFactor);
		} else {
			color.xyz = lerp(color.xyz, fogColor, fogFactor);
		}
#		else
		color.xyz = lerp(color.xyz, fogColor, fogFactor);
#		endif
	}
#	endif

#	if defined(TESTCUBEMAP) && defined(ENVMAP) && defined(DYNAMIC_CUBEMAPS)
	baseColor.xyz = 0.0;
	specularColor = 0.0;
	diffuseColor = 0.0;
	dynamicCubemap = true;
	envColor = 1.0;
	material.Roughness = 0.0;
	color.xyz = 0;
#	endif

#	if defined(LANDSCAPE) && !defined(LOD_LAND_BLEND)
	psout.Diffuse.w = 0;
#	else
	float alpha = baseColor.w;
#		if defined(EMAT) && !defined(LANDSCAPE)
#			if defined(PARALLAX)
	alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
#			elif defined(TRUE_PBR)
	[branch] if (PBRParallax)
	{
		alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
	}
#			endif
#		endif
#		if defined(DO_ALPHA_TEST)
	[branch] if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::AdditionalAlphaMask) != 0)
	{
		uint2 alphaMask = input.Position.xy;
		alphaMask.x = ((alphaMask.x << 2) & 12);
		alphaMask.x = (alphaMask.y & 3) | (alphaMask.x & ~3);
		const float maskValues[16] = {
			0.003922,
			0.533333,
			0.133333,
			0.666667,
			0.800000,
			0.266667,
			0.933333,
			0.400000,
			0.200000,
			0.733333,
			0.066667,
			0.600000,
			0.996078,
			0.466667,
			0.866667,
			0.333333,
		};

		float testTmp = 0;
		if (MaterialData.z - maskValues[alphaMask.x] < 0) {
			discard;
		}
	}
	else
#		endif  // defined(DO_ALPHA_TEST)
	{
		alpha *= MaterialData.z;
	}
#		if !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
	alpha *= input.Color.w;
#		endif  // !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
#		if defined(DO_ALPHA_TEST)
#			if defined(DEPTH_WRITE_DECALS)
	if (alpha - 0.0156862754 < 0) {
		discard;
	}
	alpha = saturate(1.05 * alpha);
#			endif  // DEPTH_WRITE_DECALS
	if (alpha - AlphaTestRefRS < 0) {
		discard;
	}
#		endif      // DO_ALPHA_TEST

#		if defined(ANISOTROPIC_ALPHA)
	// Uniform alpha material settings
	uint AlphaMaterialModel = ExtendedTranslucency::GetMaterialModelFromDescriptor(Permutation::ExtraFeatureDescriptor);
	float AlphaMaterialReduction = 0.f;
	float AlphaMaterialSoftness = 0.f;
	float AlphaMaterialStrength = 0.f;
	[branch] if (AlphaMaterialModel == ExtendedTranslucency::MaterialModel::Default)
	{
		AlphaMaterialModel = SharedData::extendedTranslucencySettings.MaterialModel;
		AlphaMaterialReduction = SharedData::extendedTranslucencySettings.Reduction;
		AlphaMaterialSoftness = SharedData::extendedTranslucencySettings.Softness;
		AlphaMaterialStrength = SharedData::extendedTranslucencySettings.Strength;
	}

	[branch] if (ExtendedTranslucency::IsValidMaterial(AlphaMaterialModel))
	{
		if (alpha >= 0.0156862754 && alpha < 1.0) {
			float originalAlpha = alpha;
			alpha = alpha * (1.0 - AlphaMaterialReduction);
			[branch] if (AlphaMaterialModel == ExtendedTranslucency::MaterialModel::AnisotropicFabric)
			{
#			if defined(SKINNED) || !defined(MODELSPACENORMALS)
				alpha = ExtendedTranslucency::GetViewDependentAlphaFabric2D(alpha, viewDirection, tbnTr);
#			else
				alpha = ExtendedTranslucency::GetViewDependentAlphaFabric1D(alpha, viewDirection, worldNormal.xyz);
#			endif
			}
			else if (AlphaMaterialModel == ExtendedTranslucency::MaterialModel::IsotropicFabric)
			{
				alpha = ExtendedTranslucency::GetViewDependentAlphaFabric1D(alpha, viewDirection, worldNormal.xyz);
			}
			else
			{
				alpha = ExtendedTranslucency::GetViewDependentAlphaNaive(alpha, viewDirection, worldNormal.xyz);
			}
			alpha = saturate(ExtendedTranslucency::SoftClamp(alpha, 2.0f - AlphaMaterialSoftness));
			alpha = lerp(alpha, originalAlpha, AlphaMaterialStrength);
		}
	}
#		endif  // ANISOTROPIC_ALPHA

	psout.Diffuse.w = alpha;
#	endif

#	if defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 0) {
			psout.Diffuse.xyz = Color::TurboColormap(LightLimitFix::NumStrictLights >= 7.0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			psout.Diffuse.xyz = Color::TurboColormap((float)LightLimitFix::NumStrictLights / 15.0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 2) {
			psout.Diffuse.xyz = Color::TurboColormap((float)numClusteredLights / MAX_CLUSTER_LIGHTS);
		} else {
			psout.Diffuse.xyz = shadowColor.xyz;
		}
		baseColor.xyz = 0.0;
	} else {
		psout.Diffuse.xyz = color.xyz;
	}
#	else
	psout.Diffuse.xyz = color.xyz;
#	endif  // defined(LIGHT_LIMIT_FIX)

	psout.MotionVectors.xy = screenMotionVector.xy;
	psout.MotionVectors.zw = float2(0, psout.Diffuse.w);

#	if defined(DEFERRED)

#		if defined(TERRAIN_BLENDING)
	[flatten] if (SharedData::terrainBlendingSettings.Enabled)
	{
		psout.Diffuse.w = blendFactorTerrain;
	}
#		endif

	psout.MotionVectors.zw = float2(0.0, psout.Diffuse.w);
	psout.Specular = float4(specularColor, psout.Diffuse.w);
	psout.Albedo = float4(outputAlbedo, psout.Diffuse.w);

#		if defined(WETTERNESS)
	// Match rain-time indirect cubemap suppression in the deferred output. Without
	// this, the flattened wet normal and low roughness can still make the base material
	// reflect temporal sky cubemaps even after the explicit Wetterness lobe is muted.
	float wetDeferredCubemapScale = 1.0 - saturate(wetRainCubemapSuppression);
	float wetDeferredBlend = saturate(wetnessGlossinessSpecular * wetDeferredCubemapScale);
	indirectLobeWeights.specular *= lerp(1.0, wetDeferredCubemapScale, saturate(wetnessGlossinessSpecular));
	indirectLobeWeights.specular += wetnessReflectance;
	if (waterRoughnessSpecular < 1) {
		float3 wetScreenSpaceNormal = NormalizeWetnessVector(FrameBuffer::WorldToView(wetIndirectNormal, false), screenSpaceNormal);
		screenSpaceNormal = lerp(screenSpaceNormal, wetScreenSpaceNormal, wetDeferredBlend);
		material.Roughness = lerp(material.Roughness, waterRoughnessSpecular, wetDeferredBlend);
	}
#		elif defined(WETNESS_EFFECTS)
	indirectLobeWeights.specular += wetnessReflectance;
	if (waterRoughnessSpecular < 1) {
		// Reflection is from the water film surface; wetnessReflectance scales intensity by wetness amount.
		screenSpaceNormal = normalize(FrameBuffer::WorldToView(wetnessNormal, false));
		material.Roughness = waterRoughnessSpecular;
	}
#		endif

	psout.Reflectance = float4(indirectLobeWeights.specular, psout.Diffuse.w);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), saturate(1.0 - material.Roughness), psout.Diffuse.w);

#		if defined(SNOW)
#			if defined(TRUE_PBR)
	psout.Parameters.x = Color::RGBToLuminanceAlternative(specularColor);
	psout.Parameters.y = 0;
#			else
	psout.Parameters.x = Color::RGBToLuminanceAlternative(lightsSpecularColor);
#			endif
	psout.Parameters.w = psout.Diffuse.w;
#		endif

	float masksZ = Color::RGBToYCoCg(directionalAmbientColor).x;

#		if defined(SSS) && defined(SKIN)
	psout.Masks = float4(saturate(baseColor.a), !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsBeastRace), masksZ, psout.Diffuse.w);
#		else
	psout.Masks = float4(0, 0, masksZ, psout.Diffuse.w);
#		endif

	// Stored as 1 - vertexAO so the cleared default (0) means no occlusion
	// for pixels that do not write to this RT (sky, water, grass, effects).
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 0);

	float stochasticBlend = (screenNoise * screenNoise) < psout.Diffuse.w ? 1.0 : 0.0;
	psout.NormalGlossiness.w = stochasticBlend;
#	endif

#	if !defined(HDR_OUTPUT)  // Do not apply gamma correction before we pass to ISHDR.
	if ((!inWorld && !inReflection) && SharedData::linearLightingSettings.enableLinearLighting && !(Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow)) {
		psout.Diffuse.xyz = Color::LinearToSrgb(psout.Diffuse.xyz);
	}
#	endif

	return psout;
}
#endif  // PSHADER
