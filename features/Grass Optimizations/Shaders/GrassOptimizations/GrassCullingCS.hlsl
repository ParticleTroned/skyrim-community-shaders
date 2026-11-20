#include "Common/DepthOrder.hlsli"

#include "Common/Math.hlsli"
#include "GrassOptimizations/GrassCullingBounds.hlsli"
#include "GrassOptimizations/GrassInstance.hlsli"
#include "GrassOptimizations/GrassOcclusion.hlsli"

#ifdef VR
#	define EYES 2
#else
#	define EYES 1
#endif
#include "GrassOptimizations/GrassCullingInputs.hlsli"

#ifdef VR
cbuffer StereoParameters : register(b1)
{
	float AlphaRef, StereoEnabled;
	float2 EyeOffsetScale;
	float4 EyeClipEdge[2];
};
#endif
struct Slice
{
	uint first, count, end;
	float previousPlacementDelta;
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
		GrassGeometry(WorldViewProj)[Eye(eye)][3] + GrassGeometry(WorldViewProj)[Eye(eye)][0], GrassGeometry(WorldViewProj)[Eye(eye)][3] - GrassGeometry(WorldViewProj)[Eye(eye)][0],
		GrassGeometry(WorldViewProj)[Eye(eye)][3] + GrassGeometry(WorldViewProj)[Eye(eye)][1], GrassGeometry(WorldViewProj)[Eye(eye)][3] - GrassGeometry(WorldViewProj)[Eye(eye)][1],
		GrassGeometry(WorldViewProj)[Eye(eye)][2], GrassGeometry(WorldViewProj)[Eye(eye)][3] - GrassGeometry(WorldViewProj)[Eye(eye)][2]
	};
	float distances[6] = { clip.w + clip.x, clip.w - clip.x, clip.w + clip.y, clip.w - clip.y, clip.z, clip.w - clip.z };
	[unroll] for (uint i = 0; i < 6; ++i) if (distances[i] < -radius * length(planes[i].xyz)) return true;
	return false;
}

