#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
#include "DeferredCompositeColor.hlsli"

RWTexture2D<float4> MainRW : register(u0);
RWTexture2D<float4> NormalTAAMaskSpecularMaskRW : register(u1);
RWTexture2D<float2> MotionVectorsRW : register(u2);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside screen bounds
	if (any(dispatchID.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float depth = DepthTexture[dispatchID.xy];
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

	if (depth == 1.0)
		MotionVectorsRW[dispatchID.xy] = MotionBlur::GetSSMotionVector(positionWS, positionWS, eyeIndex);  // Apply sky motion vectors

#if !defined(DEFERRED_METADATA_ONLY)
	float3 color = DeferredComposite::CompositeColor(
		dispatchID.xy,
		uv,
		eyeIndex,
		normalGlossiness.z,
		normalVS,
		positionWS,
		MainRW[dispatchID.xy].xyz,
		SpecularTexture[dispatchID.xy],
		AlbedoTexture[dispatchID.xy]);

	MainRW[dispatchID.xy] = float4(color, 1.0);
#endif
	NormalTAAMaskSpecularMaskRW[dispatchID.xy] = float4(GBuffer::EncodeNormalVanilla(normalVS), 0.0, 0.0);
}
