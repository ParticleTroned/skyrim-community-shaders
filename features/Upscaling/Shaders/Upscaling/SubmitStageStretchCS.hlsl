SamplerState LinearSampler : register(s0);
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer DynamicResolutionStretchCB : register(b0)
{
	float2 InputSize;
	float2 OutputSize;
	float2 SourceTextureSize;
	float2 SourceOffset;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
	const float2 zero = float2(0.0, 0.0);
	if (any(InputSize <= zero) || any(OutputSize <= zero) || any(SourceTextureSize <= zero))
		return;

	uint2 outputSize = uint2(OutputSize);
	if (dispatchThreadID.x >= outputSize.x || dispatchThreadID.y >= outputSize.y)
		return;

	float2 safeInputSize = max(InputSize, float2(1.0, 1.0));
	float2 safeOutputSize = max(OutputSize, float2(1.0, 1.0));
	float2 safeSourceTextureSize = max(SourceTextureSize, float2(1.0, 1.0));
	float2 safeSourceOffset = clamp(
		SourceOffset,
		zero,
		max(safeSourceTextureSize - float2(1.0, 1.0), zero));
	float2 validInputSize = min(
		safeInputSize,
		max(safeSourceTextureSize - safeSourceOffset, float2(1.0, 1.0)));

	if (all(abs(InputSize - OutputSize) < float2(0.5, 0.5))) {
		uint2 sourcePixel = uint2(safeSourceOffset) + dispatchThreadID.xy;
		uint2 sourceMax = uint2(safeSourceOffset + validInputSize - float2(1.0, 1.0));
		OutputTexture[dispatchThreadID.xy] = InputTexture.Load(int3(min(sourcePixel, sourceMax), 0));
		return;
	}

	float2 outputPixel = float2(dispatchThreadID.xy) + 0.5;
	float2 uv = outputPixel / safeOutputSize;
	float2 sourcePixel = safeSourceOffset + uv * safeInputSize;
	float2 sourceMin = safeSourceOffset + float2(0.5, 0.5);
	float2 sourceMax = max(
		safeSourceOffset + validInputSize - float2(0.5, 0.5),
		sourceMin);
	sourcePixel = clamp(sourcePixel, sourceMin, sourceMax);

	OutputTexture[dispatchThreadID.xy] = InputTexture.SampleLevel(LinearSampler, sourcePixel / safeSourceTextureSize, 0.0);
}