bool OutsideCullingPlanes(float3 center, float radius, uint eye)
{
	bool outside = false;
	if (GrassFrustum(CullingPlanesValid)) {
		[unroll] for (uint i = 0; i < 6; ++i)
			outside = outside || dot(GrassFrustum(CullingPlanes)[Eye(eye) * 6 + i], float4(center, 1)) < -radius;
	} else
		outside = Outside(mul(GrassGeometry(WorldViewProj)[Eye(eye)], float4(center, 1)), radius, eye);
	return outside;
}
bool ProjectBounds(float3 center, float radius, uint eye, out float2 lo, out float2 hi, out float nearDepth)
{
	return GrassProjectSphere(GrassGeometry(WorldViewProj)[Eye(eye)], center, radius, lo, hi, nearDepth);
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
	if (GrassParameter(DepthMips) == 0 || any(lo < -1) || any(hi > 1) || !isfinite(nearest) || nearest < 0 || nearest > 1) {
		reason = 13;
		return false;
	}
	float2 uvLo = float2(lo.x * .5 + .5, .5 - hi.y * .5);
	float2 uvHi = float2(hi.x * .5 + .5, .5 - lo.y * .5);
	uvLo *= GrassParameter(DepthUVScale).xy;
	uvHi *= GrassParameter(DepthUVScale).xy;
	uvLo.x = (uvLo.x + eye) / GrassParameter(EyeCount);
	uvHi.x = (uvHi.x + eye) / GrassParameter(EyeCount);
	float2 pixels = (uvHi - uvLo) * float2(GrassParameter(DepthWidth), GrassParameter(DepthHeight));
	uint mip = min((uint)max(0, ceil(log2(max(1, max(pixels.x, pixels.y))))), GrassParameter(DepthMips) - 1);
	uint width, height, levels;
	DepthPyramid.GetDimensions(mip, width, height, levels);
	int2 first = clamp(int2(floor(uvLo * float2(width, height))), 0, int2(width, height) - 1);
	int2 last = clamp(int2(floor(uvHi * float2(width, height))), 0, int2(width, height) - 1);
	cells = uint(last.x - first.x + 1) * uint(last.y - first.y + 1);
	if (cells > 9) {
		reason = 14;
		return false;
	}
	float farthest = DepthOrder::Near();
	[loop] for (int y = first.y; y <= last.y; ++y)
		[loop] for (int x = first.x; x <= last.x; ++x)
			farthest = DepthOrder::Farthest(farthest, DepthPyramid.Load(int3(x, y, mip)));
	if (!isfinite(farthest)) {
		reason = 15;
		return false;
	}
	uint projectionEye = Eye(eye);
	float margin = GrassOcclusionMargin(GrassParameter(ProjectionAsymptote)[projectionEye], (GrassParameter(ProjectionModes) >> (projectionEye * 2)) & 3,
		nearest, farthest, GrassParameter(DepthBias), GrassParameter(DepthUVScale).z);
	bool hidden = DepthOrder::IsBehindWithBias(nearest, farthest, margin);
	[branch] if (!hidden && GrassParameter(RefineHiZ) && mip >= 2)
	{
		// Keep resource indices valid for inactive lanes in a divergent wave.
		uint fineMip = mip - min(mip, 2u);
		uint fineWidth, fineHeight, fineLevels;
		DepthPyramid.GetDimensions(fineMip, fineWidth, fineHeight, fineLevels);
		int eyeFirst = int(eye * (fineWidth / GrassParameter(EyeCount)));
		int eyeLast = eyeFirst + int(fineWidth / GrassParameter(EyeCount)) - 1;
		int2 fineFirst = clamp(int2(floor(uvLo * float2(fineWidth, fineHeight))), int2(eyeFirst, 0), int2(eyeLast, fineHeight - 1));
		int2 fineLast = clamp(int2(floor(uvHi * float2(fineWidth, fineHeight))), int2(eyeFirst, 0), int2(eyeLast, fineHeight - 1));
		uint fineCells = uint(fineLast.x - fineFirst.x + 1) * uint(fineLast.y - fineFirst.y + 1);
		if (fineCells <= 25) {
			Count(20);
			hidden = true;
			[loop] for (int y = fineFirst.y; y <= fineLast.y && hidden; ++y)
				[loop] for (int x = fineFirst.x; x <= fineLast.x; ++x)
			{
				float depth = DepthPyramid.Load(int3(x, y, fineMip));
				++cells;
				float fineMargin = GrassOcclusionMargin(GrassParameter(ProjectionAsymptote)[projectionEye], (GrassParameter(ProjectionModes) >> (projectionEye * 2)) & 3,
					nearest, depth, GrassParameter(DepthBias), GrassParameter(DepthUVScale).z);
				if (!isfinite(depth) || !DepthOrder::IsBehindWithBias(nearest, depth, fineMargin)) {
					hidden = false;
					break;
				}
			}
			if (hidden)
				Count(21);
		}
	}
	if (!hidden && abs(farthest - DepthOrder::Far()) <= 1e-6)
		reason = 16;
	else if (!hidden)
		reason = 17;
	return hidden;
}
uint FindSlice(uint index)
{
	uint left = 0, right = GrassParameter(SliceCount);
	[loop] while (left < right)
	{
		uint middle = (left + right) / 2;
		if (index < Slices[GrassParameter(SliceOffset) + middle].end)
			right = middle;
		else
			left = middle + 1;
	}
	return left;
}

