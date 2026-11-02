#include "Common/CharacterCategoryMask.hlsli"
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/ReverseZ.hlsli"

#if defined(VSHADER)
struct VS_INPUT
{
	float4 Position: POSITION0;

#if (defined(TEX)) || (defined(HORIZFADE))
	float2 TexCoord: TEXCOORD0;
#endif

	float4 Color: COLOR0;
};
#endif

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;

#if (defined(DITHER)) && (defined(TEX))
	float4 TexCoord0: TEXCOORD0;
#elif defined(DITHER)
	float2 TexCoord0: TEXCOORD3;
#elif (defined(TEX)) || (defined(HORIZFADE))
	float2 TexCoord0: TEXCOORD0;
#endif

#if defined(TEXLERP)
	float2 TexCoord1: TEXCOORD1;
#endif

#if defined(HORIZFADE)
	float TexCoord2: TEXCOORD2;
#endif

#if (defined(TEX)) || ((defined(DITHER)) || (defined(HORIZFADE)))
	float4 Color: COLOR0;
#endif

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
};

#if defined(VSHADER)
cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 World : packoffset(c4);
	row_major float4x4 PreviousWorld : packoffset(c8);
	float3 EyePosition : packoffset(c12);
	float VParams : packoffset(c12.w);
	float4 BlendColor[3] : packoffset(c13);
	float2 TexCoordOff : packoffset(c16);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;


	float4 inputPosition = float4(input.Position.xyz, 1.0);

#if defined(OCCLUSION)

	// Intentionally left blank

#elif defined(MOONMASK)

	vsout.TexCoord0 = input.TexCoord;
	vsout.Color = float4(VParams.xxx, 1.0);

#elif defined(HORIZFADE)

	float worldHeight = mul(World, inputPosition).z;
	float eyeHeightDelta = -EyePosition.z + worldHeight;

	vsout.TexCoord0.xy = input.TexCoord;
	vsout.TexCoord2.x = saturate((1.0 / 17.0) * eyeHeightDelta);
	vsout.Color.xyz = BlendColor[0].xyz * VParams;
	vsout.Color.w = BlendColor[0].w;

#else

#if defined(DITHER)

#if defined(TEX)
	vsout.TexCoord0.xyzw = input.TexCoord.xyxy * float4(1.0, 1.0, 501.0, 501.0);
#else
	float3 inputDirection = normalize(input.Position.xyz);
	inputDirection.y += inputDirection.z;

	vsout.TexCoord0.x = 501 * acos(inputDirection.x);
	vsout.TexCoord0.y = 501 * asin(inputDirection.y);
#endif

#elif defined(CLOUDS)
	vsout.TexCoord0.xy = TexCoordOff + input.TexCoord;
#else
	vsout.TexCoord0.xy = input.TexCoord;
#endif

#if defined(TEXLERP)
	vsout.TexCoord1.xy = TexCoordOff + input.TexCoord;
#endif

	float3 skyColor = BlendColor[0].xyz * input.Color.xxx + BlendColor[1].xyz * input.Color.yyy +
	                  BlendColor[2].xyz * input.Color.zzz;

	vsout.Color.xyz = VParams * skyColor;
	vsout.Color.w = BlendColor[0].w * input.Color.w;

#endif

	float4 skyClip = mul(WorldViewProj, inputPosition);
	vsout.Position = float4(skyClip.xy, FrameBuffer::FarPlaneClipZ(skyClip.w), skyClip.w);
	vsout.WorldPosition = mul(World, inputPosition);
	vsout.PreviousWorldPosition = mul(PreviousWorld, inputPosition);

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 Normal: SV_Target2;
#if defined(DEFERRED)
#if defined(CHARACTER_CATEGORY)
	float4 Masks2: SV_Target7;
#endif
#endif
#if (defined(CLOUD_SHADOWS)) && ((defined(CLOUDS)) && (!(defined(DEFERRED))))
	float4 CloudShadows: SV_Target3;
#endif
};

