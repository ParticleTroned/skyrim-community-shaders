#include "Common/WaterFinite.hlsli"

#			if defined(UNIFIED_WATER) && defined(DEPTH) && (!defined(VERTEX_ALPHA_DEPTH) || defined(VR)) && !defined(UNDERWATER) && !defined(LOD)
#				define UNIFIED_WATER_SHALLOW_FALLBACK

static const float UnifiedWaterFallbackCullFadeRange = 2048.0;
static const float UnifiedWaterDeepProbeMinRadiusPixels = 4.0;
static const float UnifiedWaterDeepProbeMaxRadiusPixels = 512.0;
static const float UnifiedWaterDeepProbeReliableRadiusRatio = 0.75;

int2 GetUnifiedWaterDepthRenderDimensions()
{
	return max(
		int2(1, 1),
		int2(round(
			SharedData::BufferDim.xy *
			FrameBuffer::DynamicResolutionParams1.xy)));
}

int2 ClampUnifiedWaterDepthPixel(
	int2 pixelPosition,
	uint eyeIndex,
	int2 renderDimensions)
{
#				if defined(VR)
	return Stereo::ClampToEyeBounds(pixelPosition, eyeIndex, renderDimensions);
#				else
	return clamp(pixelPosition, 0, renderDimensions - 1);
#				endif
}

bool IsUnifiedWaterDepthPixelInEyeBounds(
	int2 pixelPosition,
	uint eyeIndex,
	int2 renderDimensions)
{
	return all(
		pixelPosition ==
		ClampUnifiedWaterDepthPixel(
			pixelPosition,
			eyeIndex,
			renderDimensions));
}

float GetUnifiedWaterShallowFallbackAvailability(float viewDistance)
{
	float maximumDistance = max(
		SharedData::unifiedWaterSettings.ShallowFallbackMaxDistance,
		0.0);
	if (!WaterFinite::IsFinite(viewDistance) || !WaterFinite::IsFinite(maximumDistance) || maximumDistance <= 0.0)
		return 0.0;

	float fadeRange = min(
		UnifiedWaterFallbackCullFadeRange,
		0.25 * maximumDistance);
	float fadeStart = maximumDistance - fadeRange;
	return 1.0 - smoothstep(fadeStart, maximumDistance, viewDistance);
}

bool IsUnifiedWaterRawDepthValid(float rawDepth)
{
	return WaterFinite::IsFinite(rawDepth) &&
	       FrameBuffer::ToStandardDepth(rawDepth) > 1e-5 &&
	       FrameBuffer::ToStandardDepth(rawDepth) < 1.0 - 1e-5;
}

bool TryGetUnifiedWaterScenePosition(
	float2 logicalUV,
	float rawDepth,
	uint eyeIndex,
	out float3 scenePosition)
{
	scenePosition = 0.0.xxx;
	if (!IsUnifiedWaterRawDepthValid(rawDepth) || !all(WaterFinite::IsFinite(logicalUV)))
		return false;

#				if defined(VR)
	float2 clipPosition =
		Stereo::ConvertFromStereoUV(logicalUV, eyeIndex, 1) * 2.0 - 1.0;
#				else
	float2 clipPosition =
		(logicalUV * 2.0 - 1.0) * float2(1.0, -1.0);
#				endif
	float4 scenePositionH = mul(
		FrameBuffer::CameraViewProjInverse[eyeIndex],
		float4(clipPosition, rawDepth, 1.0));
	if (!all(WaterFinite::IsFinite(scenePositionH)) || abs(scenePositionH.w) <= 1e-6)
		return false;

	scenePosition = scenePositionH.xyz / scenePositionH.w;
	return all(WaterFinite::IsFinite(scenePosition));
}

#				if defined(VR)
bool TryGetUnifiedWaterVRColumn(
	float2 logicalUV,
	float sceneRawDepth,
	float waterSurfaceRawDepth,
	float3 waterSurfacePosition,
	uint eyeIndex,
	out float waterColumnDepthUnits)
{
	waterColumnDepthUnits = -1.0;
	if (
		!IsUnifiedWaterRawDepthValid(sceneRawDepth) ||
		!WaterFinite::IsFinite(waterSurfaceRawDepth) ||
		!all(WaterFinite::IsFinite(waterSurfacePosition)) ||
		FrameBuffer::IsNearerDepth(sceneRawDepth, waterSurfaceRawDepth)) {
		return false;
	}

	float3 scenePosition;
	if (!TryGetUnifiedWaterScenePosition(
			logicalUV,
			sceneRawDepth,
			eyeIndex,
			scenePosition)) {
		return false;
	}

	float3 waterPlaneNormal = ReflectPlane[eyeIndex].xyz;
	float waterPlaneNormalLength = length(waterPlaneNormal);
	if (!WaterFinite::IsFinite(waterPlaneNormalLength) || waterPlaneNormalLength <= 1e-6)
		return false;
	waterPlaneNormal /= waterPlaneNormalLength;

	waterColumnDepthUnits = abs(dot(
		scenePosition - waterSurfacePosition,
		waterPlaneNormal));
	return WaterFinite::IsFinite(waterColumnDepthUnits);
}
#				endif

