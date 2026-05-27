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
	uint2 outputSize = uint2(OutputSize);
	if (dispatchThreadID.x >= outputSize.x || dispatchThreadID.y >= outputSize.y)
		return;

	float2 outputPixel = float2(dispatchThreadID.xy) + 0.5;
	float2 uv = outputPixel / OutputSize;
	float2 sourcePixel = uv * InputSize;
	float2 sourceMin = float2(0.5, 0.5);
	float2 sourceMax = InputSize - float2(0.5, 0.5);
	sourcePixel = clamp(sourcePixel, sourceMin, sourceMax);

	OutputTexture[dispatchThreadID.xy] = InputTexture.SampleLevel(LinearSampler, sourcePixel / SourceTextureSize, 0.0);
}
