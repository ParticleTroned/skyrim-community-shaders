#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
#	if defined(DOWNSAMPLE)
SamplerState AdaptSampler : register(s1);
#	elif defined(BLEND)
SamplerState BlendSampler : register(s1);
#	endif
SamplerState AvgSampler : register(s2);

Texture2D<float4> ImageTex : register(t0);
#	if defined(DOWNSAMPLE)
Texture2D<float4> AdaptTex : register(t1);
#	elif defined(BLEND)
Texture2D<float4> BlendTex : register(t1);
#	endif
Texture2D<float4> AvgTex : register(t2);

cbuffer PerGeometry : register(b2)
{
	float4 Flags : packoffset(c0);
	float4 TimingData : packoffset(c1);
	float4 Param : packoffset(c2);
	float4 Cinematic : packoffset(c3);
	float4 Tint : packoffset(c4);
	float4 Fade : packoffset(c5);
	float4 BlurScale : packoffset(c6);
	float4 BlurOffsets[16] : packoffset(c7);
};

float3 GetTonemapFactorReinhard(float3 luminance)
{
	return (luminance * (luminance * Param.y + 1)) / (luminance + 1);
}

float3 GetTonemapFactorHejlBurgessDawson(float3 luminance)
{
	float3 tmp = max(0, luminance - 0.004);
	return Param.y *
	       pow(((tmp * 6.2 + 0.5) * tmp) / (tmp * (tmp * 6.2 + 1.7) + 0.06), Color::GammaCorrectionValue);
}

#	include "Common/DisplayMapping.hlsli"

#	if defined(BLEND) && defined(ADAPTIVE_BALANCE)
float2 ClampBloomSampleUV(float2 a_uv, uint a_eyeIndex, uint a_bloomWidth)
{
	const float halfTexelX = 0.5 / max(float(a_bloomWidth), 1.0);
#		ifdef VR
	const float eyeMinX = 0.5 * a_eyeIndex;
	const float eyeMaxX = eyeMinX + 0.5;
#		else
	const float eyeMinX = 0.0;
	const float eyeMaxX = 1.0;
#		endif
	a_uv = Stereo::ClampToEyeUV(a_uv, a_eyeIndex);
	a_uv.x = clamp(a_uv.x, eyeMinX + halfTexelX, eyeMaxX - halfTexelX);
	return a_uv;
}

