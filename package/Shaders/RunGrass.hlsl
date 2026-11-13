#include "Common/CharacterCategoryMask.hlsli"
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#if defined(PSHADER)
#	include "Common/GrassTexture.hlsli"
#	if defined(PBR_GRASS) && defined(GRASS_LIGHTING)
#		define TRUE_PBR
#	endif
#	include "Common/LightingCommon.hlsli"
#endif

#define DEFERRED

#ifdef GRASS_LIGHTING
#	define GRASS
#endif  // GRASS_LIGHTING

#if !defined(DYNAMIC_CUBEMAPS) && defined(IBL)
#	undef IBL
#endif

#if defined(VSHADER)
struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
	float4 Normal: NORMAL0;
	float4 Color: COLOR0;
	float4 InstanceData1: TEXCOORD4;
	float4 InstanceData2: TEXCOORD5;
	float4 InstanceData3: TEXCOORD6;
	float4 InstanceData4: TEXCOORD7;
#	if defined(VR) || defined(GRASS_OPTIMIZATIONS)
	uint InstanceID: SV_INSTANCEID;
#	endif  // VR
};
#endif

#ifdef GRASS_LIGHTING
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float4 Color: COLOR0;
	float VertexMult: COLOR1;
#	ifdef GRASS_OPTIMIZATIONS
	nointerpolation float TextureNormalLength: COLOR2;
#	endif
	float3 TexCoord: TEXCOORD0;
	float3 ViewSpacePosition:
#	if !defined(VR)
		TEXCOORD1;
#	else
		TEXCOORD2;
#	endif
#	if defined(RENDER_DEPTH)
	float2 Depth:
#		if !defined(VR)
		TEXCOORD2;
#		else
		TEXCOORD3;
#		endif
#	endif  // RENDER_DEPTH
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float4 VertexNormal: POSITION4;
#	ifdef VR
	float ClipDistance: SV_ClipDistance0;
	float CullDistance: SV_CullDistance0;
#	endif  // VR
};
#else
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float4 Color: COLOR0;
	float VertexMult: COLOR1;
	float3 TexCoord: TEXCOORD0;
	float4 AmbientColor: TEXCOORD1;
#	ifdef GRASS_OPTIMIZATIONS
	nointerpolation float TextureNormalLength: COLOR2;
#	endif
	float3 ViewSpacePosition: TEXCOORD2;
#	if defined(RENDER_DEPTH)
	float2 Depth: TEXCOORD3;
#	endif  // RENDER_DEPTH
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
#	ifdef VR
	float ClipDistance: SV_ClipDistance0;
	float CullDistance: SV_CullDistance0;
#	endif  // VR
};
#endif

// Constant Buffers (Flat and VR)
cbuffer PerGeometry : register(
#ifdef VSHADER
						  b2
#else
						  b3
#endif
					  )
{
#if !defined(VR)
	row_major float4x4 WorldViewProj[1] : packoffset(c0);
	row_major float4x4 WorldView[1] : packoffset(c4);
	row_major float4x4 World[1] : packoffset(c8);
	row_major float4x4 PreviousWorld[1] : packoffset(c12);
	float4 FogNearColor : packoffset(c16);
	float3 WindVector : packoffset(c17);
	float WindTimer : packoffset(c17.w);
	float3 DirLightDirection : packoffset(c18);
	float PreviousWindTimer : packoffset(c18.w);
	float3 DirLightColor : packoffset(c19);
	float AlphaParam1 : packoffset(c19.w);
	float3 AmbientColor : packoffset(c20);
	float AlphaParam2 : packoffset(c20.w);
	float3 ScaleMask : packoffset(c21);
	float ShadowClampValue : packoffset(c21.w);
#else
	row_major float4x4 WorldViewProj[2] : packoffset(c0);
	row_major float4x4 WorldView[2] : packoffset(c8);
	row_major float4x4 World[2] : packoffset(c16);
	row_major float4x4 PreviousWorld[2] : packoffset(c24);
	float4 FogNearColor : packoffset(c32);
	float3 WindVector : packoffset(c33);
	float WindTimer : packoffset(c33.w);
	float3 DirLightDirection : packoffset(c34);
	float PreviousWindTimer : packoffset(c34.w);
	float3 DirLightColor : packoffset(c35);
	float AlphaParam1 : packoffset(c35.w);
	float3 AmbientColor : packoffset(c36);
	float AlphaParam2 : packoffset(c36.w);
	float3 ScaleMask : packoffset(c37);
	float ShadowClampValue : packoffset(c37.w);
#endif  // !VR
}

#ifdef VSHADER

#	ifdef GRASS_COLLISION
#		include "GrassCollision\\GrassCollision.hlsli"
#	endif  // GRASS_COLLISION

cbuffer cb7 : register(b7)
{
	float4 cb7[1];
}

