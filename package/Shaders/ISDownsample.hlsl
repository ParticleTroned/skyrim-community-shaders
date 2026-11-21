#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/VRStereoEffects.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState SourceSampler : register(s0);
SamplerState PreviousFrameSourceSampler : register(s1);
SamplerState MotionVectorsSampler : register(s2);

Texture2D<float4> SourceTex : register(t0);
Texture2D<float4> PreviousFrameSourceTex : register(t1);
Texture2D<float4> MotionVectorsTex : register(t2);

cbuffer PerGeometry : register(b2)
{
	float2 TexelSize : packoffset(c0);
	float SamplesCount : packoffset(c0.z);
	bool CompensateJittering : packoffset(c0.w);
	float4 OffsetsAndWeights[16] : packoffset(c1);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(VR)
	// Offsets and reprojection must retain the output eye, including at the seam.
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(input.TexCoord);
	uint2 sourceDimensions;
	SourceTex.GetDimensions(sourceDimensions.x, sourceDimensions.y);
#		if defined(DYNAMIC_SOURCE)
	float2 sourceMin = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(float2(0, 0), input.TexCoord);
	float2 sourceMax = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(float2(1, 1), input.TexCoord);
	sourceMin = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(sourceMin, eyeIndex, sourceDimensions, FrameBuffer::DynamicResolutionParams1.xy);
	sourceMax = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(sourceMax, eyeIndex, sourceDimensions, FrameBuffer::DynamicResolutionParams1.xy);
#		else
	float sourceMin = Stereo::ClampToEyeUV(float2(0, 0), eyeIndex, sourceDimensions).x;
	float sourceMax = Stereo::ClampToEyeUV(float2(1, 1), eyeIndex, sourceDimensions).x;
#		endif
#	endif

	float4 downsampledColor = 0;
	float luminance = 0;
	for (uint sampleIndex = 0; sampleIndex < asuint(SamplesCount); ++sampleIndex) {
		float2 texCoord = OffsetsAndWeights[sampleIndex].xy * TexelSize.xy + input.TexCoord;
#	if defined(DYNAMIC_SOURCE)
#		if defined(VR)
		texCoord = clamp(FrameBuffer::DynamicResolutionParams1.xy * texCoord, sourceMin, sourceMax);
#		else
		texCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
#		endif
#	elif defined(VR)
		texCoord.x = clamp(texCoord.x, sourceMin, sourceMax);
#	endif
		float4 sourceColor = SourceTex.Sample(SourceSampler, texCoord);
#	if defined(DYNAMIC_SOURCE)
		downsampledColor += sourceColor;
#	else
		float sampleLuminance = Color::RGBToLuminanceAlternative(sourceColor.xyz);
		if (sampleLuminance > luminance) {
			downsampledColor = sourceColor;
			luminance = sampleLuminance;
		}
#	endif
	}
#	if defined(DYNAMIC_SOURCE)
	psout.Color = downsampledColor / asuint(SamplesCount);
#	else
	if (CompensateJittering) {
		float2 adjustedTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
#		if defined(VR)
		adjustedTexCoord = VRStereoEffects::ClampDynamicStereoUVToEyeTexel(
			adjustedTexCoord, eyeIndex, MotionVectorsTex, FrameBuffer::DynamicResolutionParams1.xy);
#		endif
		float2 motion = MotionVectorsTex.Sample(MotionVectorsSampler, adjustedTexCoord).xy;
		float2 previousTexCoord = input.TexCoord + motion;
#		if defined(VR)
		previousTexCoord = VRStereoEffects::ClampStereoUVToEyeTexel(previousTexCoord, eyeIndex, PreviousFrameSourceTex);
#		endif
		float4 previousFrameColor =
			PreviousFrameSourceTex.Sample(PreviousFrameSourceSampler, previousTexCoord).xyzw;
		psout.Color = 0.5 * (previousFrameColor + downsampledColor);
	} else {
		psout.Color = downsampledColor;
	}
#	endif

	return psout;
}
#endif
