#include "ActorUtils.h"
#include "Globals.h"
#include <algorithm>
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

	bool GetShapeCollisionCapsule(RE::bhkNiCollisionObject* object, ShapeCollisionCapsule& result)
	{
		auto* body = object && object->body.get() ? object->body.get()->AsBhkRigidBody() : nullptr;
		auto* rigid = body ? skyrim_cast<RE::hkpRigidBody*>(body->referencedObject.get()) : nullptr;
		const auto* shape = rigid ? rigid->collidable.GetShape() : nullptr;
		ShapeCollisionCapsule capsule;
		if (shape && shape->type == RE::hkpShapeType::kCapsule) {
			const auto& source = *static_cast<const RE::hkpCapsuleShape*>(shape);
			const float scale = RE::bhkWorld::GetWorldScaleInverse();
			if (!std::isfinite(scale) || scale <= 0 || !StoreShapeRadius(double(source.radius) * scale, capsule.radius))
				return false;
			RE::hkTransform transform;
			body->GetTransform(transform);
			capsule.pointA = TransformHavokPoint(transform, source.vertexA, scale);
			capsule.pointB = TransformHavokPoint(transform, source.vertexB, scale);
			if (!IsFinitePoint(capsule.pointA) || !IsFinitePoint(capsule.pointB))
				return false;
		} else {
			if (!GetShapeBound(object, capsule.pointA, capsule.radius))
				return false;
			capsule.pointB = capsule.pointA;
		}
		result = capsule;
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

namespace Util
{
	void ForEachLoadedActor(const std::function<void(RE::Actor*)>& a_callback)
	{
		if (!a_callback)
			return;
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (player)
			a_callback(player);
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (actor && actor.get() != player)
					a_callback(actor.get());
			}
		}
	}

	void ForEachGeometry(RE::NiAVObject* a_root, const std::function<void(RE::BSGeometry*)>& a_callback)
	{
		if (!a_root || !a_callback)
			return;

		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&a_callback](RE::BSGeometry* a_geometry) {
			a_callback(a_geometry);
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	void ForEachActorGeometry(RE::Actor* a_actor, const std::function<void(RE::BSGeometry*)>& a_callback)
	{
		if (!a_actor || !a_actor->Is3DLoaded() || !a_callback)
			return;

		auto* thirdPersonRoot = a_actor->Get3D(false);
		auto* firstPersonRoot = a_actor->Get3D(true);
		ForEachGeometry(thirdPersonRoot, a_callback);
		if (firstPersonRoot != thirdPersonRoot)
			ForEachGeometry(firstPersonRoot, a_callback);
	}

	namespace
	{
		bool IsCapsuleFinite(const Util::ShapeCollisionCapsule& capsule)
		{
			return std::isfinite(capsule.pointA.x) && std::isfinite(capsule.pointA.y) && std::isfinite(capsule.pointA.z) &&
			       std::isfinite(capsule.pointB.x) && std::isfinite(capsule.pointB.y) && std::isfinite(capsule.pointB.z) &&
			       std::isfinite(capsule.radius) && capsule.radius > 0.0f;
		}

		/** @brief A world-space capsule along the longest side of an axis-aligned box in the body's own frame, with the mean of the other two half sides as its radius. */
		bool BoxCapsule(const float a_centre[3], const float a_half[3], const RE::hkTransform& a_transform, ShapeCollisionCapsule& a_capsule)
		{
			for (size_t axis = 0; axis < 3; ++axis)
				if (!std::isfinite(a_centre[axis]) || !std::isfinite(a_half[axis]) || a_half[axis] < 0.0f)
					return false;
			const int longest = a_half[0] >= a_half[1] && a_half[0] >= a_half[2] ? 0 : (a_half[1] >= a_half[2] ? 1 : 2);
			const float radius = 0.5f * (a_half[(longest + 1) % 3] + a_half[(longest + 2) % 3]);
			const float reach = std::max(a_half[longest] - radius, 0.0f);

			const auto endpoint = [&](float a_sign) {
				float local[4] = { a_centre[0], a_centre[1], a_centre[2], 0.0f };
				local[longest] += a_sign * reach;
				return TransformHavokPoint(a_transform, RE::hkVector4{ local[0], local[1], local[2], 0.0f }, RE::bhkWorld::GetWorldScaleInverse());
			};
			a_capsule.pointA = endpoint(-1.0f);
			a_capsule.pointB = endpoint(1.0f);
			a_capsule.radius = radius * RE::bhkWorld::GetWorldScaleInverse();
			return IsCapsuleFinite(a_capsule);
		}

		/** @brief A world-space capsule along the longest side of a shape's bounds in the body's own frame, or false for a shape this cannot bound. */
		bool BoundShapeCapsule(const RE::hkpShape* a_shape, const RE::hkTransform& a_transform, ShapeCollisionCapsule& a_capsule)
		{
			const float scale = RE::bhkWorld::GetWorldScaleInverse();
			if (a_shape->type == RE::hkpShapeType::kCapsule) {
				const auto* capsuleShape = static_cast<const RE::hkpCapsuleShape*>(a_shape);
				a_capsule.pointA = TransformHavokPoint(a_transform, capsuleShape->vertexA, scale);
				a_capsule.pointB = TransformHavokPoint(a_transform, capsuleShape->vertexB, scale);
				a_capsule.radius = capsuleShape->radius * scale;
				return IsCapsuleFinite(a_capsule);
			}
			if (a_shape->type == RE::hkpShapeType::kList || a_shape->type == RE::hkpShapeType::kCompound || a_shape->type == RE::hkpShapeType::kMOPP)
				return false;

			const auto extent = [a_shape](float x, float y, float z) {
				return a_shape->GetMaximumProjection(RE::hkVector4{ x, y, z, 0.0f });
			};
			const float high[3] = { extent(1.0f, 0.0f, 0.0f), extent(0.0f, 1.0f, 0.0f), extent(0.0f, 0.0f, 1.0f) };
			const float low[3] = { -extent(-1.0f, 0.0f, 0.0f), -extent(0.0f, -1.0f, 0.0f), -extent(0.0f, 0.0f, -1.0f) };
			float centre[3];
			float half[3];
			for (int axis = 0; axis < 3; ++axis) {
				centre[axis] = 0.5f * (high[axis] + low[axis]);
				half[axis] = 0.5f * (high[axis] - low[axis]);
			}
			return BoxCapsule(centre, half, a_transform, a_capsule);
		}
	}

	size_t GetRigidBodyCapsules(RE::bhkRigidBody* a_body, std::span<ShapeCollisionCapsule> a_capsules)
	{
		auto* rigid = a_body ? skyrim_cast<RE::hkpRigidBody*>(a_body->referencedObject.get()) : nullptr;
		const auto* shape = rigid && rigid->world ? rigid->collidable.GetShape() : nullptr;
		if (!shape || a_capsules.empty())
			return 0;

		RE::hkTransform transform;
		a_body->GetTransform(transform);

		if (const auto* list = skyrim_cast<const RE::hkpListShape*>(shape)) {
			size_t count = 0;
			const auto children = static_cast<size_t>(list->childInfo.size());
			if (children > 0 && children <= a_capsules.size()) {
				for (size_t index = 0; index < children; ++index) {
					const auto* child = list->childInfo[static_cast<int>(index)].shape;
					if (child && BoundShapeCapsule(child, transform, a_capsules[count]))
						++count;
				}
				if (count > 0)
					return count;
			}

			alignas(16) float centre[4];
			alignas(16) float half[4];
			_mm_store_ps(centre, list->aabbCenter.quad);
			_mm_store_ps(half, list->aabbHalfExtents.quad);
			return BoxCapsule(centre, half, transform, a_capsules[0]) ? 1 : 0;
		}
		return BoundShapeCapsule(shape, transform, a_capsules[0]) ? 1 : 0;
	}

}