uint CullInstance(uint index, uint commonSlice, out uint sourceRecord, out uint tier, out GrassInstanceExtra extras[EYES])
{
	sourceRecord = 0;
	tier = 0;
	[unroll] for (uint initialEye = 0; initialEye < EYES; ++initialEye)
		extras[initialEye] = (GrassInstanceExtra)0;
	uint left = commonSlice < GrassParameter(SliceCount) ? commonSlice : FindSlice(index);
	if (left >= GrassParameter(SliceCount))
		return 0;
	Slice slice = Slices[GrassParameter(SliceOffset) + left];
	sourceRecord = slice.first + index - (slice.end - slice.count);
	uint4 a = Records.Load4((GrassRecordBase + sourceRecord) * 32);
	uint4 b = Records.Load4((GrassRecordBase + sourceRecord) * 32 + 16);
	float4 instance1 = float4(UnpackGrassInstance(a.x), UnpackGrassInstance(a.y));
	float4 instance2 = float4(UnpackGrassInstance(a.z), UnpackGrassInstance(a.w));
	float4 instance3 = float4(UnpackGrassInstance(b.x), UnpackGrassInstance(b.y));
	float4 instance4 = float4(UnpackGrassInstance(b.z), UnpackGrassInstance(b.w));
	float3 scale = instance4.y * GrassGeometry(ScaleMask) + 1;
	float3x3 rotation = float3x3(instance2.xyz, instance3.xyz, float3(instance4.x, instance2.w, instance3.w));
	float3 center = instance1.xyz + mul(rotation, GrassParameter(ModelBound).xyz * scale) + slice.originFade.xyz - GrassParameter(RepresentativeOrigin).xyz;
	float norm = GrassBasisNorm(rotation);
	float radius = GrassParameter(ModelBound).w * max(abs(scale.x), max(abs(scale.y), abs(scale.z))) * norm;
	bool valid = all(isfinite(center)) && isfinite(radius) && radius > 0;
	float3 root = instance1.xyz + slice.originFade.xyz - GrassParameter(RepresentativeOrigin).xyz;
	float rootDistance = length(mul(GrassGeometry(World)[0], float4(root, 1)).xyz);
	float reach = (length(GrassParameter(ModelBound).xyz) + GrassParameter(ModelBound).w) * max(abs(scale.x), max(abs(scale.y), abs(scale.z))) * norm;
	float deformation = .4 * abs(GrassGeometry(WindVector).z) * length(GrassGeometry(WindVector).xy);
	if (GrassParameter(CollisionEnabled) && GrassParameter(CollisionDistance) > 0 && (!isfinite(rootDistance) || rootDistance <= GrassParameter(CollisionDistance) + reach))
		deformation += 2 * reach * max(1, GrassParameter(CollisionStrength));
	radius += deformation;
	valid = valid && isfinite(radius);
	float pixels = 1e20;
	float distance = 0;
	if (valid) {
		pixels = 0;
		[unroll] for (uint eye = 0; eye < EYES; ++eye)
		{
			float eyeDistance = length(mul(GrassGeometry(World)[Eye(eye)], float4(root, 1)).xyz);
			distance = eye == 0 ? eyeDistance : min(distance, eyeDistance);
			float focal = length(GrassGeometry(WorldViewProj)[Eye(eye)][1].xyz) * GrassParameter(ViewHeight) * .5;
			pixels = max(pixels, GrassParameter(QualityRadius) * max(1, 1 + instance4.y) * focal / max(eyeDistance, 1e-4));
		}
	}
	uint seed = Hash(asuint(instance1.x + slice.originFade.x) ^ Hash(asuint(instance1.y + slice.originFade.y)) ^ a.y);
	float densityHash = (seed & 0xffffff) / 16777216.0;
	float lodHash = (Hash(seed ^ 0x9e3779b9) & 0xffffff) / 16777216.0;
	if (valid && GrassParameter(MidEnabled) && pixels < GrassParameter(MidPixels) + (lodHash - .5) * GrassParameter(BandPixels))
		tier = 1;
	if (valid && GrassParameter(FarEnabled) && pixels < GrassParameter(FarPixels) + (lodHash - .5) * GrassParameter(BandPixels))
		tier = 2;
	float meshWeight = GrassParameter(MeshWeights)[tier];
	float costBias = GrassParameter(MeshCostBias) * saturate((distance - GrassParameter(CostBiasStartDistance)) / max(GrassParameter(CostBiasStartDistance), 1e-4));
	float pixelScale = lerp(1, meshWeight, costBias);
	float maximumDistance = GrassParameter(RenderDistance) * lerp(1, 1 / max(meshWeight, 1), costBias);
	float keep, minimumPixels;
	bool thin;
	bool farPolicy = GrassParameter(FarPolicyEnabled) && tier == 2;
	[branch] if (farPolicy)
	{
		minimumPixels = GrassParameter(FarDensity).x * pixelScale;
		// The validated interval stays positive without widening tightly spaced size limits.
		float detail = saturate((pixels - minimumPixels) / ((GrassParameter(FarPixels) - GrassParameter(FarDensity).x) * pixelScale));
		keep = lerp(GrassParameter(FarDensity).y, GrassParameter(FarDensity).z, detail) * GrassParameter(FarDensity).w;
		thin = true;
	}
	else
	{
		minimumPixels = GrassParameter(MinPixels) * pixelScale;
		keep = lerp(GrassParameter(MinDensity), 1, saturate((pixels - minimumPixels) / max((GrassParameter(FullPixels) - GrassParameter(MinPixels)) * pixelScale, .01)));
		thin = pixels < GrassParameter(FullPixels) * pixelScale;
	}
	float densityFade;
	// Keep the constant-width path free of a per-instance variable division.
	[branch] if (GrassParameter(ProportionalFade))
	{
		float fadeBand = .15 * keep;
		densityFade = saturate((keep + fadeBand - densityHash) / max(fadeBand, 1e-6));
	}
	else densityFade = saturate((keep + .15 - densityHash) / .15);
	densityFade = GrassParameter(DensityEnabled) && thin ? densityFade : 1;
	bool densityReject = valid && GrassParameter(DensityEnabled) && (pixels < minimumPixels || (farPolicy && keep <= 0) || densityFade <= 0);
	uint eyeMask = 0;
	bool windComputed = false;
	float2 wind = 0;
	[unroll] for (uint eye = 0; eye < EYES; ++eye)
	{
#ifdef VR
		if (StereoEnabled == 0 && eye != 0)
			continue;
#endif
		Count(0);
		if (valid && GrassParameter(FrustumEnabled) && OutsideCullingPlanes(center, radius, eye)) {
			Count(1);
			continue;
		}
		if (densityReject) {
			Count(2);
			continue;
		}
		float fade = slice.originFade.w * densityFade;
		if (valid && GrassParameter(RenderDistance) > 0) {
			if (distance > maximumDistance) {
				Count(8);
				continue;
			}
			float nativeDistance;
#ifdef VR
			nativeDistance = length(mul(GrassGeometry(World)[0], float4(root, 1)).xyz);
#else
			nativeDistance = length(mul(GrassGeometry(WorldViewProj)[0], float4(root, 1)).xyz);
#endif
			float distanceFade = 1 - saturate((nativeDistance - GrassGeometry(AlphaParam1)) / max(GrassGeometry(AlphaParam2), 1e-4));
			if (GrassParameter(OverrideDistance))
				distanceFade = 1;
			fade *= distanceFade * saturate((maximumDistance - distance) / max(maximumDistance * (1 - GrassParameter(EdgeFadeStart)), 1e-4));
		}
		if (valid && fade <= GrassParameter(InvisibleFadeCull)) {
			Count(9);
			continue;
		}
		// Normalized texture alpha cannot make an instance more opaque than its fade.
		if (!DisableAlphaRejection && valid && isfinite(fade) && fade + 1e-5 < GrassParameter(AlphaThreshold)) {
			Count(19);
			continue;
		}
		if (valid) {
			Count(10);
			if (GrassParameter(DepthMips)) {
				float2 lo, hi;
				float nearest;
				if (ProjectBounds(center, radius, eye, lo, hi, nearest)) {
					uint reason, cells;
					bool hidden = Hidden(lo, hi, nearest, eye, reason, cells);
					CountN(18, cells);
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
		bool simple = valid && GrassParameter(SimpleShadingPixelSize) > 0 && pixels < GrassParameter(SimpleShadingPixelSize);
		[branch] if (!windComputed)
		{
			wind = float2(GrassWindScalar(instance1.xy, GrassGeometry(WindTimer)), GrassWindScalar(instance1.xy, GrassGeometry(PreviousWindTimer)));
			windComputed = true;
		}
		GrassInstanceExtra extra;
		extra.originFade = float4(slice.originFade.xyz, simple ? -fade : fade);
		extra.wind = wind;
		extra.previousPlacementDelta = slice.previousPlacementDelta;
		extras[eye] = extra;
		eyeMask |= 1u << eye;
		Count(4 + tier);
	}
	return eyeMask;
}

groupshared uint CommonSlice;
groupshared uint BinCounts[3 * EYES];
groupshared uint BinOffsets[3 * EYES];

[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
#ifdef GRASS_CULL_BATCH
	uint dispatchIndex = group.y * DispatchRowWidth + group.x;
	// This exit is uniform across the group and precedes every group barrier.
	if (dispatchIndex >= DispatchGroupCount)
		return;
	uint dispatchGroup = DispatchStartGroup + dispatchIndex;
	uint firstBucket = 0, lastBucket = BucketCount - 1;
	[loop] while (firstBucket < lastBucket)
	{
		uint middle = (firstBucket + lastBucket + 1) / 2;
		if (CullBuckets[middle].DispatchStart <= dispatchGroup)
			firstBucket = middle;
		else
			lastBucket = middle - 1;
	}
	Bucket = CullBuckets[firstBucket];
	uint3 id = uint3((dispatchGroup - Bucket.DispatchStart) * 64 + lane, 0, 0);
#else
	uint3 id = uint3(group.x * 64 + lane, 0, 0);
#endif
	if (lane == 0) {
		CommonSlice = FindSlice(id.x);
		if (CommonSlice < GrassParameter(SliceCount) && Slices[GrassParameter(SliceOffset) + CommonSlice].end < min(id.x + 64, GrassParameter(InstanceCount)))
			CommonSlice = GrassParameter(SliceCount);
	}
	if (lane < 3 * EYES)
		BinCounts[lane] = 0;
	if (id.x < 3 * GrassParameter(EyeCount))
		Arguments.Store(GrassArgsBase + id.x * 32 + 12, GrassParameter(IndexCounts)[id.x / GrassParameter(EyeCount)]);
	GroupMemoryBarrierWithGroupSync();

	uint sourceRecord = 0;
	uint tier = 0, eyeMask = 0;
	GrassInstanceExtra extras[EYES];
	if (id.x < GrassParameter(InstanceCount))
		eyeMask = CullInstance(id.x, CommonSlice, sourceRecord, tier, extras);
	uint localSlots[EYES];
	[unroll] for (uint reserveEye = 0; reserveEye < EYES; ++reserveEye) if (eyeMask & (1u << reserveEye))
		InterlockedAdd(BinCounts[tier * GrassParameter(EyeCount) + reserveEye], 1, localSlots[reserveEye]);
	GroupMemoryBarrierWithGroupSync();

	if (lane < 3 * EYES && BinCounts[lane])
		Arguments.InterlockedAdd(GrassArgsBase + lane * 32 + 16, BinCounts[lane], BinOffsets[lane]);
	GroupMemoryBarrierWithGroupSync();

	[unroll] for (uint eye = 0; eye < EYES; ++eye) if (eyeMask & (1u << eye))
	{
		uint bin = tier * GrassParameter(EyeCount) + eye;
		uint output = GrassOutputBase + bin * GrassParameter(Capacity) + BinOffsets[bin] + localSlots[eye];
		// Source indices fit exactly in the float input-assembler stream.
		Compacted.Store(output * 4, asuint(float(GrassRecordBase + sourceRecord)));
		Extras[output] = extras[eye];
	}
}