#if defined(PSHADER)
SamplerState SampBaseSampler : register(s0);
SamplerState SampBlendSampler : register(s1);
SamplerState SampNoiseGradSampler : register(s2);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexBlendSampler : register(t1);
Texture2D<float4> TexNoiseGradSampler : register(t2);

cbuffer PerGeometry : register(b2)
{
	float2 PParams : packoffset(c0);
};

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

#	include "Common/MotionBlur.hlsli"
#	include "Common/SharedData.hlsli"

#if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#endif

#if defined(HDR_OUTPUT)
#include "HDRDisplay/HDRSun.hlsli"
#include "Common/Random.hlsli"
#endif

Texture2D<float> TexDepthSampler : register(t17);

/// Compose authored inputs before LL adjustment; legacy textures are already adjusted.
float3 ComposeSkyColor(float3 skyColor, float3 textureColor, float3 skyOffset, bool composeAuthoredSky)
{
	if (composeAuthoredSky)
		return Color::Sky(skyColor * textureColor + skyOffset);
	return Color::Sky(skyColor) * textureColor + Color::Sky(skyOffset);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float3 skyOffset = PParams.yyy;
	const bool composeAuthoredSky = Color::UseLinearLightingColorAdjustments();

#if !(defined(OCCLUSION))
#if !(defined(TEXLERP))
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
#if defined(HORIZFADE)
	baseColor.xyz = AdaptiveBalanceAppearance::ApplyColor(baseColor.xyz, 1.0,
		SharedData::adaptiveBalanceSettings.appearance.starsCurve, 1.0, 1.0.xxx);
#endif
	if (!composeAuthoredSky)
		baseColor.xyz = Color::Sky(baseColor.xyz);
#if defined(TEXFADE)
	baseColor.w *= PParams.x;
#endif
#else
	float4 blendColor = TexBlendSampler.Sample(SampBlendSampler, input.TexCoord1.xy);
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	if (!composeAuthoredSky) {
		blendColor.xyz = Color::Sky(blendColor.xyz);
		baseColor.xyz = Color::Sky(baseColor.xyz);
	}
	baseColor = PParams.xxxx * (-baseColor + blendColor) + baseColor;
#endif

#if defined(HDR_OUTPUT)
	float hdrSunGain = HDRSun::GetHdrSunGain(input.TexCoord0.xy, baseColor);
	baseColor.xyz *= hdrSunGain;
	if (HDRSun::IsHdrSunActive()) {
		baseColor.xyz += (Random::InterleavedGradientNoise(input.Position.xy) - 0.5f) * (saturate(hdrSunGain - 1.0f) / 255.0f);
		skyOffset = 0.0;
#if defined(CLOUD_SHADOWS)
		float cloudMult = CloudShadows::GetCloudShadowMult(input.WorldPosition.xyz, SampBaseSampler);
		baseColor.xyz *= cloudMult;
		baseColor.w *= cloudMult;
#endif
	}
#endif

#if defined(CLOUDS)
	float skyBrightnessMultiplier = SharedData::adaptiveBalanceSettings.cloudBrightness;
#else
	float skyBrightnessMultiplier = SharedData::adaptiveBalanceSettings.skyBrightness;
#endif

#if defined(DITHER)
	float2 noiseGradPosition = input.Position.xy;
	float2 noiseGradUv = float2(0.125, 0.125) * noiseGradPosition;
	// The 8x8 engine dither texture stores 0..63, so center it without shifting the weather color.
	const float noiseGradCenter = 31.5 / 255.0;
	float noiseGrad =
		(TexNoiseGradSampler.Sample(SampNoiseGradSampler, noiseGradUv).x - noiseGradCenter) * 0.03125;

#if defined(TEX)
	float3 skyVertColor = composeAuthoredSky ? (input.Color.xyz + noiseGrad) : input.Color.xyz;
	float3 sunGlareColor = ComposeSkyColor(skyVertColor, baseColor.xyz, skyOffset, composeAuthoredSky);
	// Dither/noise term is the legacy sky path contribution for gradient smoothing.
	psout.Color.xyz = (sunGlareColor * skyBrightnessMultiplier) + (composeAuthoredSky ? 0.0 : noiseGrad);
	psout.Color.w = baseColor.w * input.Color.w;
#else
	float3 skyGradientColor = ComposeSkyColor(input.Color.xyz, 1.0, skyOffset, composeAuthoredSky);
	// Additive noise remains effective near black; brighter gradients use relative noise.
	float3 additiveDither = max(skyGradientColor + noiseGrad, 0.0);
	float3 multiplicativeDither = skyGradientColor * (1.0 + noiseGrad * 10.0);
	float ditherBlend = saturate(dot(skyGradientColor, 8.0));
	psout.Color.xyz = lerp(additiveDither, multiplicativeDither, ditherBlend) * skyBrightnessMultiplier;
	psout.Color.w = input.Color.w;
#endif

#elif defined(MOONMASK)
	psout.Color.xyzw = baseColor;
	if (composeAuthoredSky)
		psout.Color.xyz = Color::Sky(psout.Color.xyz);

	if (baseColor.w - AlphaTestRefRS.x < 0) {
		discard;
	}

#elif defined(HORIZFADE)
	if (composeAuthoredSky)
		psout.Color.xyz = Color::Sky(1.5 * (input.Color.xyz * baseColor.xyz + skyOffset)) * skyBrightnessMultiplier;
	else
		psout.Color.xyz = 1.5 * (ComposeSkyColor(input.Color.xyz, baseColor.xyz, skyOffset, false) * skyBrightnessMultiplier);
	psout.Color.w = input.TexCoord2.x * (baseColor.w * input.Color.w);
#else
	psout.Color.w = input.Color.w * baseColor.w;
	psout.Color.xyz = ComposeSkyColor(input.Color.xyz, baseColor.xyz, skyOffset, composeAuthoredSky) * skyBrightnessMultiplier;
#endif

#else
	psout.Color = float4(0, 0, 0, 1.0);
#endif

#if (defined(DITHER)) && ((defined(TEX)) && (!(defined(OCCLUSION))))
	psout.Color.xyz *= SharedData::adaptiveBalanceSettings.sunGlareIntensity;
#endif

#if (!(defined(OCCLUSION))) && (!(defined(MOONMASK)))
#if defined(CLOUDS)
	float saturation = SharedData::adaptiveBalanceSettings.cloudSaturation;
#else
	float saturation = SharedData::adaptiveBalanceSettings.skySaturation;
#endif
	if (saturation != 1.0)
		psout.Color.xyz = Color::Saturation(psout.Color.xyz, saturation);
#endif

#if !(defined(OCCLUSION))
#if defined(CLOUDS)
	psout.Color.xyz = AdaptiveBalanceAppearance::ApplyColor(psout.Color.xyz, 1.0, 1.0, 1.0,
		SharedData::adaptiveBalanceSettings.appearance.cloudTint);
	const float cloudOpacity = SharedData::adaptiveBalanceSettings.appearance.cloudOpacity;
	if (Color::IsSceneColorDraw() && cloudOpacity != 1.0)
		psout.Color.w = saturate(psout.Color.w * cloudOpacity);
#elif defined(HORIZFADE)
	psout.Color.xyz = AdaptiveBalanceAppearance::ApplyColor(psout.Color.xyz,
		SharedData::adaptiveBalanceSettings.appearance.starsIntensity, 1.0, 1.0, 1.0.xxx);
#endif
#endif

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	psout.MotionVectors = float4(screenMotionVector, 0, psout.Color.w);
	psout.Normal = float4(0.5, 0.5, 0, psout.Color.w);

#if defined(DEFERRED)
#if defined(CHARACTER_CATEGORY)
	psout.Masks2 = CharacterCategoryMask::Encode(
		0.0, 0u, psout.Color.w);
#endif
#endif

#if (defined(CLOUD_SHADOWS)) && ((defined(CLOUDS)) && (!(defined(DEFERRED))))
	psout.CloudShadows = float4(1, 1, 1, psout.Color.w);

	float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
	if (depth < input.Position.z)
		psout.Color.w = 0;

#endif


	if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun) != 0) {
		float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
		if (depth < input.Position.z)
			psout.Color.w = 0;
	}

	return psout;
}
#endif
