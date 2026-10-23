#include "Common/FoveatedMask.hlsli"
#include "Upscaling/NeuralRendering/ModelResolutionCommon.hlsli"
#ifdef MODEL_OUTPUT_SELECTION
#	include "Common/NeuralRenderingSelection.hlsli"
#	include "Upscaling/NeuralRendering/ModelResolutionStorage.hlsli"
#endif

Texture2D<float4> SourceColor : register(t0);
Texture2D<float4> ProxyColor : register(t1);
Texture2D<float4> ProxyNeural : register(t2);
#ifdef PACKED_CROP_COVERAGE_GUARD
StructuredBuffer<uint> PackedCoverage : register(t3);
#endif
#ifdef MODEL_OUTPUT_SELECTION
Texture2D<float> SelectionMask : register(t4);
#endif
RWTexture2D<float4> Result : register(u0);

bool ReadResidual(int2 coordinate, uint4 bounds, out float3 residual)
{
	coordinate = clamp(coordinate, int2(bounds.xy), int2(bounds.xy + bounds.zw - 1u));
	float3 original = ProxyColor.Load(int3(coordinate, 0)).rgb;
	float3 neural = ProxyNeural.Load(int3(coordinate, 0)).rgb;
	residual = neural - original;
	return all(isfinite(original)) && all(isfinite(neural)) && all(isfinite(residual));
}

bool BilinearResidual(float2 position, uint4 bounds, out float3 residual)
{
	int2 base = int2(floor(position));
	float2 phase = frac(position);
	float3 r00, r10, r01, r11;
	bool valid = ReadResidual(base, bounds, r00);
	valid = ReadResidual(base + int2(1, 0), bounds, r10) && valid;
	valid = ReadResidual(base + int2(0, 1), bounds, r01) && valid;
	valid = ReadResidual(base + int2(1, 1), bounds, r11) && valid;
	residual = lerp(lerp(r00, r10, phase.x), lerp(r01, r11, phase.x), phase.y);
	return valid;
}

// The three-lobe window is sinc(x) * sinc(x / 3); each axis is normalized below.
float Lanczos3Weight(float distance)
{
	const float x = abs(distance);
	if (x < 1e-5)
		return 1.0;
	if (x >= 3.0)
		return 0.0;
	const float angle = 3.14159265358979323846 * x;
	return (sin(angle) / angle) * (sin(angle / 3.0) / (angle / 3.0));
}

bool Lanczos3Residual(float2 position, uint4 bounds, out float3 residual)
{
	const int2 origin = int2(floor(position)) - 2;
	float weightsX[6], weightsY[6];
	float2 normalization = 0.0;
	[unroll] for (int i = 0; i < 6; ++i)
	{
		weightsX[i] = Lanczos3Weight(position.x - float(origin.x + i));
		weightsY[i] = Lanczos3Weight(position.y - float(origin.y + i));
		normalization += float2(weightsX[i], weightsY[i]);
	}
	residual = 0.0;
	bool valid = true;
	// Clamp every tap to this eye's owned rectangle or packed tile.
	[unroll] for (int y = 0; y < 6; ++y)
	{
		float3 row = 0.0;
		[unroll] for (int x = 0; x < 6; ++x)
		{
			float3 tapResidual;
			valid = ReadResidual(origin + int2(x, y), bounds, tapResidual) && valid;
			row += tapResidual * weightsX[x];
		}
		residual += row * weightsY[y];
	}
	residual /= normalization.x * normalization.y;
	return valid && all(isfinite(residual));
}

