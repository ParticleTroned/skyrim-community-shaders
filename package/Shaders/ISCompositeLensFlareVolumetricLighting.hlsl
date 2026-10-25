#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VolumetricLighting.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color: SV_Target0;
};

#if defined(PSHADER)
#	include "Common/VolumetricLightingClarity.hlsli"

SamplerState VLSourceSampler : register(s0);
SamplerState LFSourceSampler : register(s1);

Texture2D<float4> VLSourceTex : register(t0);
Texture2D<float4> LFSourceTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 VolumetricLightingColor : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 color = 0.0.xxx;

#	if defined(VOLUMETRIC_LIGHTING)
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(input.TexCoord);
	VolumetricLighting::SamplingRegion region = VolumetricLighting::GetSamplingRegion(
		VLSourceTex, FrameBuffer::DynamicResolutionParams1.xy, eyeIndex);
	float2 screenPosition = clamp(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord),
		region.minimumUV, region.maximumUV);
	float rawVolumetricLighting = VLSourceTex.Sample(VLSourceSampler, screenPosition).x;
	float volumetricLightingPower = VolumetricLighting::ApplyOpacity(rawVolumetricLighting, SharedData::VolumetricLightingOpacity);
	float3 godrayColor = VolumetricLighting::ApplyColor(VolumetricLightingColor.xyz,
		SharedData::VolumetricLightingSaturation, SharedData::VolumetricLightingCustomColor);
	float processedPower = Color::VolumetricLighting(volumetricLightingPower.xxx).x;
	float clarity = SharedData::GodrayFinalBrightness > 0.0 ? SharedData::VolumetricLightingClarity.x : 0.0;
	float clearPower = VolumetricLighting::ApplyClarity(VLSourceTex, VLSourceSampler, screenPosition,
		region, rawVolumetricLighting, processedPower,
		clarity, SharedData::VolumetricLightingOpacity);
	color += godrayColor * clearPower * SharedData::GodrayFinalBrightness;
#	endif

#	if defined(LENS_FLARE)
	float3 lensFlareColor = LFSourceTex.Sample(LFSourceSampler, input.TexCoord).xyz;
	if (SharedData::linearLightingSettings.enableLinearLighting) {
		color += Color::SkyrimGammaToLinear(lensFlareColor);
	} else {
		color += lensFlareColor;
	}
#	endif

	psout.Color = color;

	return psout;
}
#endif
