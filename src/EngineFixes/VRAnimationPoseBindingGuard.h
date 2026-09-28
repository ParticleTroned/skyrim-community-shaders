#pragma once

#include "EngineFix.h"

#include <atomic>

/** Guards Skyrim VR's pose copier against stale animation-node bindings. */
struct VRAnimationPoseBindingGuard : EngineFix
{
	std::string GetName() override { return "VR Animation Pose Binding Guard"; }
	const char* GetEngineFixesName() const override { return "VRAnimationPoseBindingGuard"; }
	bool TryInstall() override;

private:
	using BoneNodes = RE::BSTArray<RE::BShkbAnimationGraph::BoneNodeEntry>;
	using ApplyPose = void (*)(const RE::hkQsTransform*, BoneNodes*, std::int32_t);

	static void ApplyPoseGuarded(
		const RE::hkQsTransform* a_pose,
		BoneNodes* a_boneNodes,
		std::int32_t a_boneCount);

	static inline ApplyPose original = nullptr;
	static inline std::atomic<std::uint64_t> rejectedBindings{ 0 };
};
