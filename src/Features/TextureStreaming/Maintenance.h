#pragma once

#include <chrono>
#include <cstddef>

namespace StreamingTextures
{
	inline constexpr auto maintenanceAllowance = std::chrono::microseconds(400);

	/** One shared soft deadline; indivisible engine operations can exceed it. */
	template <class Clock = std::chrono::steady_clock>
	class MaintenanceBudget
	{
	public:
		MaintenanceBudget() : deadline(Clock::now() + maintenanceAllowance) {}
		/** Check the original deadline without extending the frame allowance. */
		[[nodiscard]] bool Available() const { return Clock::now() < deadline; }

	private:
		const typename Clock::time_point deadline;
	};

	enum class MaintenancePhase
	{
		AdoptLoads,
		PreviewScan,
		RetireInventory,
		WorldScan,
		Select,
		Prune,
		Count
	};

	/** Rotate the first opportunity so an exhausted allowance cannot starve a phase. */
	class MaintenanceSchedule
	{
	public:
		static constexpr std::size_t phaseCount = static_cast<std::size_t>(MaintenancePhase::Count);
		[[nodiscard]] std::size_t StartFrame() noexcept
		{
			const auto first = next;
			next = (next + 1) % phaseCount;
			return first;
		}
		[[nodiscard]] static MaintenancePhase Phase(std::size_t first, std::size_t offset) noexcept
		{
			return static_cast<MaintenancePhase>((first + offset) % phaseCount);
		}

	private:
		std::size_t next = 0;
	};
}
