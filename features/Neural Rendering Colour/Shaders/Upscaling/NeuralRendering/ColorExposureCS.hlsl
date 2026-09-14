// Capture the actual multiplier used by ISHDR.hlsl / BLEND, not a guessed
// brightness meter or DLSS-internal exposure. The host admits a 1x1 AvgTex view.
Texture2D<float4> EngineAverage : register(t0);
RWTexture2D<float4> ExposureSnapshot : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	float2 average = EngineAverage.Load(int3(0, 0, 0)).xy;
	float ratio = 1.0;
	float validity = 0.0;
	if (all(isfinite(average))) {
		if (average.x == 0.0 || average.y == 0.0) {
			// ISHDR leaves input unchanged here; distinguish this from a measured 1.
			validity = 2.0;
		} else {
			ratio = average.y / average.x;
			if (isfinite(ratio) && ratio >= (1.0 / 256.0) && ratio <= 256.0)
				validity = 1.0;
		}
	}
	ExposureSnapshot[uint2(0, 0)] = float4(average, isfinite(ratio) ? ratio : 1.0, validity);
}
