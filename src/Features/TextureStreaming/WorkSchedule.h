#pragma once

#include <algorithm>
#include <chrono>
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
		/** Idle frames need no camera snapshot; wakeups must replace any stale demand. */
		[[nodiscard]] bool NeedsDemand(InventoryWork next, std::uint64_t now, std::size_t learningRemaining) const noexcept
		{
			return next != mode || next == InventoryWork::Active || learningRemaining || Due(now);
		}
		void Complete(std::uint64_t now) noexcept
		{
			running = false;
			lastDurationMs = now >= startedAtMs ? now - startedAtMs : 0;
			nextScanMs = now + std::max(std::min(lastDurationMs, MaximumRestMs), mode == InventoryWork::Learning ? LearningIntervalMs : MinimumActiveRestMs);
		}
	};
}