cbuffer cb8 : register(b8)
{
	float4 cb8[240];
}

#	ifdef GRASS_OPTIMIZATIONS
#		include "GrassOptimizations/GrassBatch.hlsli"
#	endif

float GetPerInstanceFade(VS_INPUT input)
{
#	ifdef GRASS_OPTIMIZATIONS
	if (GrassBatchEnabled != 0)
		return GetGrassBatchFade(input.InstanceID);
#	endif
	return dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);
}

// Calculate wind displacement for a grass vertex
float3 CalculateWindDisplacement(VS_INPUT input, float windTimer)
{
	float windAngle = 0.4 * ((input.InstanceData1.x + input.InstanceData1.y) * -0.0078125 + windTimer);
	float windAngleSin, windAngleCos;
	sincos(windAngle, windAngleSin, windAngleCos);

	float windTmp3 = 0.2 * cos(Math::PI * windAngleCos);
	float windTmp1 = sin(Math::PI * windAngleSin);
	float windTmp2 = sin(Math::TAU * windAngleSin);
	float windPower = WindVector.z * (((windTmp1 + windTmp2) * 0.3 + windTmp3) *
										 (0.5 * (input.Color.w * input.Color.w)));

	return float3(WindVector.xy, 0) * windPower;
}

float3 GetWindDisplacement(VS_INPUT input, bool previous)
{
	float3 displacement = 0;
#	ifdef GRASS_OPTIMIZATIONS
	[branch] if (GrassBatchEnabled)
	{
		float2 wind = GrassInstanceExtras[GrassBatchBase + input.InstanceID].wind;
		float power = WindVector.z * ((previous ? wind.y : wind.x) * (0.5 * (input.Color.w * input.Color.w)));
		displacement = float3(WindVector.xy, 0) * power;
	}
	else
#	endif
		displacement = CalculateWindDisplacement(input, previous ? PreviousWindTimer : WindTimer);
	return displacement;
}

float3x3 GetGrassInstanceRotation(VS_INPUT input)
{
	return float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));
}

#	ifdef GRASS_LIGHTING
float4 GetMSPosition(VS_INPUT input, float3x3 world3x3)
#	else
float4 GetMSPosition(VS_INPUT input)
#	endif
{
	float3 inputPosition = input.Position.xyz * (input.InstanceData4.yyy * ScaleMask.xyz + float3(1, 1, 1));

#	ifdef GRASS_LIGHTING
	float3 transformedPosition = mul(world3x3, inputPosition);
	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + transformedPosition;
#	else
	float3 instancePosition;
	instancePosition.z = dot(
		float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w), inputPosition);
	instancePosition.x = dot(input.InstanceData2.xyz, inputPosition);
	instancePosition.y = dot(input.InstanceData3.xyz, inputPosition);

	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + instancePosition;
#	endif
	msPosition.w = 1;

	return msPosition;
}

#	ifdef GRASS_LIGHTING
VS_OUTPUT main(VS_INPUT input)
{
#		ifdef GRASS_OPTIMIZATIONS
	LoadGrassBatchInstance(input.InstanceData1, input.InstanceData2, input.InstanceData3, input.InstanceData4);
#		endif
	VS_OUTPUT vsout;

	uint eyeIndex = Stereo::GetEyeIndexVS(
#		if defined(VR)
		input.InstanceID
#		endif  // VR
	);
#		ifdef GRASS_OPTIMIZATIONS
	if (GrassBatchEnabled)
		eyeIndex = Stereo::GetEyeIndexVS(GrassBatchEye);
#		endif
	float3x3 world3x3 = GetGrassInstanceRotation(input);

	float4 msPosition = GetMSPosition(input, world3x3);

	float3 windDisplacement = GetWindDisplacement(input, false);
	float3 previousWindDisplacement = GetWindDisplacement(input, true);
#		ifdef GRASS_OPTIMIZATIONS
	float3 batchOffset = GetGrassBatchOffset(input.InstanceID);
	msPosition.xyz += batchOffset;
#		endif

	float4 previousMsPosition = msPosition;
#		ifndef GRASS_COLLISION_BASIC
	msPosition.xyz += windDisplacement;
	previousMsPosition.xyz += previousWindDisplacement;
#		endif
#		ifdef GRASS_COLLISION
	float3 collisionAxis;
	float collisionAngle, compressionScale;
	VS_INPUT collisionInput = input;
	float collisionDistance = GrassCollision::FADE_DISTANCE;
#			ifdef GRASS_OPTIMIZATIONS
	collisionInput.InstanceData1.xyz += batchOffset;
	if (GrassBatchEnabled)
		collisionDistance = GrassBatchCollisionDistance;
#			endif
	GrassCollision::ApplyDeformation(collisionInput, msPosition.xyz, previousMsPosition.xyz,
		collisionDistance, collisionAxis, collisionAngle, compressionScale);
#			ifdef GRASS_COLLISION_BASIC
	msPosition.xyz += windDisplacement;
	previousMsPosition.xyz += previousWindDisplacement;
#			endif
#		endif

	float4 projSpacePosition = mul(WorldViewProj[eyeIndex], msPosition);
#		if !defined(VR)
	vsout.HPosition = projSpacePosition;
#		endif  // !VR

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float perInstanceFade = GetPerInstanceFade(input);

#		if defined(VR)
	float distanceFade = 1 - saturate((length(mul(World[0], msPosition).xyz) - AlphaParam1) / AlphaParam2);
#		else
	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);
