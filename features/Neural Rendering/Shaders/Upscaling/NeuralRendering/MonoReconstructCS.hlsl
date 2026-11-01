#include "Upscaling/NeuralRendering/MonoCommon.hlsli"
Texture2D<float4> Baseline : register(t0);
Texture2D<float4> Neural : register(t1);
Texture2D<float4> Prepared : register(t2);
#include "Upscaling/NeuralRendering/MonoSelection.hlsli"
ByteAddressBuffer PackedCoverage : register(t7);
RWTexture2D<float4> Result : register(u0);

bool ReadResidual(int2 pixel, uint4 validRegion, out float3 residual, out float3 original)
{
	pixel = clamp(pixel, int2(validRegion.xy), int2(validRegion.xy + validRegion.zw - 1u));
	float3 neural;
	const bool valid = InverseColor(Prepared.Load(int3(pixel, 0)).rgb, original) &&
		InverseColor(Neural.Load(int3(pixel, 0)).rgb, neural);
	residual = valid ? neural - original : 0.0;
	return valid && all(isfinite(residual));
}
float3 Candidate(uint2 pixel, float3 baseline)
{
	float3 preparedCheck;
	if (!ForwardColor(baseline, preparedCheck) || any(preparedCheck < 0.0) || any(preparedCheck > 65504.0)) return baseline;
	float2 position = PeripheralForward((float2(pixel) + 0.5) / SourceSize) * ModelSize - 0.5;
	uint4 validRegion = InferenceRegion;
	if (Packing.x > 0u) {
		const int tile = SourceTile(pixel);
		if (tile < 0) return baseline;
		validRegion = TilePacked[tile];
		position = (float2(pixel) + 0.5) * Packing.yz / SourceSize - 0.5 - TileModel[tile].xy + TilePacked[tile].xy;
	}
	if (any(position + 0.5 < validRegion.xy) || any(position + 0.5 >= validRegion.xy + validRegion.zw)) return baseline;
	const int2 base = int2(floor(position));
	const float2 phase = frac(position);
	float3 residuals[4], proxies[4];
	bool valid = ReadResidual(base, validRegion, residuals[0], proxies[0]);
	valid = ReadResidual(base + int2(1, 0), validRegion, residuals[1], proxies[1]) && valid;
	valid = ReadResidual(base + int2(0, 1), validRegion, residuals[2], proxies[2]) && valid;
	valid = ReadResidual(base + int2(1, 1), validRegion, residuals[3], proxies[3]) && valid;
	if (!valid) return baseline;
	const float4 spatial = float4((1.0 - phase.x) * (1.0 - phase.y), phase.x * (1.0 - phase.y), (1.0 - phase.x) * phase.y, phase.x * phase.y);
	float4 weights = spatial;
	if (Options.z != 0u) {
		float magnitude = max(Maximum3(abs(baseline)), max(max(Maximum3(abs(proxies[0])), Maximum3(abs(proxies[1]))), max(Maximum3(abs(proxies[2])), Maximum3(abs(proxies[3])))));
		if (magnitude > 1e-6) {
			[unroll] for (uint i = 0; i < 4; ++i) {
				const float3 difference = (baseline - proxies[i]) / magnitude;
				weights[i] *= rcp(1.0 + 16.0 * Luminance(difference * difference));
			}
		}
	}
	const float normalization = dot(weights, 1.0);
	if (!isfinite(normalization) || normalization <= 1e-6) weights = spatial;
	else weights /= normalization;
	precise float3 candidate = baseline + residuals[0] * weights.x + residuals[1] * weights.y + residuals[2] * weights.z + residuals[3] * weights.w;
	return RepresentableRGB(candidate) ? candidate : baseline;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const uint2 pixel = id.xy;
	if (any(pixel >= SourceSize)) return;
	const float4 original = Baseline.Load(int3(pixel, 0));
	if (Packing.x > 0u && PackedCoverage.Load(0) != 0u) { Result[pixel] = original; return; }
	if (!all(isfinite(original))) { Result[pixel] = 0.0; return; }
	const float selection = SelectionWeight(pixel) * ScreenWeight(pixel);
	if (selection <= 0.0) { Result[pixel] = original; return; }
	float3 candidate = Candidate(pixel, original.rgb);
	if (ColorMode == 2u) {
		const float3 base = ToWorking(original.rgb), edited = ToWorking(candidate);
		const float baseY = Luminance(base), neuralY = Luminance(edited);
		candidate = original.rgb;
		if (DetailStrength > 0.0 && baseY > 1e-5 && neuralY > 1e-5 && all(base >= 0.0) && all(edited >= 0.0)) {
			const float residual = log2(neuralY) - log2(baseY);
			float weighted = 0.0, total = 0.0;
			[unroll] for (int y = -1; y <= 1; ++y) {
				[unroll] for (int x = -1; x <= 1; ++x) {
					const uint2 neighbor = uint2(clamp(int2(pixel) + int2(x, y), 0, int2(SourceSize) - 1));
					const float3 b = Baseline.Load(int3(neighbor, 0)).rgb;
					const float by = Luminance(ToWorking(b));
					const float ny = Luminance(ToWorking(Candidate(neighbor, b)));
					if (by > 1e-5 && ny > 1e-5 && isfinite(by) && isfinite(ny)) {
						const float weight = exp2(-4.0 * abs(log2(by) - log2(baseY))) * ((x == 0 && y == 0) ? 4.0 : 1.0);
						weighted += weight * (log2(ny) - log2(by)); total += weight;
					}
				}
			}
			const float lowFrequency = total > 0.0 ? weighted / total : residual;
			const float stops = clamp((residual - LightingPreservation * lowFrequency) * DetailStrength, -MaximumDetailStops, MaximumDetailStops);
			const float3 preserved = FromWorking(base * exp2(stops));
			if (RepresentableRGB(preserved)) candidate = preserved;
		}
	}
	const float3 result = lerp(original.rgb, candidate, selection);
	Result[pixel] = float4(RepresentableRGB(result) ? result : original.rgb, original.a);
}
