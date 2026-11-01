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

#if defined(VOLUMETRIC_LIGHTING)
	VolumetricLighting::SamplingRegion region = VolumetricLighting::GetSamplingRegion(
		VLSourceTex, FrameBuffer::DynamicResolutionParams1.xy);
	float2 screenPosition = clamp(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord),
		region.minimumUV, region.maximumUV);
	float rawVolumetricLighting = VLSourceTex.Sample(VLSourceSampler, screenPosition).x;
	float processedPower = Color::VolumetricLighting(VolumetricLighting::ApplyOpacity(rawVolumetricLighting, 1.0).xxx).x;
	float3 hazeColor = VolumetricLighting::SanitizeColor(VolumetricLightingColor.xyz);
	float clarity = VolumetricLighting::IsFinite(SharedData::VolumetricLightingClarity.x) ? saturate(SharedData::VolumetricLightingClarity.x) : 0.0;
	float intensity = VolumetricLighting::IsFinite(SharedData::VolumetricLightingClarity.y) ? clamp(SharedData::VolumetricLightingClarity.y, 0.0, 3.0) : 1.0;
	color += hazeColor * processedPower;
	bool tuneShafts = intensity != 1.0 || SharedData::VolumetricLightingOpacity != 1.0 ||
	                  SharedData::VolumetricLightingSaturation != 1.0 || SharedData::VolumetricLightingCustomColor.w > 0.0;
	[branch] if (clarity > 0.0 || tuneShafts)
	{
		float2 hazeAndShafts;
		if (VolumetricLighting::SeparateLighting(VLSourceTex, SharedData::DepthTexture, VLSourceSampler,
				screenPosition, input.TexCoord, region, FrameBuffer::DynamicResolutionParams1.xy,
				rawVolumetricLighting, processedPower, SharedData::CameraData, hazeAndShafts)) {
			float3 shaftColor = VolumetricLighting::ApplyColor(VolumetricLightingColor.xyz,
				SharedData::VolumetricLightingSaturation, SharedData::VolumetricLightingCustomColor);
			float shaftPower = VolumetricLighting::ApplyOpacity(hazeAndShafts.y, SharedData::VolumetricLightingOpacity);
			color = hazeColor * hazeAndShafts.x * (1.0 - clarity) + shaftColor * shaftPower * intensity;
		}
	}
#endif

#if defined(LENS_FLARE)
	float3 lensFlareColor = LFSourceTex.Sample(LFSourceSampler, input.TexCoord).xyz;
	if (SharedData::linearLightingSettings.enableLinearLighting) {
		color += Color::SkyrimGammaToLinear(lensFlareColor);
	} else {
		color += lensFlareColor;
	}
#endif

	psout.Color = VolumetricLighting::SanitizeColor(color);

	return psout;
}
#endif
