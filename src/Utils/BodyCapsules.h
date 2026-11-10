#pragma once

#include "Utils/FrameLatch.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Util::BodyCapsules
{
	/** @brief Most capsules Select can return for one query. */
	inline constexpr size_t MaxSelectedCapsules = 64;

	/** @brief Finger and palm capsule radii used until a fit to the player's hand meshes succeeds. */
	inline constexpr float DefaultFingerRadius = 1.2f;
	inline constexpr float DefaultPalmRadius = 1.6f;

	/** @brief A body part that can touch a mesh, in absolute world space. */
	struct Capsule
	{
		float3 pointA;
		float3 pointB;
		float radius = 0.0f;
		float3 move;              ///< How far the capsule centre moved since the previous Update, in world units per frame and capped at 8 units.
		RE::FormID owner = 0;     ///< The actor whose body the capsule belongs to.
		bool appendage = false;   ///< A hand, a finger or an item held in a hand, which also touches the owner's own gear.
		int side = -1;            ///< For an appendage, 0 for the left hand and 1 for the right, which for a held item is the hand holding it.
		float strength = 1.0f;    ///< How strongly the capsule acts, 1 for a live body part and lower for a contact that is fading.
		bool remembered = false;  ///< A contact kept after its source moved on, which fades through strength; false for a live body part.

		/** @brief The point on the capsule's axis nearest a_point. */
		[[nodiscard]] float3 ClosestPointOnAxis(const float3& a_point) const
		{
			const float3 axis = pointB - pointA;
			const float t = std::clamp((a_point - pointA).Dot(axis) / std::max(axis.LengthSquared(), 1e-4f), 0.0f, 1.0f);
			return pointA + axis * t;
		}
	};

	/** @brief True for a skeleton bone of a hand or a finger, such as "NPC L Finger12 [LF12]". */
	[[nodiscard]] inline bool IsHandBone(std::string_view a_name)
	{
		return a_name.find("Hand [") != std::string_view::npos || a_name.find("Finger") != std::string_view::npos;
	}

	/** @brief 0 for a left and 1 for a right hand or finger bone, -1 for any other bone. */
	[[nodiscard]] inline int HandBoneSide(std::string_view a_name)
	{
		if (!IsHandBone(a_name))
			return -1;
		return a_name.starts_with("NPC L ") ? 0 : (a_name.starts_with("NPC R ") ? 1 : -1);
	}

	/**
	 * @brief Collects the collision capsules of nearby actors and the player's VR hands and fingers.
	 * @details Actor capsules come from the same Havok collision objects Grass Collision reads. Finger capsules come from
	 *          the player skeleton, so they work without HIGGS. When HIGGS is installed in VR, the drawn weapon or shield in each hand and the object it holds
	 *          are added too, as appendages of that hand, and its hand body stands in for the fingers until the skeleton nodes are found. Update rebuilds the list at most once per rendered frame,
	 *          so every consumer can call it; read the list on the thread that calls Update, since nothing locks it.
	 */
	class Tracker
	{
	public:
		/**
		 * @brief Rebuilds the capsule list for the actors within a_maxDistance of the camera and the player's VR hands.
		 * @details Does nothing when the list was already rebuilt this frame, so the first caller of a frame sets the range.
		 */
		void Update(float a_maxDistance, bool a_fitHands = true, bool a_heldObjects = true);
		/** @brief Drops every capsule and the movement history; the next Update rebuilds them. */
		void Clear();

		/**
		 * @brief Picks the capsules nearest a mesh, skipping its wearer's own body capsules but keeping the wearer's hands and fingers.
		 * @param a_center World bound centre of the mesh.
		 * @param a_radius World bound radius of the mesh.
		 * @param a_reach How far past the mesh a capsule still matters.
		 * @param a_wearer Form ID of the actor wearing the mesh, or 0 when unknown.
		 * @param a_out Receives the chosen capsules, at most MaxSelectedCapsules of them.
		 * @param a_nearestGap Receives the smallest distance from the mesh bound to any eligible capsule, which is negative when one touches.
		 * @return How many entries of a_out were filled.
		 */
		[[nodiscard]] size_t Select(const RE::NiPoint3& a_center, float a_radius, float a_reach, RE::FormID a_wearer,
			std::span<Capsule> a_out, float& a_nearestGap) const;

		[[nodiscard]] size_t GetCount() const { return capsules.size(); }

		/** @brief How many of the player's hand and finger skeleton nodes were found, out of 2 * HandNodeCount. */
		[[nodiscard]] size_t GetHandNodeCount() const { return handNodesFound; }

		/** @brief Mean fitted finger radius over both hands. */
		[[nodiscard]] float GetAverageFingerRadius() const;

		/** @brief Mean fitted palm radius over both hands. */
		[[nodiscard]] float GetAveragePalmRadius() const;

	private:
		static constexpr size_t FingerCount = 5;
		static constexpr size_t JointsPerFinger = 3;
		static constexpr size_t HandNodeCount = 1 + FingerCount * JointsPerFinger;

		struct HandNodes
		{
			std::array<RE::NiPointer<RE::NiAVObject>, HandNodeCount> nodes;
		};

		void AddActorCapsules(RE::Actor* a_actor, const RE::NiPoint3& a_camera);
		void AddHandCapsules(RE::Actor* a_player, bool a_left);
		void Push(Capsule a_capsule, size_t a_index, const void* a_source);
		void RefreshHandNodes(RE::Actor* a_player);
		void AddHeldCapsules(RE::Actor* a_player);

		/**
		 * @brief Fits each finger and palm capsule radius to the skinned vertices of the player's hand and armor meshes.
		 * @details Reads the CPU copies of the vertices and skin weights. Each vertex goes to its dominant bone, and a bone's radius
		 *          is a high percentile of its vertices' distances to the bone segment, taking the outermost mesh over the hand. A fingertip is
		 *          measured to the open ray past its last joint, so vertices beyond the joint set its length instead of widening its radius.
		 */
		void FitRadii(RE::Actor* a_player);

		std::vector<Capsule> capsules;
		struct MotionSample
		{
			float3 center;
			const void* source = nullptr;
		};
		std::unordered_map<uint64_t, MotionSample> previousCenters;
		std::unordered_map<uint64_t, MotionSample> currentCenters;
		std::array<HandNodes, 2> handNodes;
		const RE::NiAVObject* handNodeRoot = nullptr;
		size_t handNodesFound = 0;

		struct HandRadii
		{
			std::array<float, FingerCount * JointsPerFinger> finger;
			std::array<float, FingerCount> tipExtent{};  ///< Mesh distance from each fingertip joint to the finger's end, 0 until fitted.
			float palm = DefaultPalmRadius;

			HandRadii() { finger.fill(DefaultFingerRadius); }
		};
		std::array<HandRadii, 2> handRadii;
		size_t fitMeshes = 0;
		uint64_t fitSignature = 0;
		uint32_t updatesSinceFitCheck = 0;
		FrameLatch updateLatch;
		uint32_t lastUpdateFrame = 0;
	};

	/** @brief The shared tracker, which every consumer of body capsules reads. */
	[[nodiscard]] Tracker& GetTracker();
}
