

#pragma once
#include "RE/Skyrim.h"
#include <functional>
#include <span>
#include <string>
#include <vector>

// Forward declarations for Skyrim types
namespace RE
{
	class bhkNiCollisionObject;
	class hkpShape;
	class NiPoint3;
}

namespace Util
{
	/** @brief A Havok or Gamebryo point as the float3 the shaders and constant buffers use. */
	inline float3 ToFloat3(const RE::NiPoint3& a_point)
	{
		return { a_point.x, a_point.y, a_point.z };
	}

	struct ShapeCollisionCapsule
	{
		RE::NiPoint3 pointA, pointB;
		float radius;
	};
	/** @brief Extract a world-space capsule or enclosing sphere; preserve outputs on failure. */
	bool GetShapeCollisionCapsule(RE::bhkNiCollisionObject* object, ShapeCollisionCapsule& result);

	/**
	 * @brief Reduces a rigid body's shape to world-space capsules, so each part of a weapon is covered by its own capsule.
	 * @details A capsule shape is used as it is. Any other shape, such as a box or convex hull, becomes a capsule along the longest
	 *          side of its bounds in the body's own frame, with the mean of the other two half sides as its radius, so a haft or a
	 *          blade stays long and thin. A shape list gives one capsule per child, such as one for an axe's haft and one for its
	 *          head, or a single capsule from the bounding box it keeps when it has more children than a_capsules holds.
	 * @param a_body Rigid body that is in a physics world.
	 * @param a_capsules Receives the capsules.
	 * @return How many entries of a_capsules were filled.
	 */
	size_t GetRigidBodyCapsules(RE::bhkRigidBody* a_body, std::span<ShapeCollisionCapsule> a_capsules);

	/** Visits currently loaded actors while each resolved handle is retained. */
	void ForEachLoadedActor(const std::function<void(RE::Actor*)>& a_callback);
	/** Visits geometry beneath a loaded scene root. */
	void ForEachGeometry(RE::NiAVObject* a_root, const std::function<void(RE::BSGeometry*)>& a_callback);
	/** Visits distinct first- and third-person geometry roots. */
	void ForEachActorGeometry(RE::Actor* a_actor, const std::function<void(RE::BSGeometry*)>& a_callback);

	/**
     * @brief Extracts a world-space sphere centered on transformed shape geometry.
     * @note List shapes are supported only on blend collision objects, as one enclosing sphere.
     * @param collisionObj Pointer to the collision object.
     * @param centerPos Output: center position of the shape.
     * @param radius Output: radius of the shape.
     * @return False for missing/unsupported shapes or invalid/unrepresentable bounds; outputs remain unchanged on failure.
     */
	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius);

	/**
     * @brief Bounds a shape in world units using capsule geometry or support projections.
     * @param shape Pointer to the shape.
     * @param radius Output: radius of the shape.
     * @return False for missing/list shapes or invalid/unrepresentable bounds; radius remains unchanged on failure.
     */
	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius);

	/**
     * @brief Holds display info for an actor (used in UI tables).
     */
	struct ActorDisplayInfo
	{
		RE::TESObjectREFR* actor;  ///< Reference to the actor
		std::string name;          ///< Actor name
		std::string formID;        ///< FormID as string
		std::string type;          ///< Type string (e.g., "Actor", "Actor (Dead/Ragdoll)")
		RE::NiPoint3 pos;          ///< Position
		float sqDist;              ///< Squared distance from reference point
	};

	/**
     * @brief Gets the ragdoll center for a dead actor.
     * @param actor Pointer to the actor.
     * @param outCenter Output: center position of the ragdoll.
     * @return True if center was found, false otherwise.
     */
	inline bool GetRagdollCenter(RE::Actor* actor, RE::NiPoint3& outCenter)
	{
		if (!actor || !actor->IsDead())
			return false;
		if (auto root = actor->Get3D(false)) {
			bool found = false;
			RE::NiPoint3 ragdollCenter;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
				float radius = 0.0f;
				RE::NiPoint3 centerPos;
				if (Util::GetShapeBound(a_object, centerPos, radius)) {
					ragdollCenter = centerPos;
					found = true;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (found) {
				outCenter = ragdollCenter;
				return true;
			}
		}
		return false;
	}

	/**
     * @brief Fills outInfo with display info for an actor, including ragdoll center if dead.
     * @param ref Reference to the actor.
     * @param eyePos Position to use for distance calculation.
     * @param trackRagdolls Whether to include ragdoll info for dead actors.
     * @param outInfo Output: filled display info struct.
     * @return True if info was filled, false otherwise.
     */
	inline bool GetActorDisplayInfo(RE::TESObjectREFR* ref, const RE::NiPoint3& eyePos, bool trackRagdolls, ActorDisplayInfo& outInfo)
	{
		if (!ref)
			return false;
		auto actor = static_cast<RE::Actor*>(ref);
		outInfo.actor = ref;
		outInfo.name = ref->GetName();
		outInfo.formID = std::format("{:X}", ref->GetFormID());
		outInfo.type = "Actor";
		if (actor && actor->IsDead()) {
			if (!trackRagdolls)
				return false;
			outInfo.type = "Actor (Dead/Ragdoll)";
			RE::NiPoint3 pos;
			if (!GetRagdollCenter(actor, pos)) {
				pos = actor->GetPosition();
			}
			outInfo.pos = pos;
		} else {
			outInfo.pos = ref->GetPosition();
		}
		outInfo.sqDist = outInfo.pos.GetSquaredDistance(eyePos);
		return true;
	}
}
