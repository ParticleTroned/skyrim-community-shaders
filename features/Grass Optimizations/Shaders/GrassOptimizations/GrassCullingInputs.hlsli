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
	uint HiZOptions, CullingPadding;
};

#ifdef GRASS_CULL_BATCH
struct GrassFrustumParameters
#else
cbuffer FrustumParameters : register(b3)
#endif
{
	float4 CullingPlanes[12];
	uint CullingPlanesValid;
	uint3 CullingPlanesPadding;
};

#ifdef GRASS_CULL_BATCH
struct GrassNativeGeometry
#else
cbuffer NativeGeometry : register(b2)
#endif
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
#ifdef GRASS_CULL_BATCH
#	ifndef VR
	float4 GeometryPadding[16];
#	endif
#endif
};

#ifdef GRASS_CULL_BATCH
struct GrassCullBucket
{
	uint DispatchStart, RecordBase, OutputBase, ArgsBase;
	GrassParameters parameters;
	GrassNativeGeometry geometry;
	GrassFrustumParameters frustum;
};
StructuredBuffer<GrassCullBucket> CullBuckets : register(t3);
cbuffer DispatchParameters : register(b0)
{
	uint BucketCount, DispatchStartGroup, DispatchGroupCount, DispatchRowWidth;
	uint DisableAlphaRejection;
	uint3 DispatchPadding;
};
static GrassCullBucket Bucket;
#	define GrassParameter(field) Bucket.parameters.field
#	define GrassGeometry(field) Bucket.geometry.field
#	define GrassFrustum(field) Bucket.frustum.field
#	define GrassRecordBase Bucket.RecordBase
#	define GrassOutputBase Bucket.OutputBase
#	define GrassArgsBase Bucket.ArgsBase
#else
#	define DisableAlphaRejection 0
#	define GrassParameter(field) field
#	define GrassGeometry(field) field
#	define GrassFrustum(field) field
#	define GrassRecordBase 0
#	define GrassOutputBase 0
#	define GrassArgsBase 0
#endif

#endif
