#include "Upscaling/NeuralRendering/ColorCommon.hlsli"
Texture2D<float4> Baseline : register(t0);
Texture2D<float4> Neural : register(t1);
Texture2D<float4> Result : register(t2);
Texture2D<float4> Prepared : register(t4);
RWStructuredBuffer<float4> Statistics : register(u0);
groupshared float4 sums0[256];
groupshared float4 sums1[256];
groupshared float4 sums2[256];
groupshared float4 sums3[256];
groupshared float2 invalidCodecs[256];

[numthreads(256, 1, 1)] void main(uint3 id : SV_GroupThreadID) {
	uint lane = id.x;
	float4 s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
	float2 invalid = 0.0;
	uint area = RegionSize.x * RegionSize.y;
	uint stride = max(1u, (area + 4095u) / 4096u);
	uint count = (area + stride - 1u) / stride;
	for (uint i = lane; i < count; i += 256u) {
		uint pixelIndex = i * stride;
		uint2 p = uint2(pixelIndex % RegionSize.x, pixelIndex / RegionSize.x);
		float3 b = Baseline.Load(int3(p, 0)).rgb;
		float3 n = Neural.Load(int3(RegionOffset + p, 0)).rgb;
		float3 r = Result.Load(int3(p, 0)).rgb;
		bool bv = all(isfinite(b)), nv = all(isfinite(n)), rv = all(isfinite(r));
		float3 ignored;
		invalid.x += ForwardColor(b, ignored) ? 0.0 : 1.0;
		invalid.y += ReconstructCandidate(b, Prepared.Load(int3(RegionOffset + p, 0)).rgb, n, ignored) ? 0.0 : 1.0;
		s3 += float4(bv ? 0.0 : 1.0, nv ? 0.0 : 1.0, rv ? 0.0 : 1.0, 1.0);
		if (!bv || !rv)
			continue;
		float3 delta = abs(r - b);
		s0.xyz += b;
		s0.w += 1.0;
		s1.xyz += r;
		s1.w += (delta.r + delta.g + delta.b) / 3.0;
		s2.x = max(s2.x, Maximum3(delta));
		s2.y += Luminance(r) <= 1e-4 ? 1.0 : 0.0;
		s2.z += any(r >= 1.0) ? 1.0 : 0.0;
		s2.w += any(n < 0.0) || any(n > 1.0) ? 1.0 : 0.0;
	}
	sums0[lane] = s0;
	sums1[lane] = s1;
	sums2[lane] = s2;
	sums3[lane] = s3;
	invalidCodecs[lane] = invalid;
	GroupMemoryBarrierWithGroupSync();
	for (uint step = 128u; step > 0u; step >>= 1u) {
		if (lane < step) {
			sums0[lane] += sums0[lane + step];
			sums1[lane] += sums1[lane + step];
			sums2[lane].x = max(sums2[lane].x, sums2[lane + step].x);
			sums2[lane].yzw += sums2[lane + step].yzw;
			sums3[lane] += sums3[lane + step];
			invalidCodecs[lane] += invalidCodecs[lane + step];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		float valid = max(sums0[0].w, 1.0), exposure;
		bool exposureValid = EffectiveExposure(exposure);
		Statistics[0] = float4(sums0[0].xyz / valid, sums0[0].w);
		Statistics[1] = sums1[0] / valid;
		Statistics[2] = sums2[0];
		Statistics[3] = sums3[0];
		Statistics[4] = float4(invalidCodecs[0], exposureValid ? exposure : 0.0, exposureValid ? 1.0 : 0.0);
		// Raw producer evidence is separate from manual/captured effective exposure.
		Statistics[5] = (ControlFlags & 8u) != 0u ? ExposureSnapshot.Load(int3(0, 0, 0)) : 0.0;
	}
}
