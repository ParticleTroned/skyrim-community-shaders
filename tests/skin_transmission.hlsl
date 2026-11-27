#include "Skin/Skin.hlsli"

RWTexture2D<float4> Output : register(u0);

[numthreads(4, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	float thickness = id.x / 3.0;
	float3 profile = 0;
	if (Skin::IsEnabled() && SharedData::skinData.sssParams.w > 0 && SharedData::skinData.sssParams.x > 0)
		profile = Skin::GetTransmissionProfile(thickness);
	Output[uint2(id.x, 0)] = float4(profile, 1);
	Output[uint2(id.x, 1)] = float4(Skin::SSSSTransmittanceProfile(0, SharedData::skinData.sssParams.y, 1), 1);
	Output[uint2(id.x, 2)] = float4(Skin::SSSSTransmittanceProfile(0, SharedData::skinData.sssParams.y, thickness), 1);
}
