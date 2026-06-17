cbuffer LumaSharpenConfig : register(b0)
{
	float sharpness;
	float limit;
	float2 pad;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Dest : register(u0);

static const float3 LUMA = float3(0.2126, 0.7152, 0.0722);
static const float EPSILON = 1.0e-4;

float3 LoadSourceClamped(int2 pixel, int2 maxPixel)
{
	return Source.Load(int3(clamp(pixel, int2(0, 0), maxPixel), 0)).rgb;
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	uint2 texDim;
	Dest.GetDimensions(texDim.x, texDim.y);

	if (DTid.x >= texDim.x || DTid.y >= texDim.y)
		return;

	int2 sp = int2(DTid.xy);
	int2 maxPixel = int2(texDim) - int2(1, 1);
	float4 center = Source.Load(int3(sp, 0));
	float3 north = LoadSourceClamped(sp + int2(0, -1), maxPixel);
	float3 south = LoadSourceClamped(sp + int2(0, 1), maxPixel);
	float3 west = LoadSourceClamped(sp + int2(-1, 0), maxPixel);
	float3 east = LoadSourceClamped(sp + int2(1, 0), maxPixel);

	float centerLuma = dot(center.rgb, LUMA);
	float northLuma = dot(north, LUMA);
	float southLuma = dot(south, LUMA);
	float westLuma = dot(west, LUMA);
	float eastLuma = dot(east, LUMA);

	float blurLuma = (centerLuma + northLuma + southLuma + westLuma + eastLuma) * 0.2;
	float detail = centerLuma - blurLuma;

	float localMin = min(centerLuma, min(min(northLuma, southLuma), min(westLuma, eastLuma)));
	float localMax = max(centerLuma, max(max(northLuma, southLuma), max(westLuma, eastLuma)));
	float range = max(localMax - localMin, EPSILON);
	float limitedDetail = clamp(detail * sharpness, -range * limit, range * limit);
	float targetLuma = centerLuma + limitedDetail;

	float3 sharpened = center.rgb;
	if (abs(centerLuma) > EPSILON) {
		sharpened *= targetLuma / centerLuma;
	}

	Dest[DTid.xy] = float4(sharpened, center.a);
}
