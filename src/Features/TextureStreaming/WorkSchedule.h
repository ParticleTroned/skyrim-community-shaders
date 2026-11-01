#pragma once

#include "GeometryDemand.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace StreamingTextures
{
	inline constexpr auto MaintenanceBudget = std::chrono::microseconds(400);
	inline constexpr auto RestorationBudget = std::chrono::microseconds(100);
	inline constexpr auto PruningBudget = std::chrono::microseconds(50);
	inline constexpr auto InventoryBudget = std::chrono::microseconds(200);
	enum class InventoryWork : std::uint8_t
	{
		Idle,
		Learning,
		Active
	};

	/** Reduced resources and explicit caps require service even after pressure clears. */
	inline InventoryWork ResolveInventoryWork(bool enabled, bool reducing, bool capped, bool reduced, bool pending, bool remember) noexcept
	{
		return !enabled ? InventoryWork::Idle : reducing || capped || reduced || pending ? InventoryWork::Active :
		                                    remember                                     ? InventoryWork::Learning :
		                                                                                   InventoryWork::Idle;
	}

	/** Pace complete inventories; never let a large scene consume every frame indefinitely. */
	struct WorkSchedule
	{
		static constexpr std::uint64_t LearningIntervalMs = 5000, MinimumActiveRestMs = 250, MaximumRestMs = 5000;
		static constexpr std::uint64_t LearningRefreshMs = 30000;
		// This schedules remembered-size learning, never live reduction or restoration demand.
		static constexpr double LearningMovement = TextureStreamingPolicy::MovementHorizon / 32;
		InventoryWork mode = InventoryWork::Idle;
		std::uint64_t nextScanMs = 0, startedAtMs = 0, lastDurationMs = 0;
		bool running = false;

		/** Mode changes invalidate the caller's inventory before admitting new reductions. */
		bool Update(InventoryWork next, std::uint64_t now) noexcept
		{
			if (mode == next)
				return false;
			mode = next;
			Reset(now);
			return true;
		}
		void Reset(std::uint64_t now) noexcept
		{
			learning.valid = false;
			running = false;
			startedAtMs = now;
			lastDurationMs = 0;
			nextScanMs = now + (mode == InventoryWork::Learning ? LearningIntervalMs : 0);
		}
		[[nodiscard]] bool Due(std::uint64_t now) const noexcept
		{
			return mode != InventoryWork::Idle && (running || now < startedAtMs || now >= nextScanMs);
		}
		void Begin(std::uint64_t now) noexcept
		{
			running = true;
			startedAtMs = now;
		}
		/** Defer only learning that cannot authorize remembered reductions; periodically rescan unknown consumers. */
		[[nodiscard]] bool DeferUnchangedLearning(std::uint64_t now, std::uint32_t frame, const DemandContext& demand,
			std::uint64_t epoch, std::uint64_t resources, const void* root, bool catalogNeedsRefresh) noexcept
		{
			if (mode != InventoryWork::Learning || running)
				return false;
			if (catalogNeedsRefresh) {
				learning.valid = false;
				return false;
			}
			const bool fresh = demand.Fresh(frame, now);
			if (fresh && learning.valid && root && learning.root == root && learning.epoch == epoch && learning.resources == resources &&
				now >= learning.startedAtMs && now - learning.startedAtMs < LearningRefreshMs && SameLearningDemand(learning.demand, demand)) {
				const auto remaining = LearningRefreshMs - (now - learning.startedAtMs);
				nextScanMs = now + std::min(LearningIntervalMs, remaining);
				return true;
			}
			learning = { demand, epoch, resources, now, root, fresh };
			return false;
		}
		/** Prefetch only reusable learning demand; full inventories already request fresh demand. */
		[[nodiscard]] bool NeedsDemand(InventoryWork next, std::uint64_t now, std::size_t learningRemaining, bool catalogNeedsRefresh) const noexcept
		{
			return next != mode || next == InventoryWork::Active || learningRemaining || Due(now) ||
			       (next == InventoryWork::Learning && learning.valid && !catalogNeedsRefresh && now < nextScanMs &&
					   nextScanMs - now <= DemandContext::MaximumAgeMs && nextScanMs >= learning.startedAtMs &&
					   nextScanMs - learning.startedAtMs < LearningRefreshMs);
		}
		void Complete(std::uint64_t now) noexcept
		{
			running = false;
			lastDurationMs = now >= startedAtMs ? now - startedAtMs : 0;
			nextScanMs = now + std::max(std::min(lastDurationMs, MaximumRestMs), mode == InventoryWork::Learning ? LearningIntervalMs : MinimumActiveRestMs);
		}

	private:
		struct LearningCheckpoint
		{
			DemandContext demand;
			std::uint64_t epoch = 0, resources = 0, startedAtMs = 0;
			const void* root = nullptr;
			bool valid = false;
		} learning;

		static bool SameLearningDemand(const DemandContext& previous, const DemandContext& current) noexcept
		{
			if (previous.eyeCount != current.eyeCount || !std::isfinite(current.mipBias) ||
				std::abs(current.mipBias) > 16 || previous.mipBias != current.mipBias)
				return false;
			for (unsigned i = 0; i < current.eyeCount; ++i) {
				const auto& a = previous.eyes[i];
				const auto& b = current.eyes[i];
				const std::array before{ a.renderWidth, a.renderHeight, a.outputWidth, a.outputHeight, a.projectionX, a.projectionY };
				const std::array after{ b.renderWidth, b.renderHeight, b.outputWidth, b.outputHeight, b.projectionX, b.projectionY };
				if (before != after || std::any_of(after.begin(), after.end(), [](double value) { return !std::isfinite(value) || value <= 0; }))
					return false;
				double squared = 0;
				for (unsigned axis = 0; axis < current.positions[i].size(); ++axis) {
					const double aPosition = previous.positions[i][axis], bPosition = current.positions[i][axis];
					if (!std::isfinite(aPosition) || !std::isfinite(bPosition))
						return false;
					const double delta = bPosition - aPosition;
					squared += delta * delta;
				}
				if (squared >= LearningMovement * LearningMovement)
					return false;
			}
			return true;
		}
	};
}
