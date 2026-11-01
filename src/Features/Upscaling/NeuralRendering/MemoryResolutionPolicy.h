#pragma once

#include "MemoryRecoveryPolicy.h"
#include "ModelResolutionPolicy.h"

#include <algorithm>
#include <cstdint>

namespace NeuralRendering
{
	/** Optional session-only inference reduction; the configured quality remains the recovery target. */
	struct MemoryResolutionPolicy
	{
		static constexpr std::uint32_t kStepPercent = 10;
		static constexpr std::uint64_t kWaitingStepMs = 5000;
		static constexpr std::uint64_t kRestoreWindowMs = 30000;
		static constexpr double kRestoreBudgetRatio = 0.70;
		static constexpr std::uint64_t kRestoreHeadroom = 2048 * MemoryRecoveryPolicy::kMiB;

		bool enabled = false;
		std::uint32_t ceilingPercent = kMaximumModelResolutionPercent;
		std::uint64_t reductions = 0, restorations = 0;
		bool retired = false, healthyWindow = false;
		std::uint64_t lastReductionMs = 0, healthySinceMs = 0;
		std::uint64_t lastHealthySampleMs = 0, lastSuccessMs = 0;

		/** An explicit preference change starts a fresh adaptive episode. */
		void SetEnabled(bool value) noexcept
		{
			if (enabled == value)
				return;
			enabled = value;
			ceilingPercent = kMaximumModelResolutionPercent;
			retired = false;
			lastReductionMs = 0;
			ClearHealthyWindow();
		}

		/** Invalid requests remain invalid for the renderer's configuration validation. */
		[[nodiscard]] std::uint32_t EffectivePercent(std::uint32_t requested) const noexcept
		{
			return enabled && IsValidModelResolutionPercent(requested) ? std::min(requested, ceilingPercent) : requested;
		}

		/** History resets and inactive rendering break restoration continuity, not the pressure ceiling. */
		void ClearHealthyWindow() noexcept
		{
			healthyWindow = false;
			healthySinceMs = lastHealthySampleMs = lastSuccessMs = 0;
		}

		/** A completed reset permits later pressure steps without lowering quality by itself. */
		void MarkResourcesRetired(std::uint64_t nowMs) noexcept
		{
			ClearHealthyWindow();
			if (!enabled)
				return;
			retired = true;
			lastReductionMs = nowMs;
		}

		/** Lower capacity only after the previous native resources have been safely retired. */
		[[nodiscard]] bool OnRetired(std::uint32_t requested, std::uint64_t nowMs) noexcept
		{
			ClearHealthyWindow();
			if (!enabled || !IsValidModelResolutionPercent(requested))
				return false;
			MarkResourcesRetired(nowMs);
			return Reduce(requested);
		}

		/** Waiting owns no model capacity; fresh blocked admission may select a smaller next retry. */
		[[nodiscard]] bool WhileWaiting(std::uint32_t requested, const MemoryBudgetSample& sample,
			std::uint64_t nowMs, bool admissionBlocked) noexcept
		{
			ClearHealthyWindow();
			if (!enabled || !retired || !admissionBlocked || !sample.IsFresh(nowMs) ||
				nowMs < lastReductionMs || nowMs - lastReductionMs < kWaitingStepMs)
				return false;
			if (!Reduce(requested))
				return false;
			lastReductionMs = nowMs;
			return true;
		}

		/** A larger candidate still requires ordinary peak-allocation admission before evaluation. */
		[[nodiscard]] bool ObserveSuccess(std::uint32_t requested, const MemoryBudgetSample& sample,
			std::uint64_t nowMs, std::uint64_t pendingBytes, bool conserving, bool ready) noexcept
		{
			if (!enabled || !IsValidModelResolutionPercent(requested) || EffectivePercent(requested) >= requested ||
				!ready || conserving || pendingBytes != 0 || !sample.IsFresh(nowMs) ||
				sample.Headroom() < kRestoreHeadroom ||
				static_cast<double>(sample.usageBytes) / static_cast<double>(sample.budgetBytes) > kRestoreBudgetRatio) {
				ClearHealthyWindow();
				return false;
			}
			if (!healthyWindow || nowMs < lastSuccessMs || nowMs - lastSuccessMs > MemoryRecoveryPolicy::kMaximumSampleGapMs ||
				sample.sampledAtMs < lastHealthySampleMs ||
				sample.sampledAtMs - lastHealthySampleMs > MemoryRecoveryPolicy::kMaximumSampleGapMs) {
				healthyWindow = true;
				healthySinceMs = nowMs;
			}
			lastSuccessMs = nowMs;
			lastHealthySampleMs = sample.sampledAtMs;
			if (sample.sampledAtMs < healthySinceMs || sample.sampledAtMs - healthySinceMs < kRestoreWindowMs)
				return false;
			ceilingPercent = std::min(requested, ceilingPercent + kStepPercent);
			++restorations;
			ClearHealthyWindow();
			return true;
		}

		/** Rejected growth retains working NR and requires a new healthy restoration window. */
		[[nodiscard]] bool RejectRestoration(std::uint32_t workingPercent) noexcept
		{
			ClearHealthyWindow();
			if (!enabled || !IsValidModelResolutionPercent(workingPercent) || workingPercent >= ceilingPercent)
				return false;
			ceilingPercent = workingPercent;
			return true;
		}

	private:
		[[nodiscard]] bool Reduce(std::uint32_t requested) noexcept
		{
			if (!IsValidModelResolutionPercent(requested))
				return false;
			const auto effective = EffectivePercent(requested);
			if (effective <= kMinimumModelResolutionPercent)
				return false;
			ceilingPercent = std::max(kMinimumModelResolutionPercent, effective - kStepPercent);
			++reductions;
			return true;
		}
	};
}
