#include "ActorUtils.h"
#include <array>
#include <cmath>
#include <limits>

namespace
{
	RE::NiPoint3 ReadHavokPoint(const RE::hkVector4& point)
	{
		alignas(16) float components[4];
		_mm_store_ps(components, point.quad);
		return { components[0], components[1], components[2] };
	}

	bool IsFinitePoint(const RE::NiPoint3& point)
	{
		return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
	}

	RE::NiPoint3 TransformHavokPoint(const RE::hkTransform& transform, const RE::hkVector4& point, float worldScale)
	{
		const auto localPoint = ReadHavokPoint(point);
		__m128 worldPoint = transform.translation.quad;
		worldPoint = _mm_add_ps(worldPoint, _mm_mul_ps(transform.rotation.col0.quad, _mm_set1_ps(localPoint.x)));
		worldPoint = _mm_add_ps(worldPoint, _mm_mul_ps(transform.rotation.col1.quad, _mm_set1_ps(localPoint.y)));
		worldPoint = _mm_add_ps(worldPoint, _mm_mul_ps(transform.rotation.col2.quad, _mm_set1_ps(localPoint.z)));
		return ReadHavokPoint(RE::hkVector4(worldPoint)) * worldScale;
	}

	bool StoreShapeRadius(double bound, float& radius)
	{
		if (!std::isfinite(bound) || bound <= 0.0 || bound > std::numeric_limits<float>::max())
			return false;
		const float result = static_cast<float>(bound);
		if (result <= 0.0f)
			return false;
		radius = result;
		return true;
	}

	bool GetCapsuleRadius(const RE::hkpCapsuleShape& shape, float worldScale, float& radius)
	{
		const auto pointA = ReadHavokPoint(shape.vertexA);
		const auto pointB = ReadHavokPoint(shape.vertexB);
		if (!IsFinitePoint(pointA) || !IsFinitePoint(pointB) ||
			!std::isfinite(shape.radius) || shape.radius <= 0.0f ||
			!std::isfinite(worldScale) || worldScale <= 0.0f)
			return false;

		// A rigid capsule's enclosing radius is independent of body rotation and translation.
		const double length = std::hypot(
			static_cast<double>(pointA.x) - pointB.x,
			static_cast<double>(pointA.y) - pointB.y,
			static_cast<double>(pointA.z) - pointB.z);
		return StoreShapeRadius((shape.radius + length * 0.5) * worldScale, radius);
	}

	bool GetProjectedShapeBound(const RE::hkpShape& shape, float worldScale, RE::NiPoint3& localCenter, float& radius)
	{
		if (!std::isfinite(worldScale) || worldScale <= 0.0f)
			return false;
		std::array<double, 3> halfExtents{}, center{};
		for (size_t axis = 0; axis < 3; ++axis) {
			std::array<float, 3> direction{};
			direction[axis] = 1;
			const double positive = shape.GetMaximumProjection(RE::hkVector4{ direction[0], direction[1], direction[2], 0 });
			direction[axis] = -1;
			const double negative = shape.GetMaximumProjection(RE::hkVector4{ direction[0], direction[1], direction[2], 0 });
			if (!std::isfinite(positive) || !std::isfinite(negative))
				return false;
			// Opposite support projections add to the width and subtract to the center offset.
			halfExtents[axis] = (positive + negative) * 0.5;
			center[axis] = (positive - negative) * 0.5;
			if (halfExtents[axis] < 0)
				return false;
		}
		const double bound = shape.type == RE::hkpShapeType::kSphere ? halfExtents[0] :
		                                                               std::hypot(halfExtents[0], halfExtents[1], halfExtents[2]);
		float result;
		if (!StoreShapeRadius(bound * worldScale, result))
			return false;
		localCenter = { static_cast<float>(center[0]), static_cast<float>(center[1]), static_cast<float>(center[2]) };
		radius = result;
		return true;
	}
}

namespace Util
{
	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius)
	{
		if (!collisionObj)
			return false;

		RE::bhkRigidBody* bhkRigid = collisionObj->body.get() ? collisionObj->body.get()->AsBhkRigidBody() : nullptr;
		RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
		const auto* shape = hkpRigid ? hkpRigid->collidable.GetShape() : nullptr;
		// Compound skeleton bodies use blend collision objects; ordinary lists remain unsupported.
		if (!shape || (skyrim_cast<const RE::hkpListShape*>(shape) && !collisionObj->AsBhkBlendCollisionObject()))
			return false;

		const float worldScale = RE::bhkWorld::GetWorldScaleInverse();
		RE::NiPoint3 localCenter;
		float bound;
		if (shape->type == RE::hkpShapeType::kCapsule) {
			const auto& capsule = *static_cast<const RE::hkpCapsuleShape*>(shape);
			if (!GetCapsuleRadius(capsule, worldScale, bound))
				return false;
			localCenter = ReadHavokPoint(capsule.vertexA) * 0.5f + ReadHavokPoint(capsule.vertexB) * 0.5f;
		} else if (!GetProjectedShapeBound(*shape, worldScale, localCenter, bound)) {
			return false;
		}
		RE::hkTransform transform;
		bhkRigid->GetTransform(transform);
		const auto center = TransformHavokPoint(transform, RE::hkVector4{ localCenter.x, localCenter.y, localCenter.z, 0 }, worldScale);
		if (!IsFinitePoint(center))
			return false;
		centerPos = center;
		radius = bound;
		return true;
	}

	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius)
	{
		if (!shape || skyrim_cast<const RE::hkpListShape*>(shape))
			return false;
		if (shape->type == RE::hkpShapeType::kCapsule)
			return GetCapsuleRadius(*static_cast<const RE::hkpCapsuleShape*>(shape), RE::bhkWorld::GetWorldScaleInverse(), radius);
		RE::NiPoint3 localCenter;
		return GetProjectedShapeBound(*shape, RE::bhkWorld::GetWorldScaleInverse(), localCenter, radius);
	}
}
