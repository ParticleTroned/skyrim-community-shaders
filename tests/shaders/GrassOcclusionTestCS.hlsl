#include "GrassOptimizations/GrassOcclusion.hlsli"

struct TestCase
{
	float4 values;
	float asymptote;
	uint projectionMode;
	uint2 padding;
};
StructuredBuffer<TestCase> Cases : register(t0);
RWStructuredBuffer<float2> Results : register(u0);
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	TestCase item = Cases[id.x];
	float margin = GrassOcclusionMargin(item.asymptote, item.projectionMode, item.values.x, item.values.y, item.values.z, item.values.w);
	Results[id.x] = float2(margin, DepthOrder::IsBehindWithBias(item.values.x, item.values.y, margin));
}
