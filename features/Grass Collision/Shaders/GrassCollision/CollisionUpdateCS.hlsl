#include "Common/PerformanceCostReference.hlsli"
#include "GrassCollision/GrassCollisionContact.hlsli"

cbuffer PerFrameCB : register(b0)
{
	float2 PosOffset;
	uint2 ArrayOrigin;
	int2 ValidMargin;
	float TimeDelta;
	uint BoundingBoxCount;
	float GrassInteractionRadius;
	float CollisionStrength;
	float SpringStrength;
	float Damping;
	float MaximumBend;
	float MaximumCompression;
	float CompressionRecovery;
	float HeightOffset;
}

struct BoundingBoxPacked
{
	float2 MinExtent;
	float2 MaxExtent;
	uint IndexStart;
	uint IndexEnd;
	float2 pad0;
};

StructuredBuffer<BoundingBoxPacked> CollisionBoundingBoxes : register(t0);

struct CollisionShapePacked
{
	float4 CurrentPointAAndRadius;
	float4 CurrentPointB;
	float4 PreviousPointA;
	float4 PreviousPointB;
};

StructuredBuffer<CollisionShapePacked> CollisionInstances : register(t1);
Texture2D<float4> PreviousDeformation : register(t2);
Texture2D<float4> PreviousVelocity : register(t3);
RWTexture2D<float4> Deformation : register(u0);
RWTexture2D<float4> Velocity : register(u1);

groupshared BoundingBoxPacked SharedBoundingBoxes[64];
groupshared uint SharedBoundingBoxCount;
groupshared float4 SharedTileBounds;

// Negative prefers the candidate; zero leaves only the impulse-direction tie break.
int CompareContact(float2 bounds, float compression, float2 selectedBounds, float selectedCompression)
{
	if (bounds.x != selectedBounds.x)
		return bounds.x < selectedBounds.x ? -1 : 1;
	if (bounds.y != selectedBounds.y)
		return bounds.y < selectedBounds.y ? -1 : 1;
	return compression > selectedCompression ? -1 : (compression < selectedCompression ? 1 : 0);
}

