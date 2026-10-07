#pragma once

#include "MemoryRecoveryPolicy.h"
#include "RegionCapacity.h"

#include <array>

namespace NeuralRendering
{
	/** Reclaims optional capacity before full NR suspension, without changing image quality. */
	struct MemoryConservationPolicy
	{
		static constexpr double kEnterBudgetRatio = 0.80;
		static constexpr double kExitBudgetRatio = 0.75;
		static constexpr std::uint64_t kEnterHeadroom = 1024 * MemoryRecoveryPolicy::kMiB;
		static constexpr std::uint64_t kExitHeadroom = 1536 * MemoryRecoveryPolicy::kMiB;
		static constexpr std::uint64_t kStableHeadroomMs = 5000;
		static constexpr std::uint64_t kReclaimIntervalMs = 2000;
		static constexpr std::uint64_t kMinimumTrimBytes = 32 * MemoryRecoveryPolicy::kMiB;
		bool active = false;
		bool healthyWindow = false;
		std::uint64_t healthySinceMs = 0, lastHealthySampleMs = 0, nextReclaimMs = 0;
		std::uint64_t entries = 0, exits = 0, retiredSlots = 0, trimmedColorBuffers = 0;
		// Cumulative logical texture bytes; native allocations and physical residency are unknown.
		std::uint64_t reclaimedLogicalBytes = 0;
		bool reclaimedLogicalBytesKnown = true;
		std::array<std::uint64_t, kPhysicalFeatureSlotCount> colorTrimEpochs{};

		/** Slot rebuilds must not restart trimming within the same pressure episode. */
		[[nodiscard]] bool CanTrimColorBuffers(std::uint32_t slot) const noexcept
		{
			return active && slot < colorTrimEpochs.size() && colorTrimEpochs[slot] != entries;
		}

		void RecordColorTrim(std::uint32_t slot, std::uint64_t logicalSavings) noexcept
		{
			if (!CanTrimColorBuffers(slot))
				return;
			colorTrimEpochs[slot] = entries;
			trimmedColorBuffers += 2;
			reclaimedLogicalBytes += logicalSavings;
		}

		void ClearHealthyWindow() noexcept
		{
			healthyWindow = false;
			healthySinceMs = lastHealthySampleMs = 0;
		}

		void Update(const MemoryBudgetSample& sample, std::uint64_t additionalBytes,
			bool recovering, std::uint64_t nowMs) noexcept
		{
			const bool fresh = sample.IsFresh(nowMs);
			const double projectedRatio = fresh ?
			                                  (static_cast<double>(sample.usageBytes) + static_cast<double>(additionalBytes)) / static_cast<double>(sample.budgetBytes) :
			                                  0.0;
			const auto headroom = sample.Headroom();
			const auto projectedHeadroom = headroom > additionalBytes ? headroom - additionalBytes : 0;
			if (recovering || (fresh && (projectedRatio >= kEnterBudgetRatio || projectedHeadroom <= kEnterHeadroom))) {
				if (!active) {
					active = true;
					++entries;
					nextReclaimMs = 0;
				}
				ClearHealthyWindow();
				return;
			}
			if (!active)
				return;
			if (!fresh || projectedRatio > kExitBudgetRatio || projectedHeadroom < kExitHeadroom) {
				ClearHealthyWindow();
				return;
			}
			if (!healthyWindow || sample.sampledAtMs < lastHealthySampleMs ||
				sample.sampledAtMs - lastHealthySampleMs > MemoryRecoveryPolicy::kMaximumSampleGapMs) {
				healthyWindow = true;
				healthySinceMs = sample.sampledAtMs;
			}
			lastHealthySampleMs = sample.sampledAtMs;
			if (sample.sampledAtMs - healthySinceMs >= kStableHeadroomMs) {
				active = false;
				++exits;
				ClearHealthyWindow();
			}
		}

		/** Both native routes may be used in one frame; inactivity needs a grace interval. */
		[[nodiscard]] static bool IsInactive(std::uint64_t lastUseMs, std::uint64_t nowMs) noexcept
		{
			return nowMs >= lastUseMs && nowMs - lastUseMs >= kReclaimIntervalMs;
		}

		/** Small ROI changes do not justify a fenced release and replacement allocation. */
		[[nodiscard]] static bool WorthTrimming(std::uint64_t retainedBytes, std::uint64_t neededBytes) noexcept
		{
			return retainedBytes > neededBytes && retainedBytes - neededBytes >= kMinimumTrimBytes &&
			       retainedBytes - neededBytes >= retainedBytes / 4;
		}
	};
}