#		endif

	// Note: input.Color.w is used for wind speed
	vsout.Color.xyz = input.Color.xyz;
	vsout.Color.w = distanceFade * perInstanceFade;
	vsout.VertexMult = input.InstanceData1.w;
#		ifdef GRASS_OPTIMIZATIONS
	vsout.TextureNormalLength = GetGrassBatchTextureNormalLength();
	if (GrassBatchEnabled) {
		vsout.Color.w = perInstanceFade;
		vsout.VertexMult = GetGrassBatchShading(input.InstanceID);
	}
#		endif

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.ViewSpacePosition = mul(WorldView[eyeIndex], msPosition).xyz;
	vsout.WorldPosition = mul(World[eyeIndex], msPosition);

	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], previousMsPosition);
#		if defined(VR)
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(projSpacePosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#		endif  // !VR

	// FeatureData is bound to the pixel shader only. The runtime toggle is
	// therefore evaluated there; keep this varying initialized for the enhanced
	// path and let the basic pixel path ignore it.
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
#		ifdef GRASS_COLLISION
	vsout.VertexNormal.xyz = GrassCollision::DeformNormal(vsout.VertexNormal.xyz, collisionAxis, collisionAngle, compressionScale);
#		endif
	vsout.VertexNormal.w = input.Color.w;

	return vsout;
}
#	else
VS_OUTPUT main(VS_INPUT input)
{
#		ifdef GRASS_OPTIMIZATIONS
	LoadGrassBatchInstance(input.InstanceData1, input.InstanceData2, input.InstanceData3, input.InstanceData4);
#		endif
	VS_OUTPUT vsout;

	uint eyeIndex = Stereo::GetEyeIndexVS(
#		if defined(VR)
		input.InstanceID
#		endif  // VR
	);
#		ifdef GRASS_OPTIMIZATIONS
	if (GrassBatchEnabled)
		eyeIndex = Stereo::GetEyeIndexVS(GrassBatchEye);
#		endif

	float4 msPosition = GetMSPosition(input);

	float3 windDisplacement = GetWindDisplacement(input, false);
	float3 previousWindDisplacement = GetWindDisplacement(input, true);
#		ifdef GRASS_OPTIMIZATIONS
	float3 batchOffset = GetGrassBatchOffset(input.InstanceID);
	msPosition.xyz += batchOffset;
#		endif

	float4 previousMsPosition = msPosition;
#		ifndef GRASS_COLLISION_BASIC
	msPosition.xyz += windDisplacement;
	previousMsPosition.xyz += previousWindDisplacement;
#		endif
#		ifdef GRASS_COLLISION
	float3 collisionAxis;
	float collisionAngle, compressionScale;
	VS_INPUT collisionInput = input;
	float collisionDistance = GrassCollision::FADE_DISTANCE;
#			ifdef GRASS_OPTIMIZATIONS
	collisionInput.InstanceData1.xyz += batchOffset;
	if (GrassBatchEnabled)
		collisionDistance = GrassBatchCollisionDistance;
#			endif
	GrassCollision::ApplyDeformation(collisionInput, msPosition.xyz, previousMsPosition.xyz,
		collisionDistance, collisionAxis, collisionAngle, compressionScale);
#			ifdef GRASS_COLLISION_BASIC
	msPosition.xyz += windDisplacement;
	previousMsPosition.xyz += previousWindDisplacement;
#			endif
#		endif

	float4 projSpacePosition = mul(WorldViewProj[eyeIndex], msPosition);
#		if !defined(VR)
	vsout.HPosition = projSpacePosition;
#		endif  // !VR	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	float dirLightAngle = dot(DirLightDirection.xyz, instanceNormal);
	float3 diffuseMultiplier = input.InstanceData1.www * input.Color.xyz;

	float perInstanceFade = GetPerInstanceFade(input);

#		if defined(VR)
	float distanceFade = 1 - saturate((length(mul(World[0], msPosition).xyz) - AlphaParam1) / AlphaParam2);
#		else
	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);
#		endif

	vsout.Color.xyz = input.Color.xyz;
	vsout.Color.w = distanceFade * perInstanceFade;
	vsout.VertexMult = input.InstanceData1.w;