float CentralWeight(float2 uv)
{
	const float distance = FoveatedComputeMaskDistance(uv, CentralScale, CentralHorizontalScale, CentralOffset);
	if (distance <= 1.0)
		return 1.0;
	if (CentralFeather == 0.0)
		return 0.0;
	const float2 radii = FoveatedComputeMaskRadii(CentralScale, CentralHorizontalScale);
	const float2 normalized = abs((uv - FoveatedComputeCenterUV(CentralOffset)) / radii);
	// The mask gradient converts radial distance into output pixels near the boundary.
	const float2 gradient = normalized * normalized * normalized / (radii * FinalOutputSize * distance * distance * distance);
	const float edgeDistance = (distance - 1.0) / length(gradient);
	return 1.0 - smoothstep(0.0, CentralFeather, edgeDistance);
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	uint2 pixel = SourceRegionOffset + id.xy;
#ifdef MODEL_OUTPUT_SELECTION
	if (SelectedOutput != 0u) {
		if (any(id.xy >= SourceSize))
			return;
		pixel = id.xy;
	} else
#endif
		if (any(id.xy >= SourceRegionSize))
		return;
	float4 source = SourceColor.Load(int3(pixel, 0));
#ifdef MODEL_OUTPUT_SELECTION
	float selectionWeight = 1.0;
	if (SelectedOutput != 0u) {
		const float2 uv = (float2(pixel) + 0.5) / float2(SourceSize);
		if (SelectionMode != 0u) {
			selectionWeight = 0.0;
			[branch] if (all(uv >= SelectionBounds.xy) && all(uv < SelectionBounds.zw))
				selectionWeight = NeuralRenderingSelectionWeight(saturate(SelectionMask.Load(int3(pixel, 0))), SelectionMode);
		}
		if (selectionWeight <= 0.0 || any(pixel < SourceRegionOffset) || any(pixel >= SourceRegionOffset + SourceRegionSize)) {
			Result[pixel] = source;
			return;
		}
	}
#endif
	float weight = 1.0;
	if (CentralActive != 0u) {
		const float2 uv = (float2(pixel) + CropOrigin + 0.5) / FullEyeSize;
		weight = CentralWeight(uv);
	}
	if (weight <= 0.0) {
		Result[pixel] = source;
		return;
	}
#ifdef PACKED_CROP_COVERAGE_GUARD
	if (PackedPadding != 0u && PackedCoverage[0] != 0u) {
		Result[pixel] = source;
		return;
	}
#endif
	float2 position = (float2(pixel) + 0.5) * float2(ModelSize) / float2(SourceSize) - 0.5;
	uint4 bounds = uint4(ModelRegionOffset, ModelRegionSize);
	if (PackedCount != 0u) {
		uint tileIndex = FindPackedCrop(float2(pixel) + 0.5, true);
		if (tileIndex == PackedCount) {
			Result[pixel] = source;
			return;
		}
		PackedCropTile tile = PackedTiles[tileIndex];
		position = (float2(pixel) + 0.5) * float2(UnpackedSize) / float2(SourceSize) - 0.5 + float2(tile.packed.xy) - float2(tile.model.xy);
		bounds = tile.packed;
	}
	float3 residual;
	bool valid;
	[branch] if (ReconstructionFilter == 1u)
		valid = Lanczos3Residual(position, bounds, residual);
	else valid = BilinearResidual(position, bounds, residual);
	// Reconstruct only the matched change; the original image retains its fine detail.
	precise float3 candidate = source.rgb + residual * weight;
	valid = valid && all(isfinite(candidate));
	if (OutputFormat == 1u)
		valid = valid && all(abs(candidate) <= 65504.0);
	else if (OutputFormat == 2u)
		valid = valid && all(candidate >= 0.0) && all(candidate <= float3(65024.0, 65024.0, 64512.0));
	else if (OutputFormat == 3u)
		valid = valid && all(candidate >= 0.0) && all(candidate <= 1.0);
	float4 reconstructed = float4(valid ? candidate : source.rgb, source.a);
#ifdef MODEL_OUTPUT_SELECTION
	if (SelectedOutput != 0u) {
		if (SelectionMode != 1u || selectionWeight >= 1.0) {
			Result[pixel] = reconstructed;
			return;
		}
		reconstructed = RoundModelReconstruction(reconstructed, OutputStorageFormat);
		Result[pixel] = lerp(source, reconstructed, selectionWeight);
		return;
	}
#endif
	Result[pixel] = reconstructed;
}
