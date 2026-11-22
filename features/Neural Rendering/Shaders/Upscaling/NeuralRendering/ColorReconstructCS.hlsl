#include "Common/PerformanceCostReference.hlsli"
#include "Upscaling/NeuralRendering/ColorCommon.hlsli"
Texture2D<float4> Baseline : register(t0);
Texture2D<float4> Neural : register(t1);
Texture2D<float4> Prepared : register(t2);
RWTexture2D<float4> Result : register(u0);

static const uint ReconstructionGroupSize = 8u;
static const uint ReconstructionTileSize = ReconstructionGroupSize + 2u;
struct ReconstructionSample
{
	float3 candidate;
	float baseLog;
	float residual;
	uint valid;
};
groupshared ReconstructionSample ReconstructionTile[ReconstructionTileSize * ReconstructionTileSize];

float3 Candidate(uint2 local, float3 baseline)
{
	float3 result;
	bool valid = ReconstructCandidate(baseline,
		Prepared.Load(int3(RegionOffset + local, 0)).rgb,
		Neural.Load(int3(RegionOffset + local, 0)).rgb, result);
	return valid ? result : baseline;
}

uint ReconstructionTileIndex(uint2 local, uint2 groupBase)
{
	uint2 tile = uint2(int2(local) - int2(groupBase) + 1);
	return tile.y * ReconstructionTileSize + tile.x;
}

void PrepareReconstructionTile(uint2 groupBase, uint groupIndex)
{
	for (uint index = groupIndex; index < ReconstructionTileSize * ReconstructionTileSize;
		index += ReconstructionGroupSize * ReconstructionGroupSize) {
		int2 tile = int2(index % ReconstructionTileSize, index / ReconstructionTileSize);
		uint2 local = uint2(clamp(int2(groupBase) + tile - 1, int2(0, 0), int2(RegionSize) - 1));
		float3 baseline = Baseline.Load(int3(local, 0)).rgb;
		ReconstructionSample sample;
		sample.candidate = Candidate(local, baseline);
		float3 b = ToWorking(baseline), n = ToWorking(sample.candidate);
		float by = Luminance(b), ny = Luminance(n);
		// Candidate failure can still leave a valid baseline neighbour with zero residual.
		sample.valid = by > 1e-5 && ny > 1e-5 && all(b >= 0.0) && all(n >= 0.0) &&
		               all(Finite::IsFinite(b)) && all(Finite::IsFinite(n));
		sample.baseLog = 0.0;
		sample.residual = 0.0;
		if (sample.valid) {
			sample.baseLog = log2(by);
			sample.residual = log2(ny) - sample.baseLog;
		}
		ReconstructionTile[index] = sample;
	}
}

