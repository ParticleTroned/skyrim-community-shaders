SamplerState LinearSampler : register(s0);
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer DynamicResolutionStretchCB : register(b0)
{
	float2 InputSize;
	float2 OutputSize;
	float2 SourceTextureSize;
	float2 Padding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	const float2 zero = float2(0.0, 0.0);
	if (any(InputSize <= zero) || any(OutputSize <= zero) || any(SourceTextureSize <= zero))
		return;

	uint2 outputSize = uint2(OutputSize);
	if (dispatchThreadID.x >= outputSize.x || dispatchThreadID.y >= outputSize.y)
		return;

	float2 safeInputSize = max(InputSize, float2(1.0, 1.0));
	float2 safeOutputSize = max(OutputSize, float2(1.0, 1.0));
	float2 safeSourceTextureSize = max(SourceTextureSize, float2(1.0, 1.0));
	float2 validInputSize = min(safeInputSize, safeSourceTextureSize);

	float2 outputPixel = float2(dispatchThreadID.xy) + 0.5;
	float2 uv = outputPixel / safeOutputSize;
	float2 sourcePixel = uv * safeInputSize;
	float2 sourceMin = float2(0.5, 0.5);
	float2 sourceMax = max(validInputSize - float2(0.5, 0.5), sourceMin);
	sourcePixel = clamp(sourcePixel, sourceMin, sourceMax);

	OutputTexture[dispatchThreadID.xy] = InputTexture.SampleLevel(LinearSampler, sourcePixel / safeSourceTextureSize, 0.0);
}
