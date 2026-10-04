cbuffer Batch : register(b0)
{
	uint InstanceCount;
	uint GroupCount;
	uint2 Padding;
};

cbuffer NativeFade : register(b1)
{
	float4 GroupFades[240];
};

StructuredBuffer<uint2> GroupRanges : register(t0);
RWStructuredBuffer<float> InstanceFades : register(u0);

[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (id.x >= InstanceCount || GroupCount == 0)
		return;
	uint lo = 0;
	uint hi = GroupCount - 1;
	while (lo < hi) {
		uint mid = (lo + hi) / 2;
		if (id.x < GroupRanges[mid].x)
			hi = mid;
		else
			lo = mid + 1;
	}
	uint fadeIndex = GroupRanges[lo].y;
	InstanceFades[id.x] = GroupFades[fadeIndex >> 2][fadeIndex & 3];
}
