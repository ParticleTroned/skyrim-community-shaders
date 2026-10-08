#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "VRHybridCullingHistory.h"

#	include <cstddef>
#	include <cstdint>

namespace VRHybridCullingMatchedReadback
{
	inline constexpr std::size_t kCapacity = 3;
	inline constexpr std::uint32_t kMaximumAge = 8;
	inline constexpr std::size_t kNoSlot = kCapacity;

	enum class Stage : std::uint8_t
	{
		Free,
		Submitted,
		AwaitingNativeReadback,
		BeforeRecovery,
		PendingGPU
	};

	/** Native production may rotate its output; the submitted input and frame must stay identical. */
	constexpr bool CanBindNativeOutput(const VRHybridCullingHistory::Batch& a_submitted,
		const VRHybridCullingHistory::Batch& a_native, bool a_boundsUnchanged)
	{
		return a_boundsUnchanged && a_submitted.culler != 0 && a_submitted.transforms != 0 && a_submitted.results != 0 && a_submitted.selector <= 1 &&
		       a_submitted.count > 0 && a_submitted.count <= VRDepthCullingTemporalPolicy::kMaximumObjects &&
		       a_native.results != 0 && a_native.selector <= 1 && a_submitted.epoch != 0 &&
		       a_submitted.culler == a_native.culler && a_submitted.transforms == a_native.transforms &&
		       a_submitted.count == a_native.count && a_submitted.epoch == a_native.epoch &&
		       a_submitted.frame == a_native.frame;
	}

	/** Unsigned subtraction retains the bound across the engine frame counter's wrap. */
	constexpr bool Expired(std::uint32_t a_submissionFrame, std::uint32_t a_currentFrame)
	{
		return a_currentFrame - a_submissionFrame > kMaximumAge;
	}
}

#endif
