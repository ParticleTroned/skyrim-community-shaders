#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState VLSourceSampler : register(s0);
SamplerState LFSourceSampler : register(s1);

Texture2D<float4> VLSourceTex : register(t0);
Texture2D<float4> LFSourceTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 VolumetricLightingColor : packoffset(c0);
};

#if defined(VOLUMETRIC_LIGHTING)
static const float kGodrayOpacityMax = 2.0;
static const float kGodrayTuningEpsilon = 0.0001;

float ApplyGodrayOpacity(float value)
{
	float positiveValue = isfinite(value) ? max(value, 0.0) : 0.0;
	float rawOpacity = SharedData::VolumetricLightingOpacity;
	float opacity = isfinite(rawOpacity) ? clamp(rawOpacity, 0.0, kGodrayOpacityMax) : 1.0;
	if (opacity <= kGodrayTuningEpsilon)
		return 0.0;
	if (abs(opacity - 1.0) <= kGodrayTuningEpsilon)
		return positiveValue;

	float boundedValue = saturate(positiveValue);
	float shapedValue = 1.0 - pow(max(1.0 - boundedValue, 0.0), opacity);
	return shapedValue + max(positiveValue - 1.0, 0.0) * opacity;
}
#endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 color = 0.0.xxx;

#	if defined(VOLUMETRIC_LIGHTING)
	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float volumetricLightingPower = ApplyGodrayOpacity(VLSourceTex.Sample(VLSourceSampler, screenPosition).x);
	color += VolumetricLightingColor.xyz * Color::VolumetricLighting(volumetricLightingPower.xxx).x;
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