#		ifdef GRASS_OPTIMIZATIONS
	vsout.TextureNormalLength = GetGrassBatchTextureNormalLength();
	if (GrassBatchEnabled) {
		vsout.Color.w = perInstanceFade;
		vsout.VertexMult = GetGrassBatchShading(input.InstanceID);
	}
#		endif

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.AmbientColor.xyz = input.InstanceData1.www * (AmbientColor.xyz * input.Color.xyz);
	vsout.AmbientColor.w = ShadowClampValue;
#		ifdef GRASS_OPTIMIZATIONS
	// Basic shading leaves this varying unused; far billboards carry authored normals here.
	if (GrassBatchEnabled & 2u) {
		vsout.AmbientColor.xyz = mul(GetGrassInstanceRotation(input), input.Normal.xyz * 2.0 - 1.0);
#			ifdef GRASS_COLLISION
		vsout.AmbientColor.xyz = GrassCollision::DeformNormal(vsout.AmbientColor.xyz, collisionAxis, collisionAngle, compressionScale);
#			endif
	}
#		endif

	vsout.ViewSpacePosition = mul(WorldView[eyeIndex], msPosition).xyz;
	vsout.WorldPosition = mul(World[eyeIndex], msPosition);

#		if defined(VR)
	Stereo::VR_OUTPUT VRout = Stereo::GetVRVSOutput(projSpacePosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#		endif  // !VR

	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], previousMsPosition);

	return vsout;
}

#	endif

#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#ifdef GRASS_LIGHTING
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS: SV_Target0;
#	else
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
#		if defined(PBR_GRASS)
	float4 Reflectance: SV_Target5;
#		endif
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif  // RENDER_DEPTH
};
#else
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS: SV_Target0;
#	else
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 Normal: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif
};
#endif

#ifdef PSHADER
SamplerState SampBaseSampler : register(s0);
SamplerState SampShadowMaskSampler : register(s1);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexShadowMaskSampler : register(t1);

cbuffer PerFrame : register(b0)
{
	float4 cb0_1[2] : packoffset(c0);
	float4 VPOSOffset : packoffset(c2);
	float4 cb0_2[7] : packoffset(c3);
}

#	if !defined(VR)
cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif  // !VR

#	if defined(SCREEN_SPACE_SHADOWS)
#		include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#	endif

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	if defined(ISL) && defined(LIGHT_LIMIT_FIX)
#		include "InverseSquareLighting/InverseSquareLighting.hlsli"
#	endif

#	define SampColorSampler SampBaseSampler

#	if defined(SKYLIGHTING)
#		define SKYLIGHTING_SHADOW_VIS
#	endif

#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(TERRAIN_SHADOWS)
#		include "TerrainShadows/TerrainShadows.hlsli"
#	endif

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

#	if defined(SKYLIGHTING)
#		include "Skylighting/Skylighting.hlsli"
#	endif

#	if defined(WATER_LIGHTING)
#		include "WaterLighting/WaterCaustics.hlsli"
#	endif

#	if defined(IBL)
#		include "IBL/IBL.hlsli"
#	endif

#	if defined(SNOW_COVER)
#		define BASIC_SNOW_COVER
#		define SampColorSampler SampBaseSampler
#		include "SnowCover/SnowCover.hlsli"
#	endif

#	define LinearSampler SampBaseSampler

#	include "Common/ShadowSampling.hlsli"

float3 ApplyGrassWetDarkening(float3 baseColor)
{
	const float wetDarkening = saturate(SharedData::wetternessSettings.GrassWetnessPhase) *
	                           saturate(SharedData::wetternessSettings.GrassWetDarkeningStrength);
	return baseColor * (1.0 - wetDarkening);
}

#	ifdef GRASS_OPTIMIZATIONS
#		define GRASS_DETAILED(condition) (!(input.VertexMult < 0) && (condition))
#		define GRASS_BILLBOARD (input.VertexMult < -1.5)
#	else
#		define GRASS_DETAILED(condition) condition
#		define GRASS_BILLBOARD false
#	endif

float4 SampleGrassLightingBaseColor(PS_INPUT input, out bool complex, bool fullTexture = false)
{
	float normalLength = 0;
#	ifdef GRASS_OPTIMIZATIONS
	normalLength = input.TextureNormalLength;
#	endif
	[branch] if (!(normalLength > 0))
		normalLength = GetGrassTextureNormalLength(TexBaseSampler);
	complex = abs(normalLength - 1.0) < SharedData::grassLightingSettings.ComplexGrassThreshold;
#	if defined(PBR_GRASS)
	fullTexture = fullTexture || (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::PBRGrass);
#	endif
	complex = complex && !fullTexture;
	float4 baseColor;
	if (complex) {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, input.TexCoord.y * 0.5), SharedData::MipBias);
	} else {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	}
	return baseColor;
}

