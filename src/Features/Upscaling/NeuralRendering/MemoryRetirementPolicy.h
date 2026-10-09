#pragma once

#include "MemoryRecoveryPolicy.h"

namespace NeuralRendering
{
	/** Explicit resets release the backend; healthy resource transitions may keep it initialized. */
	enum class BackendRetirementPolicy
	{
		ReleaseBackend,
		RetainHealthyBackend
	};

	enum class MemoryRetirementReason
	{
		None,
		ResourcesReleased,
		Headroom,
		IdleTimeout
	};

	[[nodiscard]] constexpr const char* ToString(MemoryRetirementReason reason) noexcept
	{
		switch (reason) {
		case MemoryRetirementReason::None:
			return "none";
		case MemoryRetirementReason::ResourcesReleased:
			return "resources_released";
		case MemoryRetirementReason::Headroom:
			return "insufficient_headroom";
		case MemoryRetirementReason::IdleTimeout:
			return "idle_timeout";
		}
		return "unknown";
	}

	/** Retain only an empty healthy backend, with bounded lifetime and fresh pressure observation. */
	struct MemoryRetirementPolicy
	{
		static constexpr std::uint64_t kBlockedWindowMs = 1000;
		static constexpr std::uint64_t kIdleRetentionMs = 30000;
		static constexpr std::uint64_t kRecoveryRetentionMs = 120000;
		bool backendRetained = false;
		std::uint64_t warmRetirements = 0, coldEvictions = 0, retiredAtMs = 0;
		MemoryRetirementReason lastReason = MemoryRetirementReason::None;

		void Retained(std::uint64_t nowMs) noexcept
		{
			if (backendRetained)
				return;
			backendRetained = true;
			retiredAtMs = nowMs;
			++warmRetirements;
			lastReason = MemoryRetirementReason::ResourcesReleased;
			blockedWindow = false;
		}

		void Released(MemoryRetirementReason reason = MemoryRetirementReason::None) noexcept
		{
			backendRetained = false;
			blockedWindow = false;
			if (reason == MemoryRetirementReason::Headroom || reason == MemoryRetirementReason::IdleTimeout) {
				++coldEvictions;
				lastReason = reason;
			}
		}

		/** Cached samples and observation gaps cannot establish sustained post-retirement pressure. */
		[[nodiscard]] MemoryRetirementReason Observe(const MemoryBudgetSample& sample,
			std::uint64_t nowMs, bool admissionBlocked, bool recovering) noexcept
		{
			if (!backendRetained || nowMs < retiredAtMs)
				return MemoryRetirementReason::None;
			if (nowMs - retiredAtMs >= (recovering ? kRecoveryRetentionMs : kIdleRetentionMs))
				return MemoryRetirementReason::IdleTimeout;
			if (!sample.IsFresh(nowMs) || sample.sampledAtMs < retiredAtMs || !admissionBlocked) {
				blockedWindow = false;
				return MemoryRetirementReason::None;
			}
			if (!blockedWindow || sample.sampledAtMs < lastSampleMs ||
				sample.sampledAtMs - lastSampleMs > MemoryRecoveryPolicy::kMaximumSampleGapMs) {
				blockedWindow = true;
				blockedSinceMs = sample.sampledAtMs;
			}
			lastSampleMs = sample.sampledAtMs;
			return sample.sampledAtMs - blockedSinceMs >= kBlockedWindowMs ?
			           MemoryRetirementReason::Headroom :
			           MemoryRetirementReason::None;
		}

	private:
		bool blockedWindow = false;
		std::uint64_t blockedSinceMs = 0, lastSampleMs = 0;
	};
}
