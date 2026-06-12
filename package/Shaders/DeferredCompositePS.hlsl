#include "Common/GBuffer.hlsli"
#include "DeferredCompositeColor.hlsli"

Texture2D<float3> MainInputTexture : register(t16);

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
};

float4 main(VS_OUTPUT input) : SV_Target0
{
	uint2 pixCoord = uint2(input.position.xy);
	if (any(pixCoord >= uint2(SharedData::BufferDim.xy))) {
		discard;
		return 0;
	}

	float2 uv = float2(pixCoord + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float3 normalGlossiness = NormalRoughnessTexture[pixCoord];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float depth = DepthTexture[pixCoord];
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

	float3 color = DeferredComposite::CompositeColor(
		pixCoord,
		uv,
		eyeIndex,
		normalGlossiness.z,
		normalVS,
		positionWS,
		MainInputTexture[pixCoord].xyz,
		SpecularTexture[pixCoord],
		AlbedoTexture[pixCoord]);

	return float4(color, 1.0);
}