// Basic and far-billboard shading share alpha, shadows and deferred outputs.
PS_OUTPUT RenderBasicGrass(PS_INPUT input, bool frontFace)
{
	PS_OUTPUT psout = (PS_OUTPUT)0;

	const bool billboard = GRASS_BILLBOARD;
	const bool pbrTexture = billboard && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::PBRGrass);
	bool complex = false;
	float4 baseColor;
	if (billboard) {
		baseColor = SampleGrassLightingBaseColor(input, complex, pbrTexture);
		if (pbrTexture)
			baseColor.xyz = Color::PBRDiffuse(baseColor.xyz);
		else
			baseColor.xyz = Color::Diffuse(baseColor.xyz);
		if (SharedData::grassLightingSettings.Enabled && ((!complex && !pbrTexture) || SharedData::grassLightingSettings.OverrideComplexGrassSettings))
			baseColor.xyz *= SharedData::grassLightingSettings.BasicGrassBrightness;
	} else {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	}

#	if defined(RENDER_DEPTH) || defined(DO_ALPHA_TEST)
	float diffuseAlpha = input.Color.w * baseColor.w;

	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#	endif

#	if defined(RENDER_DEPTH)
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#	else
	if (SharedData::ShouldDisableTerrainVertexColors())
		input.Color.xyz = 1;

	uint eyeIndex = Stereo::GetEyeIndexPS(input.HPosition, VPOSOffset);

	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
	float screenNoise = Random::InterleavedGradientNoise(Stereo::EyeStableNoiseCoord(input.HPosition.xy, SharedData::BufferDim.xy), SharedData::FrameCount);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float dirShadow = ShadowSampling::HasDirectionalShadows() ? shadowColor.x : 1.0;
	float dirDetailShadow = 1.0;

	if (dirShadow > 0.0 && ShadowSampling::HasDirectionalShadows())
		dirShadow *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);

	if (GRASS_DETAILED(dirShadow > 0.0 && ShadowSampling::HasDirectionalShadows())) {
#		if defined(SCREEN_SPACE_SHADOWS)
		dirDetailShadow = ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise, eyeIndex);
#		endif  // SCREEN_SPACE_SHADOWS

#		if defined(WATER_LIGHTING)
		if (dirShadow > 0.0) {
			float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
			dirShadow *= WaterLighting::ComputeCaustics(waterData, input.WorldPosition.xyz, eyeIndex);
		}
#		endif
	}

	float llDirLightMult = (Color::UseLinearLightingColorAdjustments() && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 directionalLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * dirShadow * dirDetailShadow * llDirLightMult;
	float3 diffuseColor = directionalLightColor;

	float3 normal = -normalize(cross(ddx_coarse(input.WorldPosition.xyz), ddy_coarse(input.WorldPosition.xyz)));
	if (billboard) {
		// Authored upward tilt keeps vertical cards lit by overhead sunlight.
#		ifdef GRASS_LIGHTING
		float3 billboardNormal = normalize(input.VertexNormal.xyz);
#		else
		float3 billboardNormal = normalize(input.AmbientColor.xyz);
#		endif
		diffuseColor *= abs(dot(billboardNormal, SharedData::DirLightDirection.xyz)) * Color::VanillaNormalization();
	}
	// An upward ambient normal keeps billboard ambient light independent of the viewed face.
	float3 ambientNormal = billboard ? float3(0, 0, 1) : normal;
	float3 viewDirection = -input.WorldPosition.xyz * rsqrt(max(dot(input.WorldPosition.xyz, input.WorldPosition.xyz), 1e-8f));
	float3 foliageNormal = normal;
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::GrassSphereNormal) && dot(foliageNormal, viewDirection) < 0.0)
		foliageNormal = -foliageNormal;
	[branch] if (!billboard && SharedData::foliageLightingSettings.EnableGrassScattering != 0)
		diffuseColor += directionalLightColor * GetFoliageTransmission(dot(foliageNormal, SharedData::DirLightDirection.xyz), dot(viewDirection, SharedData::DirLightDirection.xyz)) * Color::VanillaNormalization();

#		if defined(LIGHT_LIMIT_FIX)
	uint clusterIndex = 0;
	uint lightCount = 0;

	if (GRASS_DETAILED(LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex))) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
				LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];

				float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
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
				float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear, light.lightFlags) * intensityMultiplier * light.fade;

				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				lightColor *= lightShadow;
				float3 normalizedLightDirection = lightDirection / max(lightDist, 1e-5f);

				diffuseColor += lightColor;
				[branch] if (SharedData::foliageLightingSettings.EnableGrassScattering != 0)
					diffuseColor += lightColor * GetFoliageTransmission(dot(foliageNormal, normalizedLightDirection), dot(viewDirection, normalizedLightDirection)) * Color::VanillaNormalization();
			}
		}
	}
