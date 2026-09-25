#include "ActorUtils.h"
#include <algorithm>
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
		const double bound = (shape.radius + length * 0.5) * worldScale;
		if (!std::isfinite(bound) || bound > std::numeric_limits<float>::max())
			return false;
		const float result = static_cast<float>(bound);
		if (result <= 0.0f)
			return false;
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
		if (!shape || skyrim_cast<const RE::hkpListShape*>(shape))
			return false;

		const float worldScale = RE::bhkWorld::GetWorldScaleInverse();
		RE::NiPoint3 center;
		float bound;
		if (shape->type == RE::hkpShapeType::kCapsule) {
			const auto& capsule = *static_cast<const RE::hkpCapsuleShape*>(shape);
			if (!GetCapsuleRadius(capsule, worldScale, bound))
				return false;
			RE::hkTransform transform;
			bhkRigid->GetTransform(transform);
			const auto pointA = TransformHavokPoint(transform, capsule.vertexA, worldScale);
			const auto pointB = TransformHavokPoint(transform, capsule.vertexB, worldScale);
			if (!IsFinitePoint(pointA) || !IsFinitePoint(pointB))
				return false;
			center = pointA * 0.5f + pointB * 0.5f;
		} else {
			RE::hkVector4 massCenter;
			bhkRigid->GetCenterOfMassWorld(massCenter);
			float massTrans[4];
			// Use unaligned store to avoid UB from potential stack misalignment
			_mm_storeu_ps(massTrans, massCenter.quad);
			center = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * worldScale;
			if (!Util::ExtractShapeBound(shape, bound))
				return false;
		}
		if (!IsFinitePoint(center))
			return false;
		centerPos = center;
		radius = bound;
		return true;
	}

	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius)
	{
		using ShapeType = RE::hkpShapeType;
		if (!shape)
			return false;
		if (shape->type == ShapeType::kCapsule)
			return GetCapsuleRadius(*static_cast<const RE::hkpCapsuleShape*>(shape), RE::bhkWorld::GetWorldScaleInverse(), radius);

		// Helpers to avoid repeating projection math and ensure offset-invariant half-extents
		auto project = [shape](float x, float y, float z) {
			return shape->GetMaximumProjection(RE::hkVector4{ x, y, z, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
		};
		auto symmetricHalfExtents = [&project](float& hx, float& hy, float& hz) {
			float x_pos = project(1.0f, 0.0f, 0.0f);
			float x_neg = project(-1.0f, 0.0f, 0.0f);
			float y_pos = project(0.0f, 1.0f, 0.0f);
			float y_neg = project(0.0f, -1.0f, 0.0f);
			float z_pos = project(0.0f, 0.0f, 1.0f);
			float z_neg = project(0.0f, 0.0f, -1.0f);
			hx = 0.5f * (x_pos - x_neg);
			hy = 0.5f * (y_pos - y_neg);
			hz = 0.5f * (z_pos - z_neg);
		};
		auto halfDiagonal = [](float hx, float hy, float hz) {
			return sqrtf(hx * hx + hy * hy + hz * hz);
		};
		if (shape->type == ShapeType::kSphere) {
			// For spheres, any axis should yield the same half-extent; use symmetric X
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = hx;
			return true;
		} else if (shape->type == ShapeType::kBox) {
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = halfDiagonal(hx, hy, hz);
			return true;
		} else if (shape->type == ShapeType::kCylinder) {
			// Use symmetric half-extents; cylinder radius is max of X/Y half-extents
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			float hr = std::max(hx, hy);
			radius = sqrtf(hr * hr + hz * hz);
			return true;
		} else if (shape->type == ShapeType::kConvexVertices || shape->type == ShapeType::kTriangle) {
			// Offset-invariant estimate: take symmetric half-extents per axis and use the max
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = std::max(hx, std::max(hy, hz));
			return true;
		} else {
			// Fallback: mirror the convex/triangle approach for consistency
			float hx, hy, hz;
			symmetricHalfExtents(hx, hy, hz);
			radius = std::max(hx, std::max(hy, hz));
			return true;
		}
	}
}
