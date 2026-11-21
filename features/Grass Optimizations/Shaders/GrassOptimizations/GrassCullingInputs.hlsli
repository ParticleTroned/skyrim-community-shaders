#ifndef GRASS_CULLING_INPUTS_HLSLI
#define GRASS_CULLING_INPUTS_HLSLI

#ifdef GRASS_CULL_BATCH
struct GrassParameters
#else
cbuffer Parameters : register(b0)
#endif
{
	uint InstanceCount, SliceCount, Capacity, EyeCount;
	uint FrustumEnabled, DensityEnabled, MidEnabled, FarEnabled;
	float MinPixels, FullPixels, MinDensity, BandPixels;
	float MidPixels, FarPixels, DepthBias, ViewHeight;
	float4 RepresentativeOrigin;
	float4 ModelBound;
	uint DepthWidth, DepthHeight, DepthMips, CollisionEnabled;
	float4 DepthUVScale;  // xy: active viewport scale; z: absolute depth-range slack.
	float MeshCostBias, CostBiasStartDistance, InvisibleFadeCull, RenderDistance;
	float EdgeFadeStart, SimpleShadingPixelSize, QualityRadius;
	uint ProjectionModes;
	uint OverrideDistance;
	float CollisionDistance;
	uint SliceOffset;
	float CollisionStrength;
	uint4 IndexCounts;
	float4 MeshWeights;
	float4 FarDensity;  // Minimum pixels, minimum/maximum density, authored-bound area scale.
	uint FarPolicyEnabled, ProportionalFade;
	float2 ProjectionAsymptote;
	float AlphaThreshold;
	uint RefineHiZ;
	float OcclusionTightness;
	uint CullingPadding;
};

#ifdef GRASS_CULL_BATCH
struct GrassFrustumParameters
#else
cbuffer FrustumParameters : register(b3)
#endif
{
	float4 CullingPlanes[12];
	uint CullingPlanesValid;
#ifdef GRASS_CULL_BATCH
	uint ProjectionModes;
	uint2 CullingPlanesPadding;
#else
	uint3 CullingPlanesPadding;
#endif
};

#ifdef GRASS_CULL_BATCH
struct GrassCullGeometry
{
	row_major float4x4 WorldViewProj[2];
	row_major float4x4 World[2];
	float3 WindVector;
	float WindTimer;
	float3 ScaleMask;
	float PreviousWindTimer;
	float AlphaParam1, AlphaParam2;
	float2 ProjectionAsymptote;
};
#else
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
#endif

#ifdef GRASS_CULL_BATCH
struct GrassCullBucket
{
	uint DispatchStart, RecordBase, OutputBase, ArgsBase;
	uint InstanceCount, SliceCount, Capacity, SliceOffset;
	float4 RepresentativeOrigin, ModelBound;
	uint4 IndexCounts;
	float4 MeshWeights;
	float QualityRadius, FarCoverage, AlphaThreshold;
	uint Flags;
	GrassCullGeometry geometry;
	GrassFrustumParameters frustum;
};
StructuredBuffer<GrassCullBucket> CullBuckets : register(t3);
cbuffer CullFrameParameters : register(b2)
{
	GrassParameters FrameParameters;
};
cbuffer DispatchParameters : register(b0)
{
	uint BucketCount, DispatchStartGroup, DispatchGroupCount, DispatchRowWidth;
	uint DisableAlphaRejection;
	uint3 DispatchPadding;
};
static GrassCullBucket Bucket;
static GrassParameters ActiveParameters;

void LoadGrassBucket(uint index)
{
	Bucket = CullBuckets[index];
	ActiveParameters = FrameParameters;
	ActiveParameters.InstanceCount = Bucket.InstanceCount;
	ActiveParameters.SliceCount = Bucket.SliceCount;
	ActiveParameters.Capacity = Bucket.Capacity;
	ActiveParameters.SliceOffset = Bucket.SliceOffset;
	ActiveParameters.RepresentativeOrigin = Bucket.RepresentativeOrigin;
	ActiveParameters.ModelBound = Bucket.ModelBound;
	ActiveParameters.IndexCounts = Bucket.IndexCounts;
	ActiveParameters.MeshWeights = Bucket.MeshWeights;
	ActiveParameters.QualityRadius = Bucket.QualityRadius;
	ActiveParameters.FarDensity.w = Bucket.FarCoverage;
	ActiveParameters.AlphaThreshold = Bucket.AlphaThreshold;
	ActiveParameters.MidEnabled = Bucket.Flags & 1u;
	ActiveParameters.FarEnabled = (Bucket.Flags >> 1) & 1u;
	ActiveParameters.FarPolicyEnabled = (Bucket.Flags >> 2) & 1u;
	ActiveParameters.ProjectionModes = Bucket.frustum.ProjectionModes;
	ActiveParameters.ProjectionAsymptote = Bucket.geometry.ProjectionAsymptote;
}

#	define GrassParameter(field) ActiveParameters.field
#	define GrassGeometry(field) Bucket.geometry.field
#	define GrassFrustum(field) Bucket.frustum.field
#	define GrassRecordBase Bucket.RecordBase
#	define GrassOutputBase Bucket.OutputBase
#	define GrassArgsBase Bucket.ArgsBase
#	define GrassRecordStep ((Bucket.Flags & 8u) ? 1u : 2u)
#else
#	define DisableAlphaRejection 0
#	define GrassParameter(field) field
#	define GrassGeometry(field) field
#	define GrassFrustum(field) field
#	define GrassRecordBase 0
#	define GrassOutputBase 0
#	define GrassArgsBase 0
#	define GrassRecordStep 2u
#endif

#endif