#		endif  // LIGHT_LIMIT_FIX

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);

#		if defined(SKYLIGHTING)
#			if defined(VR)
	float3 positionMSSkylight = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#			else
	float3 positionMSSkylight = input.WorldPosition.xyz;
#			endif
	float skylightingDiffuse = Skylighting::GetVertexSkylightingDiffuse(positionMSSkylight, ambientNormal, vertexAO);
#		endif  // SKYLIGHTING

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(ambientNormal)));

#		if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
#			if defined(SKYLIGHTING)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -ambientNormal, skylightingDiffuse);
#			else
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -ambientNormal);
#			endif
	}
#		endif

	directionalAmbientColor = Color::ApplyAmbientBalance(directionalAmbientColor);
	diffuseColor += directionalAmbientColor;

	float3 albedo = ApplyGrassWetDarkening(baseColor.xyz) * vertexColor;
#		if defined(SNOW_COVER)
	[branch] if (SnowCover::IsActive())
	{
#			if defined(SKYLIGHTING)
		float snowSkyVisibility = skylightingDiffuse;
#			else
		float snowSkyVisibility = 1.0;
#			endif
		SnowCover::ApplyGrassSnow(albedo, normal, input.WorldPosition.xyz, input.TexCoord.xy, viewPosition.xyz, snowSkyVisibility, complex, eyeIndex, TexBaseSampler, pbrTexture, billboard);
	}
#		endif

	diffuseColor *= albedo;
	directionalAmbientColor *= albedo;

#		if defined(SKYLIGHTING)
#			if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#			endif
	{
		Skylighting::ApplySkylighting(diffuseColor, directionalAmbientColor, albedo, skylightingDiffuse);
	}
#		endif

	psout.Diffuse = float4(diffuseColor, 1);
	psout.MotionVectors = float4(MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex), 0, 1);
#		if defined(GRASS_LIGHTING)
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(FrameBuffer::WorldToView(normal, false, eyeIndex)), 0, 0);
#		else
	psout.Normal.xy = GBuffer::EncodeNormal(FrameBuffer::WorldToView(normal, false, eyeIndex));
	psout.Normal.zw = 0;
#		endif
	psout.Albedo = float4(albedo, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
#		if defined(CHARACTER_CATEGORY)
	psout.Masks2 = CharacterCategoryMask::Encode(
		1.0 - vertexAO, 0, psout.Diffuse.w);
#		else
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, psout.Diffuse.w);
#		endif
#	endif

	return psout;
}

#	ifdef GRASS_LIGHTING
#		include "GrassLighting/GrassLighting.hlsli"
#		if defined(PBR_GRASS)
#			include "Common/GrassPBR.hlsli"
#		endif

float GetSoftLightMultiplier(float angle, float rolloff)
{
	float softLight = saturate((rolloff + angle) / (1 + rolloff));
	float arg1 = (softLight * softLight) * (3 - 2 * softLight);
	float clampedAngle = saturate(angle);
	float arg2 = (clampedAngle * clampedAngle) * (3 - 2 * clampedAngle);
	return saturate(arg1 - arg2);
}

float GetWrappedDiffuseMultiplier(float angle, float wrapAmount, bool useWrappedLighting)
{
	if (useWrappedLighting)
		return saturate(angle + wrapAmount) / (1.0 + wrapAmount);

	return saturate(angle);
}

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
#		if defined(GRASS_OPTIMIZATIONS)
	[branch] if (GRASS_BILLBOARD) return RenderBasicGrass(input, frontFace);
#		endif
	[branch] if (!SharedData::grassLightingSettings.Enabled) return RenderBasicGrass(input, frontFace);
#		if defined(PBR_GRASS)
	[branch] if (SharedData::truePBRSettings.Enabled &&
				 (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::PBRGrassShading)) return RenderPBRGrass(input, frontFace);
#		endif

	PS_OUTPUT psout = (PS_OUTPUT)0;

	bool complex;
	float4 baseColor = SampleGrassLightingBaseColor(input, complex);

	baseColor.xyz = Color::Diffuse(baseColor.xyz);