bool TryGetUnifiedWaterProbeColumn(
	int2 samplePixel,
	uint eyeIndex,
	int2 renderDimensions,
	float3 waterSurfacePosition,
	float3 waterPlaneNormal,
	float belowPlaneSign,
	out float probeColumn)
{
	probeColumn = -1.0;
	if (!IsUnifiedWaterDepthPixelInEyeBounds(
			samplePixel,
			eyeIndex,
			renderDimensions)) {
		return false;
	}

	float rawDepth = DepthTex.Load(int3(samplePixel, 0)).x;
	float2 logicalUV =
		FrameBuffer::DynamicResolutionParams2.xy *
			(float2(samplePixel) + 0.5) *
			VPOSOffset.xy +
		VPOSOffset.zw;
	float3 scenePosition;
	if (!TryGetUnifiedWaterScenePosition(
			logicalUV,
			rawDepth,
			eyeIndex,
			scenePosition))
		return false;

	probeColumn =
		belowPlaneSign *
		dot(scenePosition - waterSurfacePosition, waterPlaneNormal);
	return WaterFinite::IsFinite(probeColumn) && probeColumn > 0.0;
}

float GetUnifiedWaterDeepContextWeight(
	float2 screenPosition,
	uint eyeIndex,
	float2 primaryLogicalUV,
	float primaryRawDepth,
	float3 waterSurfacePosition,
	float centerColumn,
	float2 columnGradient,
	float3 waterSurfaceDx,
	float3 waterSurfaceDy)
{
	if (!WaterFinite::IsFinite(centerColumn) || centerColumn < 0.0)
		return 0.0;

	float deepDepth = max(
		SharedData::unifiedWaterSettings.DeepContextDepthUnits,
		1.0);
	float transition = max(
		SharedData::unifiedWaterSettings.DeepContextTransitionUnits,
		0.0);
	float transitionStart = max(deepDepth - transition, 0.0);

	float maximumReach = max(
		SharedData::unifiedWaterSettings.DeepConnectionProbeReachUnits,
		0.0);
	if (
		maximumReach <= 0.0 ||
		!all(WaterFinite::IsFinite(columnGradient)) ||
		!all(WaterFinite::IsFinite(waterSurfaceDx)) ||
		!all(WaterFinite::IsFinite(waterSurfaceDy))) {
		return 0.0;
	}

	float gradientMagnitude = length(columnGradient);
	if (!WaterFinite::IsFinite(gradientMagnitude) || gradientMagnitude <= 1e-4)
		return 0.0;

	float2 probeDirection = columnGradient / gradientMagnitude;
	float3 directionalSurfaceStep =
		probeDirection.x * waterSurfaceDx +
		probeDirection.y * waterSurfaceDy;
	float surfaceUnitsPerPixel = length(directionalSurfaceStep);
	if (!WaterFinite::IsFinite(surfaceUnitsPerPixel) || surfaceUnitsPerPixel <= 1e-4)
		return 0.0;

	int2 renderDimensions = GetUnifiedWaterDepthRenderDimensions();
	float2 eyeRenderDimensions = float2(renderDimensions);
#				if defined(VR)
	eyeRenderDimensions.x *= 0.5;
#				endif
	float requestedRadiusPixels = maximumReach / surfaceUnitsPerPixel;
	float representableRadiusPixels = min(
		UnifiedWaterDeepProbeMaxRadiusPixels,
		length(eyeRenderDimensions));
	if (
		!WaterFinite::IsFinite(requestedRadiusPixels) ||
		requestedRadiusPixels < UnifiedWaterDeepProbeMinRadiusPixels ||
		representableRadiusPixels < UnifiedWaterDeepProbeMinRadiusPixels) {
		return 0.0;
	}

	float reliabilityFadeStartPixels =
		UnifiedWaterDeepProbeReliableRadiusRatio * representableRadiusPixels;
	float probeRepresentationConfidence =
		1.0 - smoothstep(
				  reliabilityFadeStartPixels,
				  representableRadiusPixels,
				  requestedRadiusPixels);

	float probeNearFieldConfidence = 1.0;
#				if defined(VR)
	float waterSurfaceViewDistance = length(waterSurfacePosition);
	if (!WaterFinite::IsFinite(waterSurfaceViewDistance))
		return 0.0;
	float nearFieldFadeStart = maximumReach;
	float nearFieldFadeEnd = max(
		2.0 * maximumReach,
		nearFieldFadeStart + 1.0);
	probeNearFieldConfidence = smoothstep(
		nearFieldFadeStart,
		nearFieldFadeEnd,
		waterSurfaceViewDistance);
#				endif

	float probeConfidence =
		probeRepresentationConfidence *
		probeNearFieldConfidence;
	float probeAuthority = saturate(probeConfidence);
	if (probeAuthority <= 1e-4)
		return 0.0;

	float radiusPixels = min(
		requestedRadiusPixels,
		representableRadiusPixels);

	int2 endpointOffset = int2(round(probeDirection * radiusPixels));
	int2 midpointOffset = int2(round(0.5 * float2(endpointOffset)));
	if (
		all(endpointOffset == 0) ||
		all(midpointOffset == 0) ||
		all(midpointOffset == endpointOffset)) {
		return 0.0;
	}

	int2 centerPixel = int2(screenPosition);
	int2 midpointPixel = centerPixel + midpointOffset;
	int2 endpointPixel = centerPixel + endpointOffset;
	if (!IsUnifiedWaterDepthPixelInEyeBounds(
			centerPixel,
			eyeIndex,
			renderDimensions)) {
		return 0.0;
	}

	float3 centerScenePosition;
	if (!TryGetUnifiedWaterScenePosition(
			primaryLogicalUV,
			primaryRawDepth,
			eyeIndex,
			centerScenePosition)) {
		return 0.0;
	}

	float3 waterPlaneNormal = ReflectPlane[eyeIndex].xyz;
	float waterPlaneNormalLength = length(waterPlaneNormal);
	if (!WaterFinite::IsFinite(waterPlaneNormalLength) || waterPlaneNormalLength <= 1e-6)
		return 0.0;
	waterPlaneNormal /= waterPlaneNormalLength;

	float centerPlaneOffset = dot(
		centerScenePosition - waterSurfacePosition,
		waterPlaneNormal);
	if (!WaterFinite::IsFinite(centerPlaneOffset) || abs(centerPlaneOffset) <= 1e-4)
		return 0.0;
	float belowPlaneSign = centerPlaneOffset < 0.0 ? -1.0 : 1.0;

	float midpointColumn = -1.0;
	bool midpointValid = false;
	if (IsUnifiedWaterDepthPixelInEyeBounds(
			midpointPixel,
			eyeIndex,
			renderDimensions)) {
		midpointValid = TryGetUnifiedWaterProbeColumn(
			midpointPixel,
			eyeIndex,
			renderDimensions,
			waterSurfacePosition,
			waterPlaneNormal,
			belowPlaneSign,
			midpointColumn);
	}

	float endpointColumn = -1.0;
	bool endpointValid = false;
	if (IsUnifiedWaterDepthPixelInEyeBounds(
			endpointPixel,
			eyeIndex,
			renderDimensions)) {
		endpointValid = TryGetUnifiedWaterProbeColumn(
			endpointPixel,
			eyeIndex,
			renderDimensions,
			waterSurfacePosition,
			waterPlaneNormal,
			belowPlaneSign,
			endpointColumn);
	}

	if (!midpointValid)
		return 0.0;

	float deepestConnectedColumn = midpointColumn;
	if (endpointValid)
		deepestConnectedColumn = max(deepestConnectedColumn, endpointColumn);

	float connectedDeepEvidence = saturate(
		transition > 1e-4 ?
			smoothstep(transitionStart, deepDepth, deepestConnectedColumn) :
			step(deepDepth, deepestConnectedColumn));
	return saturate(
		connectedDeepEvidence *
		probeAuthority);
}