// Packed inputs must be finite, nonnegative and within their format's range.
float3 RoundPreserveSourceForStorage(float3 value)
{
	if ((ControlFlags & 0x300u) != 0x100u)
		return value;
	// Packed UAV stores can truncate: round once to the nearest value, ties to even.
	const uint3 discardedBits = uint3(17u, 17u, 18u);
	const uint3 discardedMask = (1u << discardedBits) - 1u;
	uint3 bits = asuint(value);
	uint3 bias = (discardedMask >> 1u) + ((bits >> discardedBits) & 1u);
	float3 normal = asfloat((bits + bias) & ~discardedMask);
	// Below 2^-14, packed subnormals use fixed 2^-20 / 2^-19 steps.
	const float3 subnormalScale = float3(1048576.0, 1048576.0, 524288.0);
	float3 subnormal = round(value * subnormalScale) / subnormalScale;
	return value < (1.0 / 16384.0) ? subnormal : normal;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
	uint2 groupBase = groupID.xy * ReconstructionGroupSize;
#if (CSX_PERF_REFERENCE_FLAGS & CSX_PERF_NEURAL_PREPARATION)
	const bool useTile = false;
#else
	const bool useTile = all(RegionSize > 0u) && (ControlFlags & 3u) == 0u && ColorMode == 2 &&
	                     DetailStrength > 0.0 && LightingPreservation != 0.0 && AppearanceMix < 1.0;
#endif
	[branch] if (useTile)
	{
		PrepareReconstructionTile(groupBase, groupIndex);
		// Dispatch constants gate this barrier; even padding and invalid output lanes participate.
		GroupMemoryBarrierWithGroupSync();
	}
	uint2 local = id.xy;
	if (any(local >= RegionSize))
		return;
	float4 baseline = Baseline.Load(int3(local, 0));
	if (!all(Finite::IsFinite(baseline))) {
		Result[local] = 0.0;
		return;
	}
	if ((ControlFlags & 2u) != 0u) {
		// Display-only A/B: real inference and its timing remain untouched.
		Result[local] = baseline;
		return;
	}
	const bool preserveSource = (ControlFlags & 1u) == 0u && ColorMode == 2;
#if (CSX_PERF_REFERENCE_FLAGS & CSX_PERF_NEURAL_PREPARATION)
	float3 candidate = Candidate(local, baseline.rgb);
	if (!preserveSource) {
		Result[local] = float4(candidate, baseline.a);
		return;
	}
#endif
	[branch] if (preserveSource && DetailStrength == 0.0 && AppearanceMix == 0.0)
	{
		Result[local] = baseline;
		return;
	}
#if !(CSX_PERF_REFERENCE_FLAGS & CSX_PERF_NEURAL_PREPARATION)
	float3 candidate;
	[branch] if (useTile)
		candidate = ReconstructionTile[ReconstructionTileIndex(local, groupBase)].candidate;
	else candidate = Candidate(local, baseline.rgb);
	if (!preserveSource) {
		Result[local] = float4(candidate, baseline.a);
		return;
	}
#endif
	if (AppearanceMix >= 1.0) {
		Result[local] = float4(candidate, baseline.a);
		return;
	}
	uint2 regionMin = 0, regionMax = RegionSize - 1u;
	if (ReconstructionRegionCount != 0u) {
		bool owned = false;
		[unroll] for (uint tile = 0; tile < 4; ++tile)
		{
			uint4 region = ReconstructionRegions[tile];
			if (tile < ReconstructionRegionCount && all(local >= region.xy) && all(local - region.xy < region.zw)) {
				regionMin = region.xy;
				regionMax = region.xy + region.zw - 1u;
				owned = true;
				break;
			}
		}
		if (!owned) {
			Result[local] = baseline;
			return;
		}
	}
	float3 baseWorking = ToWorking(baseline.rgb);
	float3 neuralWorking = ToWorking(candidate);
	float baseY = Luminance(baseWorking), neuralY = Luminance(neuralWorking);
	float3 preserved = baseline.rgb;
	if (DetailStrength > 0.0 && baseY > 1e-5 && neuralY > 1e-5 &&
		all(baseWorking >= 0.0) && all(neuralWorking >= 0.0) && all(Finite::IsFinite(baseWorking)) && all(Finite::IsFinite(neuralWorking))) {
		// The centre needs its original arithmetic to preserve optimized output bits.
		float residual = (log2(neuralY) - log2(baseY));
		float acceptedResidual = residual;
		// Zero preservation uses only the current residual, including resolved Neural Lighting.
		[branch] if (LightingPreservation != 0.0)
		{
			float weightedResidual = 0.0, totalWeight = 0.0;
			// Adjacent taps preserve fine detail without reading another crop or atlas padding.
			[unroll] for (int y = -1; y <= 1; ++y)
			{
				[unroll] for (int x = -1; x <= 1; ++x)
				{
					uint2 p = uint2(clamp(int2(local) + int2(x, y), int2(regionMin), int2(regionMax)));
#if !(CSX_PERF_REFERENCE_FLAGS & CSX_PERF_NEURAL_PREPARATION)
					[branch]
#endif
						if (useTile)
					{
						ReconstructionSample sample = ReconstructionTile[ReconstructionTileIndex(p, groupBase)];
						if (sample.valid) {
							float distance = abs(sample.baseLog - log2(baseY));
							float weight = exp2(-4.0 * distance) * ((x == 0 && y == 0) ? 4.0 : 1.0);
							weightedResidual += weight * sample.residual;
							totalWeight += weight;
						}
					}
					else
					{
						// Keep the untiled expressions intact so the reference permutation retains its bytecode.
						float3 b = ToWorking(Baseline.Load(int3(p, 0)).rgb);
						float3 n = ToWorking(Candidate(p, Baseline.Load(int3(p, 0)).rgb));
						float by = Luminance(b), ny = Luminance(n);
						if (by > 1e-5 && ny > 1e-5 && all(b >= 0.0) && all(n >= 0.0) && all(Finite::IsFinite(b)) && all(Finite::IsFinite(n))) {
							float distance = abs((log2(by) - log2(baseY)));
							float weight = exp2(-4.0 * distance) * ((x == 0 && y == 0) ? 4.0 : 1.0);
							weightedResidual += weight * (log2(ny) - log2(by));
							totalWeight += weight;
						}
					}
				}
			}
			float lowFrequency = totalWeight > 0.0 ? weightedResidual / totalWeight : residual;
			if (LightingPreservation == 1.0)
				acceptedResidual = residual - lowFrequency;
			else
				acceptedResidual = residual - LightingPreservation * lowFrequency;
		}
		uint2 edge = min(local - regionMin, regionMax - local);
		float edgeWeight = saturate(float(min(edge.x, edge.y)) / 4.0);
		float stops = clamp(acceptedResidual * DetailStrength * edgeWeight, -MaximumDetailStops, MaximumDetailStops);
		float3 detail = FromWorking(baseWorking * exp2(stops));
		if (RepresentableRGB(detail))
			preserved = detail;
	}
	float3 result = AppearanceMix <= 0.0 ? preserved : lerp(preserved, candidate, AppearanceMix);
	Result[local] = float4(RepresentableRGB(result) ? RoundPreserveSourceForStorage(result) : baseline.rgb, baseline.a);
}
