#pragma once

#include "SubmissionFenceSnapshot.h"
#include <algorithm>
#include <cstdint>

namespace NeuralRendering
{
	enum class StallRecoveryDecision
	{
		Waiting,
		Recover,
		Unsafe,
		Expired
	};

	/** Recovery never releases a stalled submission before observable completion and a full drain. */
	struct StallRecoveryPolicy
	{
		static constexpr std::uint32_t kMaximumAttempts = 3;
		static constexpr std::uint64_t kPollIntervalMs = 500;
		static constexpr std::uint64_t kCooldownMs = 10000;
		static constexpr std::uint64_t kMaximumWaitMs = 60000;
		bool pending = false;
		std::uint32_t attempts = 0;
		std::uint64_t failedAtMs = 0, lastPollAtMs = 0;
		LifetimeFenceSnapshot fences{};

		/** Zero means the session has exhausted its automatic retry allowance. */
		[[nodiscard]] std::uint64_t CooldownMs() const noexcept
		{
			return attempts < kMaximumAttempts ? kCooldownMs * (attempts + 1ull) : 0;
		}
		/** Retry limits survive successful teardown and explicit settings transitions. */
		bool Arm(std::uint64_t now) noexcept
		{
			if (pending || attempts >= kMaximumAttempts)
				return false;
			pending = true;
			failedAtMs = lastPollAtMs = now;
			fences = {};
			return true;
		}
		void Cancel() noexcept { pending = false; }
		[[nodiscard]] bool ShouldPoll(std::uint64_t now) const noexcept
		{
			return pending && (now < lastPollAtMs || now - lastPollAtMs >= kPollIntervalMs);
		}
		StallRecoveryDecision Observe(std::uint64_t now, const LifetimeFenceSnapshot& observed) noexcept
		{
			lastPollAtMs = now;
			fences = observed;
			if (!pending || now < failedAtMs || !fences.initialized || fences.recording || !fences.completedKnown || fences.deviceRemoved)
				return StallRecoveryDecision::Unsafe;
			const bool complete = fences.completed >= fences.issued &&
			                      std::ranges::all_of(fences.contexts, [&](auto value) { return value <= fences.completed; });
			if (complete && now - failedAtMs >= CooldownMs())
				return StallRecoveryDecision::Recover;
			return now - failedAtMs >= kMaximumWaitMs ? StallRecoveryDecision::Expired : StallRecoveryDecision::Waiting;
		}
		/** Count the attempt before starting the drain; a failed drain cannot retry. */
		void BeginAttempt() noexcept
		{
			pending = false;
			++attempts;
		}
	};
}
