#pragma once

#include <cstdint>

namespace VRRenderScaleFrameBoundaryPolicy
{
	struct PairIdentity
	{
		std::uint64_t token = 0;
		std::uint64_t compositorCycle = 0;
		std::uint32_t frame = 0;
		std::uint32_t thread = 0;
	};

	struct PairCompletion
	{
		PairIdentity identity{};
		std::uint32_t completedEyeMask = 0;
		bool ambiguousCompletion = false;
	};

	/// Record returned eye work only for the owning native stereo invocation.
	constexpr void RecordEyeCompletion(
		PairCompletion& a_completion,
		std::uint64_t a_pairToken,
		std::uint32_t a_eye) noexcept
	{
		if (a_pairToken == 0 ||
			a_pairToken != a_completion.identity.token)
			return;
		if (a_eye > 1u) {
			a_completion.ambiguousCompletion = true;
			return;
		}
		const auto eyeBit = 1u << a_eye;
		if ((a_completion.completedEyeMask & eyeBit) != 0)
			a_completion.ambiguousCompletion = true;
		a_completion.completedEyeMask |= eyeBit;
	}

	/// Require both native eye calls to have returned within one unchanged boundary.
	[[nodiscard]] constexpr bool CanServiceCompletedPair(
		const PairCompletion& a_completion,
		const PairIdentity& a_current) noexcept
	{
		const auto& captured = a_completion.identity;
		return captured.token != 0 && captured.compositorCycle != 0 &&
		       captured.frame != 0 && captured.thread != 0 &&
		       captured.token == a_current.token &&
		       captured.compositorCycle == a_current.compositorCycle &&
		       captured.frame == a_current.frame &&
		       captured.thread == a_current.thread &&
		       a_completion.completedEyeMask == 0x3u &&
		       !a_completion.ambiguousCompletion;
	}
}