#		if defined(RENDER_DEPTH) || defined(DO_ALPHA_TEST)
	float diffuseAlpha = input.Color.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else
	if (SharedData::ShouldDisableTerrainVertexColors())
		input.Color.xyz = 1;

	float4 specColor = 1;
	[branch] if (GRASS_DETAILED(complex))
		specColor = TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, 0.5 + input.TexCoord.y * 0.5), SharedData::MipBias);

	uint eyeIndex = Stereo::GetEyeIndexPS(input.HPosition, VPOSOffset);
	psout.MotionVectors = float4(MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex), 0, 1);

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float3 normal = normalize(input.VertexNormal.xyz);

	float3 viewPosition = mul(FrameBuffer::CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition, true, eyeIndex);
	float screenNoise = Random::InterleavedGradientNoise(Stereo::EyeStableNoiseCoord(input.HPosition.xy, SharedData::BufferDim.xy), SharedData::FrameCount);

	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::GrassSphereNormal) && !frontFace)
		normal = -normal;

	float3x3 tbn = 0;

	if (GRASS_DETAILED(complex)) {
		float3 normalColor = GrassLighting::TransformNormal(specColor.xyz);
		// world-space -> tangent-space -> world-space.
		// This is because we don't have pre-computed tangents.
		tbn = GrassLighting::CalculateTBN(normal, -input.WorldPosition.xyz, input.TexCoord.xy);
		normal = normalize(mul(normalColor, tbn));
	}

	if (!complex || SharedData::grassLightingSettings.OverrideComplexGrassSettings)
		baseColor.xyz *= SharedData::grassLightingSettings.BasicGrassBrightness;

	baseColor.xyz = ApplyGrassWetDarkening(baseColor.xyz);

	float llDirLightMult = (Color::UseLinearLightingColorAdjustments() && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;
	float3 dirLightColorMultiplier = 1;

	float dirLightAngle = dot(normal, SharedData::DirLightDirection.xyz);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float dirShadow = ShadowSampling::HasDirectionalShadows() ? shadowColor.x : 1.0;
	float dirDetailShadow = 1.0;

	if (dirShadow > 0.0 && ShadowSampling::HasDirectionalShadows())
		dirShadow *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);

	if (GRASS_DETAILED(dirShadow > 0.0 && ShadowSampling::HasDirectionalShadows())) {
#			if defined(SCREEN_SPACE_SHADOWS)
		if (dirLightAngle >= 0.0 || SharedData::foliageLightingSettings.EnableGrassScattering != 0)
			dirDetailShadow = ScreenSpaceShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise, eyeIndex);
#			endif  // SCREEN_SPACE_SHADOWS

#			if defined(WATER_LIGHTING)
		if (dirShadow > 0.0) {
			float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
			dirShadow *= WaterLighting::ComputeCaustics(waterData, input.WorldPosition.xyz, eyeIndex);
		}
#			endif
	}

	float3 diffuseColor = 0;
	float3 specularColor = 0;

	float3 lightsDiffuseColor = 0;
	float3 lightsSpecularColor = 0;

	dirLightColor *= dirLightColorMultiplier;
	const float3 unshadowedDirLightColor = dirLightColor;
	dirLightColor *= dirShadow;
	dirLightColor *= dirDetailShadow;

	float softLightRolloff = saturate(input.VertexNormal.w * 10.0) * SharedData::grassLightingSettings.SubsurfaceScatteringAmount * 2.0;
	float vanillaGrassWrapAmount = saturate(input.VertexNormal.w * 10.0) * 0.5;
	bool useVanillaGrassWrappedLighting = !complex && SharedData::grassLightingSettings.EnableWrappedLighting;

	lightsDiffuseColor += dirLightColor * GetWrappedDiffuseMultiplier(dirLightAngle, vanillaGrassWrapAmount, useVanillaGrassWrappedLighting) * Color::VanillaNormalization();
	[branch] if (SharedData::foliageLightingSettings.EnableGrassScattering != 0)
		lightsDiffuseColor += dirLightColor * GetFoliageTransmission(dirLightAngle, dot(viewDirection, SharedData::DirLightDirection.xyz)) * Color::VanillaNormalization();

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);

#			if defined(SKYLIGHTING)
#				if defined(VR)
	float3 positionMSSkylight = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#				else
	float3 positionMSSkylight = input.WorldPosition.xyz;
#				endif
	Skylighting::ShadowedSample skylightingSample = Skylighting::SampleWithShadow(positionMSSkylight, normal);
	sh2 skylightingSH = skylightingSample.Probe;
	float skylightingShadowVisibility = skylightingSample.Visibility;
	float skylightingDiffuse = Skylighting::GetSkylightingDiffuse(skylightingSH, positionMSSkylight, normal, vertexAO);
#			endif  // SKYLIGHTING

	float3 albedo = baseColor.xyz * vertexColor;
#			if defined(SNOW_COVER)
	[branch] if (SnowCover::IsActive())
	{
#				if defined(SKYLIGHTING)
		float snowSkyVisibility = skylightingDiffuse;
#				else
		float snowSkyVisibility = 1.0;
#				endif
		SnowCover::ApplyGrassSnow(albedo, normal, input.WorldPosition.xyz, input.TexCoord.xy, viewPosition.xyz, snowSkyVisibility, complex, eyeIndex, TexBaseSampler);
	}
#			endif

	float dirSoftShadow = dirShadow * dirDetailShadow;