[numthreads(8, 8, 1)] void main(
	uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex, uint3 groupID : SV_GroupID) {
	const uint TEXTURE_SIZE = 1024;
	const float WORLD_SIZE = 8192.0;
	const float COLLISION_ACCELERATION = 50.0;
	uint boundingBoxCount = BoundingBoxCount;
#if !defined(GRASS_COLLISION_TEST_UNFILTERED) && !(CSX_PERF_REFERENCE_FLAGS & CSX_PERF_GRASS_TILE_FILTER)
	// Direct scans avoid compaction for short actor lists.
	if (BoundingBoxCount > 4) {
		if (groupIndex == 0) {
			SharedBoundingBoxCount = 0;
			uint2 firstCell = (groupID.xy * 8 + TEXTURE_SIZE - ArrayOrigin) % TEXTURE_SIZE;
			uint2 lastCell = (firstCell + 7) % TEXTURE_SIZE;
			// A tile crossing the ring seam conservatively spans the full logical axis.
			float2 minimumCell = float2(firstCell.x <= lastCell.x ? firstCell.x : 0,
				firstCell.y <= lastCell.y ? firstCell.y : 0);
			float2 maximumCell = float2(firstCell.x <= lastCell.x ? lastCell.x : TEXTURE_SIZE - 1,
				firstCell.y <= lastCell.y ? lastCell.y : TEXTURE_SIZE - 1);
			SharedTileBounds.xy = (minimumCell + 0.5 - TEXTURE_SIZE * 0.5) / TEXTURE_SIZE * WORLD_SIZE + PosOffset;
			SharedTileBounds.zw = (maximumCell + 0.5 - TEXTURE_SIZE * 0.5) / TEXTURE_SIZE * WORLD_SIZE + PosOffset;
		}
		GroupMemoryBarrierWithGroupSync();
		if (groupIndex < BoundingBoxCount) {
			BoundingBoxPacked box = CollisionBoundingBoxes[groupIndex];
			if (all(SharedTileBounds.zw >= box.MinExtent) && all(SharedTileBounds.xy <= box.MaxExtent)) {
				uint index;
				InterlockedAdd(SharedBoundingBoxCount, 1, index);
				SharedBoundingBoxes[index] = box;
			}
		}
		GroupMemoryBarrierWithGroupSync();
		boundingBoxCount = SharedBoundingBoxCount;
	} else
#endif
	{
		if (groupIndex < BoundingBoxCount)
			SharedBoundingBoxes[groupIndex] = CollisionBoundingBoxes[groupIndex];
		GroupMemoryBarrierWithGroupSync();
	}
	int2 cellID = int2((dispatchThreadId.xy + TEXTURE_SIZE - ArrayOrigin) % TEXTURE_SIZE);
	float2 cellCentreMS = float2(cellID) + 0.5 - TEXTURE_SIZE * 0.5;
	cellCentreMS = cellCentreMS / TEXTURE_SIZE * WORLD_SIZE + PosOffset;

	int2 validMin = max(int2(0, 0), ValidMargin);
	int2 validMax = int2(TEXTURE_SIZE - 1, TEXTURE_SIZE - 1) + min(int2(0, 0), ValidMargin);
	bool isValid = all(cellID >= validMin) && all(cellID <= validMax);

	float2 bend = 0.0;
	float2 velocity = 0.0;
	float compression = 0.0;
	float2 contactBounds = 0.0;
	float idleUpdates = 1;
	bool staleHistory = false;
	bool hasHistory = false;
	if (isValid) {
		float4 previousState = PreviousDeformation.Load(int3(dispatchThreadId.xy, 0));
		float4 previousMotion = PreviousVelocity.Load(int3(dispatchThreadId.xy, 0));
		contactBounds = previousMotion.zw;
		hasHistory = previousState.w > 0 && all(isfinite(previousState)) && all(isfinite(previousMotion.xy)) &&
		             GrassCollisionContact::IsValid(contactBounds);
		if (hasHistory) {
			bend = previousState.xy;
			compression = previousState.z;
			velocity = previousMotion.xy;
			idleUpdates = min(previousState.w, GrassCollisionContact::MaximumIdleUpdates);
			staleHistory = idleUpdates >= GrassCollisionContact::MaximumIdleUpdates;
		}
	}

	float deltaTime = clamp(TimeDelta, 0.0, 1.0 / 15.0);
	if (hasHistory && deltaTime > 0)
		idleUpdates = min(idleUpdates + 1, GrassCollisionContact::MaximumIdleUpdates);
	float2 collisionAcceleration = 0.0;
	float collisionCompression = 0.0;
	float2 impactBounds = 0.0;
	bool hasImpact = false;
	for (uint i = 0; i < boundingBoxCount; ++i) {
		BoundingBoxPacked boundingBox = SharedBoundingBoxes[i];
		if (all(cellCentreMS >= boundingBox.MinExtent && cellCentreMS <= boundingBox.MaxExtent)) {
			for (uint j = boundingBox.IndexStart; j < boundingBox.IndexEnd; ++j) {
				CollisionShapePacked collider = CollisionInstances[j];
				float2 currentA = collider.CurrentPointAAndRadius.xy;
				float2 currentB = collider.CurrentPointB.xy;
				float2 previousA = collider.PreviousPointA.xy;
				float2 previousB = collider.PreviousPointB.xy;
				float2 currentCentre = (currentA + currentB) * 0.5;
				float2 previousCentre = (previousA + previousB) * 0.5;
				float2 movement = currentCentre - previousCentre;
				float sweepLengthSquared = dot(movement, movement);
				float projectedHalfLength = 0.5 * max(length(currentB - currentA), length(previousB - previousA));
				float radius = collider.CurrentPointAAndRadius.w + projectedHalfLength + max(GrassInteractionRadius, 0.0);
				float lineFraction = sweepLengthSquared > 1e-6 ? dot(cellCentreMS - previousCentre, movement) / sweepLengthSquared : 1;
				float2 closestPoint = lerp(previousCentre, currentCentre, saturate(lineFraction));
				float2 delta = cellCentreMS - closestPoint;
				float distanceToSweep = length(delta);
				float penetration = radius - distanceToSweep;
				if (penetration > 0.0 && deltaTime > 0.0) {
					float2 contactFractions = float2(0, 1);
					if (sweepLengthSquared > 1e-6) {
						float2 lineDelta = cellCentreMS - (previousCentre + movement * lineFraction);
						float halfInterval = sqrt(max(radius * radius - dot(lineDelta, lineDelta), 0) / sweepLengthSquared);
						contactFractions = saturate(lineFraction + float2(-halfInterval, halfInterval));
					}
					float2 previousHeights = float2(collider.PreviousPointA.z, collider.PreviousPointB.z);
					float2 currentHeights = float2(collider.CurrentPointAAndRadius.z, collider.CurrentPointB.z);
					float2 firstHeights = lerp(previousHeights, currentHeights, contactFractions.x);
					float2 lastHeights = lerp(previousHeights, currentHeights, contactFractions.y);
					float2 bounds = float2(min(min(firstHeights.x, firstHeights.y), min(lastHeights.x, lastHeights.y)),
										max(max(firstHeights.x, firstHeights.y), max(lastHeights.x, lastHeights.y))) +
					                HeightOffset + collider.CurrentPointAAndRadius.w * float2(-1, 1);
					if (!GrassCollisionContact::IsValid(bounds))
						continue;
					float penetrationRatio = saturate(penetration / max(radius, 1e-4));
					int priority = hasImpact ? CompareContact(bounds, penetrationRatio, impactBounds, collisionCompression) : -1;
					if (priority > 0)
						continue;
					float2 fallbackDirection = sweepLengthSquared > 1e-6 ?
					                               normalize(float2(-movement.y, movement.x)) :
					                               float2(1.0, 0.0);
					float2 outwardDirection = distanceToSweep > 1e-4 ? delta / distanceToSweep : fallbackDirection;
					float2 impulse = outwardDirection * penetrationRatio;
					// One dominant lowest contact avoids order-dependent unions and another field traversal.
					if (priority == 0 && (impulse.x > collisionAcceleration.x ||
											 (impulse.x == collisionAcceleration.x && impulse.y >= collisionAcceleration.y)))
						continue;
					impactBounds = bounds;
					collisionAcceleration = impulse;
					collisionCompression = penetrationRatio;
					hasImpact = true;
				}
			}
		}
	}

	// Recent lower history survives higher contacts; stale half-float history can yield when needed.
	if (hasImpact && hasHistory && contactBounds.y < impactBounds.x && !staleHistory)
		hasImpact = false;
	if (hasImpact) {
		if (!hasHistory || !GrassCollisionContact::Overlaps(contactBounds, impactBounds)) {
			bend = velocity = 0;
			compression = 0;
		}
		if (!hasHistory || impactBounds.x < contactBounds.x || !GrassCollisionContact::Overlaps(contactBounds, impactBounds))
			contactBounds = GrassCollisionContact::ConservativeHalfBounds(impactBounds);
		idleUpdates = 1;
	} else if (!hasHistory) {
		contactBounds = 0;
	}
	if (!hasImpact) {
		collisionAcceleration = 0;
		collisionCompression = 0;
	}

	collisionAcceleration *= max(CollisionStrength, 0.0) * COLLISION_ACCELERATION;
	float springStrength = max(SpringStrength, 0.0);
	float springDenominator = 1.0 + max(Damping, 0.0) * deltaTime + springStrength * deltaTime * deltaTime;
	float2 updatedVelocity = (velocity + collisionAcceleration * deltaTime - bend * springStrength * deltaTime) / springDenominator;
	float2 updatedBend = bend + updatedVelocity * deltaTime;
	velocity = updatedVelocity;
	bend = updatedBend;

	float maximumBend = max(MaximumBend, 0.0);
	float bendMagnitude = length(bend);
	if (bendMagnitude > maximumBend && bendMagnitude > 1e-6) {
		float2 bendDirection = bend / bendMagnitude;
		bend = bendDirection * maximumBend;
		velocity -= bendDirection * max(dot(velocity, bendDirection), 0.0);
	}

	float updatedCompression = compression * exp(-max(CompressionRecovery, 0.0) * deltaTime);
	updatedCompression = max(updatedCompression, collisionCompression * max(MaximumCompression, 0.0));
	compression = updatedCompression;
	compression = clamp(compression, 0.0, max(MaximumCompression, 0.0));
	// Half-float recovery must retire imperceptible residuals before they pin a contact layer.
	const float RECOVERY_EPSILON = 1e-4;
	bool active = any(abs(bend) > RECOVERY_EPSILON) || any(abs(velocity) > RECOVERY_EPSILON) || compression > RECOVERY_EPSILON;
	if (!active && deltaTime > 0) {
		bend = velocity = 0;
		compression = 0;
	} else
		active = any(bend != 0) || any(velocity != 0) || compression > 0;
	Deformation[dispatchThreadId.xy] = float4(bend, compression, active ? idleUpdates : 0.0);
	Velocity[dispatchThreadId.xy] = float4(velocity, active ? contactBounds : float2(0, 0));
}