float GetUnifiedWaterShoreContactWeight(
	float waterColumnDepthUnits,
	float blendRange,
	float depthFootprint)
{
	blendRange = max(blendRange, 0.0);
	float minimumFadePixels = max(
		SharedData::unifiedWaterSettings.ShoreContactMinFadePixels,
		0.0);
	if (
		!WaterFinite::IsFinite(waterColumnDepthUnits) ||
		!WaterFinite::IsFinite(blendRange) ||
		!WaterFinite::IsFinite(minimumFadePixels))
		return 0.0;
	float adaptiveRange = WaterFinite::IsFinite(depthFootprint) ?
	                          minimumFadePixels * max(depthFootprint, 0.0) :
	                          0.0;
	float effectiveRange = max(blendRange, adaptiveRange);
	if (effectiveRange <= 1e-4)
		return 1.0;

	return smoothstep(
		0.0,
		effectiveRange,
		max(waterColumnDepthUnits, 0.0));
}

float GetUnifiedWaterShallowSurfaceDepthWeight(float waterColumnDepthUnits)
{
	float depthRange = max(
		SharedData::unifiedWaterSettings.ShallowSurfaceDepthRangeUnits,
		1.0);
	if (
		!WaterFinite::IsFinite(waterColumnDepthUnits) ||
		waterColumnDepthUnits < 0.0 ||
		!WaterFinite::IsFinite(depthRange))
		return 0.0;

	float normalizedDepth = saturate(max(waterColumnDepthUnits, 0.0) / depthRange);
	float smootherDepth =
		normalizedDepth * normalizedDepth * normalizedDepth *
		(normalizedDepth * (normalizedDepth * 6.0 - 15.0) + 10.0);
	return 1.0 - smootherDepth;
}

#			endif