#			if defined(SKYLIGHTING_SHADOW_VIS)
	if (Skylighting::IsEnabled() && SharedData::skylightingSettings.ShadowDataAvailable != 0)
		dirSoftShadow = min(dirSoftShadow, skylightingShadowVisibility);
#			endif

	float3 subsurfaceColor = unshadowedDirLightColor * dirSoftShadow * GetSoftLightMultiplier(dirLightAngle, softLightRolloff) * Color::VanillaNormalization();

	if (GRASS_DETAILED(complex))
		lightsSpecularColor += GrassLighting::GetLightSpecularInput(SharedData::DirLightDirection.xyz, viewDirection, normal, dirLightColor, SharedData::grassLightingSettings.Glossiness) * Color::VanillaNormalization() * Color::MoonReflectionMultiplier();

#			if defined(LIGHT_LIMIT_FIX)
	uint clusterIndex = 0;
	uint lightCount = 0;

	if (GRASS_DETAILED(LightLimitFix::GetClusterIndex(screenUV, viewPosition.z, clusterIndex))) {
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
				LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];

				float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);

#				if defined(ISL)
				float intensityMultiplier = InverseSquareLighting::GetAttenuation(lightDist, light);
				if (intensityMultiplier < 1e-5)
					continue;
#				else
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#				endif

				const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
				float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear, light.lightFlags) * intensityMultiplier * light.fade;
				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				float3 normalizedLightDirection = normalize(lightDirection);

				lightColor *= lightShadow;

				float lightAngle = dot(normal, normalizedLightDirection);
				float3 lightDiffuseColor;

				lightDiffuseColor = lightColor * GetWrappedDiffuseMultiplier(lightAngle, vanillaGrassWrapAmount, useVanillaGrassWrappedLighting);
				[branch] if (SharedData::foliageLightingSettings.EnableGrassScattering != 0)
					lightDiffuseColor += lightColor * GetFoliageTransmission(lightAngle, dot(viewDirection, normalizedLightDirection));

				subsurfaceColor += lightColor * GetSoftLightMultiplier(lightAngle, softLightRolloff) * Color::VanillaNormalization();

				lightsDiffuseColor += lightDiffuseColor * Color::VanillaNormalization();

				if (complex)
					lightsSpecularColor += GrassLighting::GetLightSpecularInput(normalizedLightDirection, viewDirection, normal, lightColor, SharedData::grassLightingSettings.Glossiness) * Color::VanillaNormalization();
			}
		}
	}
#			endif  // LIGHT_LIMIT_FIX

	diffuseColor += lightsDiffuseColor;

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));

#			if defined(IBL)
	if (SharedData::iblSettings.EnableIBL) {
#				if defined(SKYLIGHTING)
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBLOccluded(directionalAmbientColor, -normal, skylightingDiffuse);
#				else
		directionalAmbientColor = ImageBasedLighting::GetDiffuseIBL(directionalAmbientColor, -normal);
#				endif
	}
#			endif

	directionalAmbientColor = Color::ApplyAmbientBalance(directionalAmbientColor);
	diffuseColor += directionalAmbientColor;
	diffuseColor *= albedo;
	diffuseColor += subsurfaceColor * albedo;

	directionalAmbientColor *= albedo;

#			if defined(SKYLIGHTING)
#				if defined(IBL)
	if (!SharedData::iblSettings.EnableIBL)
#				endif
	{
		Skylighting::ApplySkylighting(diffuseColor, directionalAmbientColor, albedo, skylightingDiffuse);
	}
#			endif

	specularColor += lightsSpecularColor;
	specularColor *= specColor.w * SharedData::grassLightingSettings.SpecularStrength;

#			if defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)
	if (SharedData::lightLimitFixSettings.EnableLightsVisualisation) {
		if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 0) {
			diffuseColor.xyz = Color::TurboColormap(0);
		} else if (SharedData::lightLimitFixSettings.LightsVisualisationMode == 1) {
			diffuseColor.xyz = Color::TurboColormap(0);
		} else {
			diffuseColor.xyz = Color::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
		}
	} else {
		psout.Diffuse = float4(diffuseColor, 1);
	}
#			else
	psout.Diffuse.xyz = diffuseColor;
#			endif

	float3 normalVS = normalize(FrameBuffer::WorldToView(normal, false, eyeIndex));
	psout.Albedo = float4(albedo, 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), specColor.w, 1);

	psout.Specular = float4(specularColor, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
#			if defined(CHARACTER_CATEGORY)
	psout.Masks2 = CharacterCategoryMask::Encode(
		1.0 - vertexAO, 0, psout.Diffuse.w);
#			else
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, psout.Diffuse.w);
#			endif
#		endif
	return psout;
}
#	else
PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	return RenderBasicGrass(input, frontFace);
}
#	endif

#endif  // PSHADER
