#include "Common/FoveatedMask.hlsli"
#include "Upscaling/NeuralRendering/ModelResolutionCommon.hlsli"
#ifdef NR_PERIPHERAL_COMPRESSION
#	include "Common/Color.hlsli"
#	include "Upscaling/NeuralRendering/PeripheralCompression.hlsli"
#endif
#ifdef MODEL_OUTPUT_SELECTION
#	include "Common/NeuralRenderingSelection.hlsli"
#	include "Upscaling/NeuralRendering/ModelResolutionStorage.hlsli"
#	include "Upscaling/NeuralRendering/SceneCategorySelection.hlsli"
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

bool ReadResidual(int2 coordinate, uint4 bounds, out float3 residual, out float3 original)
{
	coordinate = clamp(coordinate, int2(bounds.xy), int2(bounds.xy + bounds.zw - 1u));
	original = ProxyColor.Load(int3(coordinate, 0)).rgb;
	float3 neural = ProxyNeural.Load(int3(coordinate, 0)).rgb;
	residual = neural - original;
	return all(isfinite(original)) && all(isfinite(neural)) && all(isfinite(residual));
}

bool BilinearResidual(float2 position, uint4 bounds, out float3 residual, out float3 proxy)
{
	int2 base = int2(floor(position));
	float2 phase = frac(position);
	float3 r00, r10, r01, r11, p00, p10, p01, p11;
	bool valid = ReadResidual(base, bounds, r00, p00);
	valid = ReadResidual(base + int2(1, 0), bounds, r10, p10) && valid;
	valid = ReadResidual(base + int2(0, 1), bounds, r01, p01) && valid;
	valid = ReadResidual(base + int2(1, 1), bounds, r11, p11) && valid;
	residual = lerp(lerp(r00, r10, phase.x), lerp(r01, r11, phase.x), phase.y);
	proxy = lerp(lerp(p00, p10, phase.x), lerp(p01, p11, phase.x), phase.y);
	return valid;
}

#ifdef GUIDED_NR_RECONSTRUCTION
#	include "Upscaling/NeuralRendering/ModelResolutionGuided.hlsli"
#endif

#ifdef NR_PERIPHERAL_COMPRESSION
float3 TransferCompressedResidual(float3 source, float3 proxy, float3 residual)
{
	// Mixed bright proxy texels must not transfer their full absolute edit into a darker source pixel.
	// Magnitudes keep signed HDR continuous as individual channels cross zero.
	float sourceY = Color::RGBToLuminance(abs(source));
	float proxyY = Color::RGBToLuminance(abs(proxy));
	float transfer = proxyY > sourceY ? sourceY / proxyY : 1.0;
#	if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_SHADOW_TRANSFER_TUNING)
	transfer = lerp(1.0, transfer, ExperimentalShadowTransfer);
#	endif
	residual *= transfer;
	return residual;
}
#endif

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
		if (SelectionMode == 3u) {
			selectionWeight = SceneCategorySelectionWeight(pixel);
		} else if (SelectionMode != 0u) {
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
#ifdef NR_PERIPHERAL_COMPRESSION
	float2 position = PeripheralForward((float2(pixel) + 0.5) / float2(SourceSize)) * float2(ModelSize) - 0.5;
#else
	float2 position = (float2(pixel) + 0.5) * float2(ModelSize) / float2(SourceSize) - 0.5;
#endif
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
	float3 residual, proxy;
	bool valid;
#ifdef GUIDED_NR_RECONSTRUCTION
	valid = GuidedResidual(position, bounds, source.rgb, residual, proxy);
#else
	valid = BilinearResidual(position, bounds, residual, proxy);
#endif
#ifdef NR_PERIPHERAL_COMPRESSION
	valid = valid && all(isfinite(proxy));
	residual = TransferCompressedResidual(source.rgb, proxy, residual);
#endif
	// Reconstruct only the matched change; the original image retains its fine detail.
	precise float3 candidate = source.rgb + residual * weight;
	valid = valid && all(isfinite(candidate));
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_WHOLE_PIXEL_REJECTION)
	if (OutputFormat == 1u)
		valid = valid && all(abs(candidate) <= 65504.0);
	else if (OutputFormat == 2u)
		valid = valid && all(candidate >= 0.0) && all(candidate <= float3(65024.0, 65024.0, 64512.0));
	else if (OutputFormat == 3u)
		valid = valid && all(candidate >= 0.0) && all(candidate <= 1.0);
#endif
	// Finite channel overflow saturates continuously; corrupt samples still retain the original pixel.
	if (OutputFormat == 1u)
		candidate = clamp(candidate, -65504.0, 65504.0);
	else if (OutputFormat == 2u)
		candidate = clamp(candidate, 0.0, float3(65024.0, 65024.0, 64512.0));
	else if (OutputFormat == 3u)
		candidate = saturate(candidate);
	float4 reconstructed = float4(valid ? candidate : source.rgb, source.a);
#ifdef MODEL_OUTPUT_SELECTION
	if (SelectedOutput != 0u) {
		if ((SelectionMode != 1u && SelectionMode != 3u) || selectionWeight >= 1.0) {
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
