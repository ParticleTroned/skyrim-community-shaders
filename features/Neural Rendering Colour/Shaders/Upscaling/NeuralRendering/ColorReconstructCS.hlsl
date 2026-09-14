#include "ColorCommon.hlsli"
Texture2D<float4> Baseline : register(t0);
Texture2D<float4> Neural : register(t1);
Texture2D<float4> Prepared : register(t2);
RWTexture2D<float4> Result : register(u0);

float3 Candidate(uint2 local, float3 baseline)
{
	float3 result;
	bool valid = ReconstructCandidate(baseline,
		Prepared.Load(int3(RegionOffset + local, 0)).rgb,
		Neural.Load(int3(RegionOffset + local, 0)).rgb, result);
	return valid ? result : baseline;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint2 local = id.xy;
	if (any(local >= RegionSize)) return;
	float4 baseline = Baseline.Load(int3(local, 0));
	if (!all(isfinite(baseline))) { Result[local] = 0.0; return; }
	if ((ControlFlags & 2u) != 0u) {
		// Display-only A/B: real inference and its timing remain untouched.
		Result[local] = baseline;
		return;
	}
	float3 candidate = Candidate(local, baseline.rgb);
	if ((ControlFlags & 1u) != 0u || ColorMode != 2) {
		Result[local] = float4(candidate, baseline.a);
		return;
	}
	if (DetailStrength == 0.0 && AppearanceMix == 0.0) { Result[local] = baseline; return; }
	if (AppearanceMix >= 1.0) { Result[local] = float4(candidate, baseline.a); return; }
	float3 baseWorking = ToWorking(baseline.rgb);
	float3 neuralWorking = ToWorking(candidate);
	float baseY = Luminance(baseWorking), neuralY = Luminance(neuralWorking);
	float3 preserved = baseline.rgb;
	if (DetailStrength > 0.0 && baseY > 1e-5 && neuralY > 1e-5 &&
		all(baseWorking >= 0.0) && all(neuralWorking >= 0.0) && all(isfinite(baseWorking)) && all(isfinite(neuralWorking))) {
		float residual = (log2(neuralY) - log2(baseY)), weightedResidual = 0.0, totalWeight = 0.0;
		// Only initialized pixels of this physical ROI, never multi-ROI gaps.
		[unroll] for (int y = -1; y <= 1; ++y) {
			[unroll] for (int x = -1; x <= 1; ++x) {
				uint2 p = uint2(clamp(int2(local) + int2(x, y) * 2, int2(0, 0), int2(RegionSize) - 1));
				float3 b = ToWorking(Baseline.Load(int3(p, 0)).rgb);
				float3 n = ToWorking(Candidate(p, Baseline.Load(int3(p, 0)).rgb));
				float by = Luminance(b), ny = Luminance(n);
				if (by > 1e-5 && ny > 1e-5 && all(b >= 0.0) && all(n >= 0.0) && all(isfinite(b)) && all(isfinite(n))) {
					float distance = abs((log2(by) - log2(baseY)));
					float weight = exp2(-4.0 * distance) * ((x == 0 && y == 0) ? 4.0 : 1.0);
					weightedResidual += weight * (log2(ny) - log2(by)); totalWeight += weight;
				}
			}
		}
		float lowFrequency = totalWeight > 0.0 ? weightedResidual / totalWeight : residual;
		uint2 edge = min(local, RegionSize - 1u - local);
		float edgeWeight = saturate(float(min(edge.x, edge.y)) / 4.0);
		float stops = clamp((residual - lowFrequency) * DetailStrength * edgeWeight, -MaximumDetailStops, MaximumDetailStops);
		float3 detail = FromWorking(baseWorking * exp2(stops));
		if (RepresentableRGB(detail)) preserved = detail;
	}
	float3 result = AppearanceMix <= 0.0 ? preserved : lerp(preserved, candidate, AppearanceMix);
	Result[local] = float4(RepresentableRGB(result) ? result : baseline.rgb, baseline.a);
}
