Texture2D<float> InVLTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
RWTexture2D<float> OutVLTexture : register(u0);

#ifndef TG_DIM
#	define TG_DIM 256
#endif

#ifndef WINDOW
#	define WINDOW 12
#endif

cbuffer CBSize : register(b0)
{
	float4 invSize;
	float4 maxUV;
}

#include "Common/VolumetricLightingBlur.hlsli"

[numthreads(1, TG_DIM, 1)] void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID) {
	int idx = groupThreadId.y;
	int base = idx - WINDOW;
	int x = groupId.x;
	int y = groupId.y * (TG_DIM - WINDOW * 2) + base;

	int2 pix = clamp(int2(x, y), 0, screenSizeMin1.xy);
	vl[idx] = InVLTexture[pix];
	depth[idx] = VolumetricLightingBlur::GetLinearDepth(DepthTexture[pix]);

	GroupMemoryBarrierWithGroupSync();

	if (base >= 0 && base < TG_DIM - WINDOW * 2 && all(int2(x, y) <= screenSizeMin1.xy)) {
		OutVLTexture[int2(x, y)] = VolumetricLightingBlur::Filter(idx);
	}
}
