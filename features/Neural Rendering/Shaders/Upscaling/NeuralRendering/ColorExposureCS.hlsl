// Identical raw pairs or positive x == y at every texel prove a scalar ratio.
// The host validates one visible mip and non-border, non-comparison sampling.
Texture2D<float4> EngineAverage : register(t0);
RWTexture2D<float4> ExposureSnapshot : register(u0);

float4 ExposureValue(float2 average)
{
	float ratio = 1.0;
	float validity = 0.0;
	if (all(isfinite(average)) && all(average >= 0.0)) {
		if (average.x == 0.0 || average.y == 0.0) {
			// ISHDR leaves input unchanged here; distinguish this from a measured 1.
			validity = 2.0;
		} else {
			ratio = average.x == average.y ? 1.0 : average.y / average.x;
			if (isfinite(ratio) && ratio >= (1.0 / 256.0) && ratio <= 256.0)
				validity = 1.0;
		}
	}
	return float4(average, isfinite(ratio) ? ratio : 1.0, validity);
}

[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	uint width, height, levels;
	EngineAverage.GetDimensions(0, width, height, levels);
	[unroll] for (uint i = 0; i < 5; ++i)
		ExposureSnapshot[uint2(i, 0)] = float4(0, 0, 1, 0);
	if (levels != 1 || !((width == 1 && height == 1) || (width == 2 && height == 2)))
		return;
	float2 first = EngineAverage.Load(int3(0, 0, 0)).xy;
	bool uniformValues = true;
	bool unitRatio = true;
	bool finite = true;
	[unroll] for (uint y = 0; y < 2; ++y)
	{
		[unroll] for (uint x = 0; x < 2; ++x)
		{
			if (x >= width || y >= height)
				continue;
			float2 value = EngineAverage.Load(int3(x, y, 0)).xy;
			finite = finite && all(isfinite(value));
			uniformValues = uniformValues && all(value == first);
			unitRatio = unitRatio && value.x > 0.0 && value.x == value.y;
			ExposureSnapshot[uint2(1 + y * width + x, 0)] = ExposureValue(value);
		}
	}
	// Filtering preserves equal positive channels even when brightness varies.
	ExposureSnapshot[uint2(0, 0)] = !finite       ? float4(0, 0, 1, 0) :
	                                unitRatio     ? float4(first, 1, 1) :
	                                uniformValues ? ExposureValue(first) :
	                                                float4(0, 0, 1, 3);
}
