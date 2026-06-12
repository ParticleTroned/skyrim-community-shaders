cbuffer AAVRSRefinementCB : register(b0)
{
	float4 RenderInfo;  // xy=render dim, zw=1/render dim
	float4 RateInfo;    // xy=rate image dim, zw=VRS tile size
	float4 Thresholds;  // x=luma range, y=bright luma, z=motion pixels, w=depth range
};

Texture2D<float4> SceneColor : register(t0);
Texture2D<float2> MotionVectors : register(t1);
Texture2D<float> SceneDepth : register(t2);
RWTexture2D<uint> RateImage : register(u0);

static const uint kRateIndex1x1 = 0;
static const float kSkyDepthThreshold = 1e-6;

float Luma(float3 color)
{
	return dot(max(color, 0.0), float3(0.2126, 0.7152, 0.0722));
}

uint2 UVToPixel(float2 uv, uint2 dim)
{
	return min((uint2)(saturate(uv) * (float2)dim), dim - 1u);
}

void AccumulateSample(
	float2 renderPixel,
	uint2 renderDim,
	uint2 colorDim,
	uint2 motionDim,
	uint2 depthDim,
	inout float minLuma,
	inout float maxLuma,
	inout float minDepth,
	inout float maxDepth,
	inout float maxMotionPixels,
	inout bool hasSceneDepth)
{
	const float2 uv = (clamp(renderPixel, float2(0.0, 0.0), (float2)renderDim - 1.0) + 0.5) / (float2)renderDim;
	const uint2 colorPixel = UVToPixel(uv, colorDim);
	const uint2 motionPixel = UVToPixel(uv, motionDim);
	const uint2 depthPixel = UVToPixel(uv, depthDim);

	const float sampleLuma = Luma(SceneColor.Load(int3(colorPixel, 0)).rgb);
	minLuma = min(minLuma, sampleLuma);
	maxLuma = max(maxLuma, sampleLuma);

	const float depth = SceneDepth.Load(int3(depthPixel, 0));
	if (depth > kSkyDepthThreshold) {
		hasSceneDepth = true;
		minDepth = min(minDepth, depth);
		maxDepth = max(maxDepth, depth);
	}

	const float2 motion = MotionVectors.Load(int3(motionPixel, 0));
	maxMotionPixels = max(maxMotionPixels, length(motion * RenderInfo.xy));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	const uint2 rateDim = (uint2)(RateInfo.xy + 0.5);
	if (any(dispatchID.xy >= rateDim))
		return;

	const uint2 renderDim = uint2(max((uint)(RenderInfo.x + 0.5), 1u), max((uint)(RenderInfo.y + 0.5), 1u));
	const float2 tileSize = max(RateInfo.zw, float2(1.0, 1.0));
	const float2 tileMin = (float2)dispatchID.xy * tileSize;
	const float2 tileMax = min(tileMin + tileSize, (float2)renderDim - 1.0);
	const float2 tileCenter = (tileMin + tileMax) * 0.5;

	uint colorWidth = 0;
	uint colorHeight = 0;
	uint motionWidth = 0;
	uint motionHeight = 0;
	uint depthWidth = 0;
	uint depthHeight = 0;
	SceneColor.GetDimensions(colorWidth, colorHeight);
	MotionVectors.GetDimensions(motionWidth, motionHeight);
	SceneDepth.GetDimensions(depthWidth, depthHeight);
	const uint2 colorDim = uint2(max(colorWidth, 1u), max(colorHeight, 1u));
	const uint2 motionDim = uint2(max(motionWidth, 1u), max(motionHeight, 1u));
	const uint2 depthDim = uint2(max(depthWidth, 1u), max(depthHeight, 1u));

	float minLuma = 1e20;
	float maxLuma = 0.0;
	float minDepth = 1.0;
	float maxDepth = 0.0;
	float maxMotionPixels = 0.0;
	bool hasSceneDepth = false;

	AccumulateSample(tileCenter, renderDim, colorDim, motionDim, depthDim, minLuma, maxLuma, minDepth, maxDepth, maxMotionPixels, hasSceneDepth);
	AccumulateSample(tileMin, renderDim, colorDim, motionDim, depthDim, minLuma, maxLuma, minDepth, maxDepth, maxMotionPixels, hasSceneDepth);
	AccumulateSample(float2(tileMax.x, tileMin.y), renderDim, colorDim, motionDim, depthDim, minLuma, maxLuma, minDepth, maxDepth, maxMotionPixels, hasSceneDepth);
	AccumulateSample(float2(tileMin.x, tileMax.y), renderDim, colorDim, motionDim, depthDim, minLuma, maxLuma, minDepth, maxDepth, maxMotionPixels, hasSceneDepth);
	AccumulateSample(tileMax, renderDim, colorDim, motionDim, depthDim, minLuma, maxLuma, minDepth, maxDepth, maxMotionPixels, hasSceneDepth);

	const float lumaRange = maxLuma - minLuma;
	const float depthRange = hasSceneDepth ? maxDepth - minDepth : 0.0;
	const bool forceFullRate =
		lumaRange >= Thresholds.x ||
		maxLuma >= Thresholds.y ||
		maxMotionPixels >= Thresholds.z ||
		depthRange >= Thresholds.w;

	if (forceFullRate) {
		RateImage[dispatchID.xy] = kRateIndex1x1;
		return;
	}
}
