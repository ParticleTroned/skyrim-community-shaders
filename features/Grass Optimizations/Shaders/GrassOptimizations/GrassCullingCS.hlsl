#include "Common/Math.hlsli"
#include "GrassOptimizations/GrassInstance.hlsli"

#ifdef VR
#	define EYES 2
#else
#	define EYES 1
#endif
cbuffer Parameters : register(b0)
{
	uint InstanceCount, SliceCount, Capacity, EyeCount;
	uint FrustumEnabled, DensityEnabled, MidEnabled, FarEnabled;
	float MinPixels, FullPixels, MinDensity, BandPixels;
	float MidPixels, FarPixels, DepthBias, ViewHeight;
	float4 RepresentativeOrigin;
	float4 ModelBound;
	uint DepthWidth, DepthHeight, DepthMips, CollisionEnabled;
	float4 DepthUVScale;
	float MeshCostBias, CostBiasStartDistance, InvisibleFadeCull, RenderDistance;
	float EdgeFadeStart, SimpleShadingPixelSize, QualityRadius, MeshWeight;
	uint OverrideDistance;
	float CollisionDistance;
	uint Padding1, Padding2;
};

cbuffer FrustumParameters : register(b3)
{
	float4 CullingPlanes[12];
	uint CullingPlanesValid;
	uint3 CullingPlanesPadding;
};
#ifdef VR
cbuffer StereoParameters : register(b1)
{
	float AlphaRef, StereoEnabled;
	float2 EyeOffsetScale;
	float4 EyeClipEdge[2];
};
#endif
cbuffer NativeGeometry : register(b2)
{
	row_major float4x4 WorldViewProj[EYES];
	row_major float4x4 WorldView[EYES];
	row_major float4x4 World[EYES];
	row_major float4x4 PreviousWorld[EYES];
	float4 FogNearColor;
	float3 WindVector;
	float WindTimer;
	float3 DirLightDirection;
	float PreviousWindTimer;
	float3 DirLightColor;
	float AlphaParam1;
	float3 AmbientColor;
	float AlphaParam2;
	float3 ScaleMask;
	float ShadowClampValue;
};
struct Slice
{
	uint first, count, end, padding;
	float4 originFade;
};
ByteAddressBuffer Records : register(t0);
StructuredBuffer<Slice> Slices : register(t1);
Texture2D<float> DepthPyramid : register(t2);
RWByteAddressBuffer Compacted : register(u0);
RWStructuredBuffer<GrassInstanceExtra> Extras : register(u1);
RWByteAddressBuffer Arguments : register(u2);
#ifdef GRASS_DIAGNOSTICS
RWByteAddressBuffer Counters : register(u3);
#endif
void Count(uint index)
{
#ifdef GRASS_DIAGNOSTICS
	uint unused = 0;
	Counters.InterlockedAdd(index * 4, 1, unused);
#endif
}
void CountN(uint index, uint amount)
{
#ifdef GRASS_DIAGNOSTICS
	uint unused = 0;
	Counters.InterlockedAdd(index * 4, amount, unused);
#endif
}
float2 Unpack(uint value) { return float2(f16tof32(value), f16tof32(value >> 16)); }
uint Hash(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352d;
	x ^= x >> 15;
	x *= 0x846ca68b;
	return x ^ (x >> 16);
}
uint Eye(uint eye)
{
#ifdef VR
	return StereoEnabled == 0 ? 0 : eye;
#else
	return eye;
#endif
}
bool Outside(float4 clip, float radius, uint eye)
{
	float4 planes[6] = {
		WorldViewProj[Eye(eye)][3] + WorldViewProj[Eye(eye)][0], WorldViewProj[Eye(eye)][3] - WorldViewProj[Eye(eye)][0],
		WorldViewProj[Eye(eye)][3] + WorldViewProj[Eye(eye)][1], WorldViewProj[Eye(eye)][3] - WorldViewProj[Eye(eye)][1],
		WorldViewProj[Eye(eye)][2], WorldViewProj[Eye(eye)][3] - WorldViewProj[Eye(eye)][2]
	};
	float distances[6] = { clip.w + clip.x, clip.w - clip.x, clip.w + clip.y, clip.w - clip.y, clip.z, clip.w - clip.z };
	[unroll] for (uint i = 0; i < 6; ++i) if (distances[i] < -radius * length(planes[i].xyz)) return true;
	return false;
}

