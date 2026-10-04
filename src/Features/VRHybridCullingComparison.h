#pragma once

#include "VRHybridCullingPolicy.h"

/** Session-only DevBench comparison policy; include only in diagnostic builds. */
namespace VRHybridCullingComparison
{
	enum class Setting : std::uint64_t
	{
		GuardedProof = 1,
		CoarseDepth = 2
	};
	inline constexpr std::uint64_t kSettingsMask = 3;

	/** Advance provenance on real changes, including a return to a previous configuration. */
	constexpr std::uint64_t Select(std::uint64_t a_previous, Setting a_setting, bool a_enabled)
	{
		const auto bit = static_cast<std::uint64_t>(a_setting);
		if (((a_previous & bit) != 0) == a_enabled)
			return a_previous;
		const auto settings = (a_previous & kSettingsMask & ~bit) | (a_enabled ? bit : 0);
		return ((a_previous & ~kSettingsMask) + kSettingsMask + 1) | settings;
	}

	constexpr bool GuardedProof(std::uint64_t a_selection) { return (a_selection & static_cast<std::uint64_t>(Setting::GuardedProof)) != 0; }
	constexpr bool CoarseDepth(std::uint64_t a_selection) { return (a_selection & static_cast<std::uint64_t>(Setting::CoarseDepth)) != 0; }
	constexpr std::uint64_t Revision(std::uint64_t a_selection) { return a_selection / (kSettingsMask + 1); }

	/** Reuse production admission limits for both the coarse baseline and finer preference. */
	constexpr bool TryMakeBuildConstants(std::uint64_t a_selection,
		const std::array<VRHybridCullingPolicy::EyeRect, VRHybridCullingPolicy::kEyeCount>& a_eyes,
		std::uint32_t a_sourceWidth, std::uint32_t a_sourceHeight,
		VRHybridCullingPolicy::BuildConstants& a_constants, VRHybridCullingPolicy::PyramidLayout& a_layout)
	{
		using namespace VRHybridCullingPolicy;
		return CoarseDepth(a_selection) ?
		           VRHybridCullingPolicy::TryMakeBuildConstants(a_eyes, a_sourceWidth, a_sourceHeight, kLargeSourceReduction, a_constants, a_layout) :
		           TryMakePreferredBuildConstants(a_eyes, a_sourceWidth, a_sourceHeight, a_constants, a_layout);
	}
}