float3 SampleVanillaBloomEnhanced(float2 a_uv, out float3 a_vanillaBloom)
{
	float3 center = ImageTex.Sample(ImageSampler, a_uv).xyz;
	a_vanillaBloom = center;
	float3 bloom = center;

	if (SharedData::bloomSettings.Enabled) {
		uint bloomWidth = 1;
		uint bloomHeight = 1;
		ImageTex.GetDimensions(bloomWidth, bloomHeight);
		// Normalized 5x5 separable Gaussian kernel. The outer samples are one
		// Halo Radius from the center, matching the corrected SE implementation.
		static const uint BLOOM_GAUSSIAN_KERNEL_SIZE = 5;
		static const uint BLOOM_GAUSSIAN_KERNEL_CENTER = 2;
		static const float BLOOM_GAUSSIAN_RADIUS_SCALE = 0.5;
		static const float BLOOM_GAUSSIAN_WEIGHTS[BLOOM_GAUSSIAN_KERNEL_SIZE] = {
			0.0625,
			0.25,
			0.375,
			0.25,
			0.0625
		};
		float2 sampleStep =
			(SharedData::bloomSettings.HaloRadius * BLOOM_GAUSSIAN_RADIUS_SCALE) /
			max(float2(bloomWidth, bloomHeight), float2(1.0, 1.0));
#		ifdef VR
		// The VR texture is side-by-side, so its reported width covers both eyes.
		// Convert X to per-eye pixels and retain eye-boundary clamping so both
		// runtimes use the same kernel without cross-eye sampling.
		sampleStep.x *= 2.0;
#		endif
		uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(a_uv);
		float3 wide = 0.0;

		[unroll] for (uint y = 0; y < BLOOM_GAUSSIAN_KERNEL_SIZE; ++y)
		{
			[unroll] for (uint x = 0; x < BLOOM_GAUSSIAN_KERNEL_SIZE; ++x)
			{
				const float weight = BLOOM_GAUSSIAN_WEIGHTS[x] * BLOOM_GAUSSIAN_WEIGHTS[y];
				if (x == BLOOM_GAUSSIAN_KERNEL_CENTER && y == BLOOM_GAUSSIAN_KERNEL_CENTER) {
					wide += center * weight;
				} else {
					const float2 offset = (float2(x, y) - float(BLOOM_GAUSSIAN_KERNEL_CENTER)) * sampleStep;
					const float2 sampleUV = ClampBloomSampleUV(a_uv + offset, eyeIndex, bloomWidth);
					wide += ImageTex.Sample(ImageSampler, sampleUV).xyz * weight;
				}
			}
		}

		bloom = lerp(center, wide, SharedData::bloomSettings.HaloSpread);
		float luminance = Color::RGBToLuminance(bloom);
		bloom = lerp(luminance.xxx, bloom, SharedData::bloomSettings.BloomSaturation);
		bloom *= SharedData::bloomSettings.BloomTint * SharedData::bloomSettings.EnhancementIntensity;
	}

	return bloom;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(DOWNSAMPLE)
	float3 downsampledColor = 0;
	for (int sampleIndex = 0; sampleIndex < DOWNSAMPLE; ++sampleIndex) {
		float2 texCoord = BlurOffsets[sampleIndex].xy * BlurScale.xy + input.TexCoord;
		[branch] if (Flags.x > 0.5)
		{
			texCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
		}
		float3 imageColor = clamp(ImageTex.Sample(ImageSampler, texCoord).xyz, 0.0, 50.0);  // Clamp to reasonable HDR bounds
#		if defined(RGB2LUM)
		imageColor = Color::RGBToLuminance(imageColor);
#		elif (defined(LUM) || defined(LUMCLAMP)) && !defined(DOWNADAPT)
		imageColor = imageColor.x;
#		endif
		downsampledColor += imageColor * BlurOffsets[sampleIndex].z;
	}
#		if defined(DOWNADAPT)
	float2 adaptValue = max(0.001, AdaptTex.Sample(AdaptSampler, input.TexCoord).xy);
	float2 adaptDelta = downsampledColor.xy - adaptValue;
	downsampledColor.xy =
		sign(adaptDelta) * clamp(abs(Param.wz * adaptDelta), 0.00390625, abs(adaptDelta)) +
		adaptValue;
#		endif
	psout.Color = float4(downsampledColor, BlurScale.z);

#	elif defined(BLEND)
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float3 inputColor = BlendTex.Sample(BlendSampler, uv).xyz;

	float3 bloomColor = 0;
	float3 vanillaBloomColor = 0;
	float bloomBlendWeight = 0.0;
	if (Flags.x > 0.5) {
#		if defined(ADAPTIVE_BALANCE)
		bloomColor = SampleVanillaBloomEnhanced(uv, vanillaBloomColor);
#		else
		bloomColor = ImageTex.Sample(ImageSampler, uv).xyz;
		vanillaBloomColor = bloomColor;
#		endif
	} else {
#		if defined(ADAPTIVE_BALANCE)
		bloomColor = SampleVanillaBloomEnhanced(input.TexCoord.xy, vanillaBloomColor);
#		else
		bloomColor = ImageTex.Sample(ImageSampler, input.TexCoord.xy).xyz;
		vanillaBloomColor = bloomColor;
#		endif
	}

#		if defined(ADAPTIVE_BALANCE)
	if (SharedData::bloomSettings.Enabled) {
		float bloomLuminance = Color::RGBToLuminance(bloomColor);
		float glowThreshold = min(SharedData::bloomSettings.CompressionThreshold, SharedData::bloomSettings.CompressionCeiling);
		float glowCeiling = SharedData::bloomSettings.CompressionCeiling;
		float bloomExcess = max(0.0, bloomLuminance - glowThreshold);
		float softRange = max(glowCeiling - glowThreshold, EPSILON_DIVISION);
		float compressedBloomLuminance = glowCeiling > 0.0 ?
		                                     (bloomLuminance <= glowThreshold ? bloomLuminance : glowThreshold + bloomExcess / (1.0 + bloomExcess / softRange)) :
		                                     0.0;
		float bloomScale = compressedBloomLuminance / max(bloomLuminance, EPSILON_DIVISION);
		bloomColor *= bloomScale;
		bloomBlendWeight = saturate(SharedData::bloomSettings.BlendWeight);
	}
#		endif

	float2 avgValue = AvgTex.Sample(AvgSampler, input.TexCoord.xy).xy;

	float3 outputColor = 0.0;

	if (avgValue.x != 0 && avgValue.y != 0)
		inputColor *= avgValue.y / avgValue.x;
	inputColor = max(0, inputColor);

	float3 blendedColor;

	[branch] if (Param.z > 0.5)
	{
		blendedColor = DisplayMapping::HuePreservingHejlBurgessDawson(inputColor, vanillaBloomColor, bloomColor, bloomBlendWeight);
	}
	else
	{
		float maxCol = Color::RGBToLuminance(inputColor);
		float mappedMax = GetTonemapFactorReinhard(maxCol).x;
		float3 compressedHuePreserving = inputColor * mappedMax / max(maxCol, EPSILON_DIVISION);
		blendedColor = DisplayMapping::ApplyBloom(compressedHuePreserving, vanillaBloomColor, bloomColor, Param.x, bloomBlendWeight);
	}

	float blendedLuminance = Color::RGBToLuminance(blendedColor);
	float3 tintedColor = Cinematic.w * lerp(lerp(blendedLuminance, blendedColor, Cinematic.x), blendedLuminance * Tint.xyz, Tint.w).xyz;
	float3 contrastedColor = lerp(avgValue.x, tintedColor, Cinematic.z);

	// Contrast modified to fix crushed shadows
	float safeAvgValue = max(avgValue.x, EPSILON_DIVISION);
	float3 contrastedColorModified = pow(max(0.0, abs(tintedColor) / safeAvgValue), Cinematic.z) * safeAvgValue * sign(tintedColor);
	contrastedColor = lerp(contrastedColorModified, contrastedColor, saturate(contrastedColorModified / 0.1f));  // blend in modified contrast for shadows

	outputColor = contrastedColor;

#		if defined(FADE)
	outputColor = lerp(outputColor, Fade.xyz, Fade.w);
#		endif

	if (ENABLE_LL)
		outputColor = Color::LinearToGammaSafe(outputColor);
	outputColor = FrameBuffer::ToSRGBColor(outputColor);

	psout.Color = float4(outputColor, 1.0);

#	endif

	return psout;
}
#endif