bool OutsideCullingPlanes(float3 center, float radius, uint eye)
{
	bool outside = false;
	if (CullingPlanesValid) {
		[unroll] for (uint i = 0; i < 6; ++i)
			outside = outside || dot(CullingPlanes[Eye(eye) * 6 + i], float4(center, 1)) < -radius;
	} else
		outside = Outside(mul(WorldViewProj[Eye(eye)], float4(center, 1)), radius, eye);
	return outside;
}
// A cube enclosing the displaced sphere bounds every perspective extremum.
bool ProjectBounds(float3 center, float radius, uint eye, out float2 lo, out float2 hi, out float nearDepth)
{
	lo = 1e20;
	hi = -1e20;
	nearDepth = 1;
	[unroll] for (uint i = 0; i < 8; ++i)
	{
		float3 sign = float3((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1);
		float4 clip = mul(WorldViewProj[Eye(eye)], float4(center + sign * radius, 1));
		if (!all(isfinite(clip)) || clip.w <= 1e-5 || clip.z <= 0)
			return false;
		float3 ndc = clip.xyz / clip.w;
		lo = min(lo, ndc.xy);
		hi = max(hi, ndc.xy);
		nearDepth = min(nearDepth, ndc.z);
	}
	return true;
}
bool Hidden(float2 lo, float2 hi, float nearest, uint eye, out uint reason, out uint cells)
{
	reason = 0;
	cells = 0;
#ifdef VR
	if (StereoEnabled != 1 || any(EyeOffsetScale != float2(-.5, .5))) {
		reason = 13;
		return false;
	}
#endif
	if (DepthMips == 0 || any(lo < -1) || any(hi > 1) || nearest <= 0 || nearest > 1) {
		reason = 13;
		return false;
	}
	float2 uvLo = float2(lo.x * .5 + .5, .5 - hi.y * .5);
	float2 uvHi = float2(hi.x * .5 + .5, .5 - lo.y * .5);
	uvLo *= DepthUVScale.xy;
	uvHi *= DepthUVScale.xy;
	uvLo.x = (uvLo.x + eye) / EyeCount;
	uvHi.x = (uvHi.x + eye) / EyeCount;
	float2 pixels = (uvHi - uvLo) * float2(DepthWidth, DepthHeight);
	uint mip = min((uint)max(0, ceil(log2(max(1, max(pixels.x, pixels.y))))), DepthMips - 1);
	uint width, height, levels;
	DepthPyramid.GetDimensions(mip, width, height, levels);
	int2 first = clamp(int2(floor(uvLo * float2(width, height))), 0, int2(width, height) - 1);
	int2 last = clamp(int2(floor(uvHi * float2(width, height))), 0, int2(width, height) - 1);
	cells = uint(last.x - first.x + 1) * uint(last.y - first.y + 1);
	float farthest = 0;
	[loop] for (int y = first.y; y <= last.y; ++y)
		[loop] for (int x = first.x; x <= last.x; ++x)
			farthest = max(farthest, DepthPyramid.Load(int3(x, y, mip)));
	if (!isfinite(farthest)) {
		reason = 15;
		return false;
	}
	if (farthest >= 1 - 1e-6)
		reason = 16;
	else if (nearest <= farthest + DepthBias)
		reason = 17;
	return nearest > farthest + DepthBias;
}
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (id.x >= InstanceCount)
		return;
	uint left = 0, right = SliceCount;
	[loop] while (left < right)
	{
		uint middle = (left + right) / 2;
		if (id.x < Slices[middle].end)
			right = middle;
		else
			left = middle + 1;
	}
	if (left >= SliceCount)
		return;
	Slice slice = Slices[left];
	uint record = slice.first + id.x - (slice.end - slice.count);
	uint4 a = Records.Load4(record * 32), b = Records.Load4(record * 32 + 16);
	float4 instance1 = float4(Unpack(a.x), Unpack(a.y));
	float4 instance2 = float4(Unpack(a.z), Unpack(a.w));
	float4 instance3 = float4(Unpack(b.x), Unpack(b.y));
	float4 instance4 = float4(Unpack(b.z), Unpack(b.w));
	float3 scale = instance4.y * ScaleMask + 1;
	float3x3 rotation = float3x3(instance2.xyz, instance3.xyz, float3(instance4.x, instance2.w, instance3.w));
	float3 center = instance1.xyz + mul(rotation, ModelBound.xyz * scale) + slice.originFade.xyz - RepresentativeOrigin.xyz;
	float norm = sqrt(dot(rotation[0], rotation[0]) + dot(rotation[1], rotation[1]) + dot(rotation[2], rotation[2]));
	float radius = ModelBound.w * max(abs(scale.x), max(abs(scale.y), abs(scale.z))) * norm;
	bool valid = all(isfinite(center)) && isfinite(radius) && radius > 0;
	float3 root = instance1.xyz + slice.originFade.xyz - RepresentativeOrigin.xyz;
	float rootDistance = length(mul(World[0], float4(root, 1)).xyz);
	float reach = (length(ModelBound.xyz) + ModelBound.w) * max(abs(scale.x), max(abs(scale.y), abs(scale.z))) * norm;
	float deformation = .4 * abs(WindVector.z) * length(WindVector.xy);
	if (CollisionEnabled && CollisionDistance > 0 && (!isfinite(rootDistance) || rootDistance <= CollisionDistance + reach))
		deformation += 2 * reach;
	radius += deformation;
	valid = valid && isfinite(radius);
	float pixels = 1e20;
	float distance = 0;
	if (valid) {
		pixels = 0;
		[unroll] for (uint eye = 0; eye < EYES; ++eye)
		{
			float eyeDistance = length(mul(World[Eye(eye)], float4(root, 1)).xyz);
			distance = eye == 0 ? eyeDistance : min(distance, eyeDistance);
			float focal = length(WorldViewProj[Eye(eye)][1].xyz) * ViewHeight * .5;
			pixels = max(pixels, QualityRadius * max(1, 1 + instance4.y) * focal / max(eyeDistance, 1e-4));
		}
	}
	uint seed = Hash(asuint(instance1.x + slice.originFade.x) ^ Hash(asuint(instance1.y + slice.originFade.y)) ^ a.y);
	float densityHash = (seed & 0xffffff) / 16777216.0;
	float lodHash = (Hash(seed ^ 0x9e3779b9) & 0xffffff) / 16777216.0;
	float costBias = MeshCostBias * saturate((distance - CostBiasStartDistance) / max(CostBiasStartDistance, 1e-4));
	float pixelScale = lerp(1, MeshWeight, costBias);
	float maximumDistance = RenderDistance * lerp(1, 1 / max(MeshWeight, 1), costBias);
	float keep = lerp(MinDensity, 1, saturate((pixels - MinPixels * pixelScale) / max((FullPixels - MinPixels) * pixelScale, .01)));
	float densityFade = DensityEnabled && pixels < FullPixels * pixelScale ? saturate((keep + .15 - densityHash) / .15) : 1;
	bool densityReject = valid && DensityEnabled && (pixels < MinPixels * pixelScale || densityFade <= 0);
	uint tier = 0;
	if (valid && MidEnabled && pixels < MidPixels + (lodHash - .5) * BandPixels)
		tier = 1;
	if (valid && FarEnabled && pixels < FarPixels + (lodHash - .5) * BandPixels)
		tier = 2;
	bool windComputed = false;
	float2 wind = 0;
	[unroll] for (uint eye = 0; eye < EYES; ++eye)
	{
#ifdef VR
		if (StereoEnabled == 0 && eye != 0)
			continue;
#endif
		Count(0);
		if (valid && FrustumEnabled && OutsideCullingPlanes(center, radius, eye)) {
			Count(1);
			continue;
		}
		if (densityReject) {
			Count(2);
			continue;
		}
		float fade = slice.originFade.w * densityFade;
		if (valid && RenderDistance > 0) {
			if (distance > maximumDistance) {
				Count(8);
				continue;
			}
			float nativeDistance;
#ifdef VR
			nativeDistance = length(mul(World[0], float4(root, 1)).xyz);
#else
			nativeDistance = length(mul(WorldViewProj[0], float4(root, 1)).xyz);
#endif
			float distanceFade = 1 - saturate((nativeDistance - AlphaParam1) / max(AlphaParam2, 1e-4));
			if (OverrideDistance)
				distanceFade = 1;
			fade *= distanceFade * saturate((maximumDistance - distance) / max(maximumDistance * (1 - EdgeFadeStart), 1e-4));
		}
		if (valid && fade <= InvisibleFadeCull) {
			Count(9);
			continue;
		}
		if (valid) {
			Count(10);
			if (DepthMips) {
				float2 lo, hi;
				float nearest;
				if (ProjectBounds(center, radius, eye, lo, hi, nearest)) {
					uint reason, cells;
					bool hidden = Hidden(lo, hi, nearest, eye, reason, cells);
					CountN(18, cells);
					if (cells > 9)
						Count(14);
					if (reason)
						Count(reason);
					if (hidden) {
						Count(3);
						continue;
					}
				} else
					Count(12);
			} else
				Count(11);
		}
		if (!valid)
			Count(7);
		uint slot;
		Arguments.InterlockedAdd((tier * EyeCount + eye) * 32 + 16, 1, slot);
		uint output = (tier * EyeCount + eye) * Capacity + slot;
		Compacted.Store4(output * 32, a);
		Compacted.Store4(output * 32 + 16, b);
		bool simple = valid && SimpleShadingPixelSize > 0 && pixels < SimpleShadingPixelSize;
		[branch] if (!windComputed)
		{
			wind = float2(GrassWindScalar(instance1.xy, WindTimer), GrassWindScalar(instance1.xy, PreviousWindTimer));
			windComputed = true;
		}
		GrassInstanceExtra extra;
		extra.originFade = float4(slice.originFade.xyz, simple ? -fade : fade);
		extra.wind = wind;
		Extras[output] = extra;
		Count(4 + tier);
	}
}
