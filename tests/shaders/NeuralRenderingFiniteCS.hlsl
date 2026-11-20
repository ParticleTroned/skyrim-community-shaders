#include "Common/Finite.hlsli"

Texture2D<uint4> InputBits : register(t0);
RWTexture2D<uint4> Result : register(u0);

[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	uint width, height;
	InputBits.GetDimensions(width, height);
	if (id.x >= width)
		return;
	float4 value = asfloat(InputBits.Load(int3(id.x, 0, 0)));
	Result[uint2(id.x, 0)] = uint4(Finite::IsFinite(value));
	Result[uint2(id.x, 1)] = uint4(Finite::IsFinite(value.x), Finite::IsFinite(value.y),
		Finite::IsFinite(value.z), Finite::IsFinite(value.w));
	Result[uint2(id.x, 2)] = uint4(Finite::IsFinite(value.xy), Finite::IsFinite(value.zw));
	Result[uint2(id.x, 3)] = uint4(Finite::IsFinite(value.xyz), Finite::IsFinite(value.w));
}
