// Each eye has private ownership maps; presentation fills never enter scene history.
cbuffer Refresh : register(b0)
{
	row_major float4x4 SourceClipToView;
	row_major float4x4 SourceViewToTargetView;
	row_major float4x4 TargetViewToClip;
	row_major float4x4 TargetClipToSourceSkyClip;
	uint2 EyeSize;
	uint EyeIndex;
	uint ReverseDepth;
	float2 DepthUvOffset;
	float2 Padding;
};

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);
RWTexture2D<uint> NearestDistance : register(u0);
RWTexture2D<uint> SourceOwner : register(u1);
RWTexture2D<float4> RefreshedColor : register(u2);

int2 DepthPixel(float2 uv)
{
	int2 pixel = clamp(int2((uv + DepthUvOffset) * EyeSize), 0, int2(EyeSize) - 1);
	pixel.x += EyeIndex * EyeSize.x;
	return pixel;
}

bool Project(uint2 pixel, out int2 target, out uint distance)
{
	float2 uv = (float2(pixel) + 0.5) / float2(EyeSize);
	float depth = SceneDepth.Load(int3(DepthPixel(uv), 0));
	target = 0;
	distance = 0xffffffff;
	if (!isfinite(depth) || depth < 0 || depth > 1 || (ReverseDepth ? depth <= 0.000001 : depth >= 0.999999))
		return false;
	float4 view = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), SourceClipToView);
	if (!all(isfinite(view)) || abs(view.w) < 1e-8)
		return false;
	view /= view.w;
	float4 destinationView = mul(view, SourceViewToTargetView);
	float4 clip = mul(destinationView, TargetViewToClip);
	if (!all(isfinite(clip)) || clip.w <= 1e-6 || clip.z < 0 || clip.z > clip.w)
		return false;
	float2 destinationUv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
	if (any(destinationUv < 0) || any(destinationUv >= 1))
		return false;
	target = int2(destinationUv * EyeSize);
	distance = asuint(abs(destinationView.z));
	return true;
}

[numthreads(8, 8, 1)] void SelectDepth(uint3 dispatch : SV_DispatchThreadID) {
	if (any(dispatch.xy >= EyeSize))
		return;
	int2 target;
	uint distance;
	if (Project(dispatch.xy, target, distance))
		InterlockedMin(NearestDistance[target], distance);
}

	[numthreads(8, 8, 1)] void SelectSource(uint3 dispatch : SV_DispatchThreadID)
{
	if (any(dispatch.xy >= EyeSize))
		return;
	int2 target;
	uint distance;
	if (Project(dispatch.xy, target, distance) && NearestDistance[target] == distance)
		InterlockedMin(SourceOwner[target], dispatch.y * EyeSize.x + dispatch.x);
}

float4 ReadOwner(uint owner)
{
	return SceneColor.Load(int3(owner % EyeSize.x + EyeIndex * EyeSize.x, owner / EyeSize.x, 0));
}

int2 AngularSource(uint2 pixel)
{
	float2 uv = (float2(pixel) + 0.5) / EyeSize;
	float4 source = mul(float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1), TargetClipToSourceSkyClip);
	float2 sourceUv = uv;
	if (all(isfinite(source)) && source.w > 1e-6)
		sourceUv = source.xy / source.w * float2(0.5, -0.5) + 0.5;
	int2 sourcePixel = clamp(int2(saturate(sourceUv) * EyeSize), 0, int2(EyeSize) - 1);
	sourcePixel.x += EyeIndex * EyeSize.x;
	return sourcePixel;
}

[numthreads(8, 8, 1)] void ResolveColor(uint3 dispatch : SV_DispatchThreadID) {
	uint2 pixel = dispatch.xy;
	if (any(pixel >= EyeSize))
		return;
	uint owner = SourceOwner[pixel];
	if (owner != 0xffffffff) {
		RefreshedColor[pixel] = ReadOwner(owner);
		return;
	}
	int2 angularPixel = AngularSource(pixel);
	float2 angularUv = (float2(angularPixel - int2(EyeIndex * EyeSize.x, 0)) + 0.5) / EyeSize;
	float angularDepth = SceneDepth.Load(int3(DepthPixel(angularUv), 0));
	if (!isfinite(angularDepth) || (ReverseDepth ? angularDepth <= 0.000001 : angularDepth >= 0.999999)) {
		RefreshedColor[pixel] = SceneColor.Load(int3(angularPixel, 0));
		return;
	}
	// Prefer background at a small disocclusion; never pull pixels across eyes.
	uint farthest = 0;
	[unroll] for (int y = -1; y <= 1; ++y)
	{
		[unroll] for (int x = -1; x <= 1; ++x)
		{
			int2 neighbor = int2(pixel) + int2(x, y);
			if (any(neighbor < 0) || any(neighbor >= int2(EyeSize)))
				continue;
			uint candidate = SourceOwner[neighbor];
			uint distance = NearestDistance[neighbor];
			if (candidate != 0xffffffff && distance > farthest) {
				owner = candidate;
				farthest = distance;
			}
		}
	}
	RefreshedColor[pixel] = owner == 0xffffffff ? SceneColor.Load(int3(angularPixel, 0)) : ReadOwner(owner);
}
