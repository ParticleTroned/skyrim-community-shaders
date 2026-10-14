#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace StreamingTextures
{
	/** Publish complete scans lazily and reject consumers from invalidated inventories. */
	template <class Consumer>
	struct ConsumerInventory
	{
		std::vector<Consumer> committed, scanning;
		std::uint64_t seen = 0, verified = 0, generation = 0;

		/** Reject the uninitialized generation as well as stale consumer evidence. */
		[[nodiscard]] bool Current(std::uint64_t currentGeneration) const noexcept { return currentGeneration && generation == currentGeneration; }
		/** Whether both published and in-progress scene references have retired. */
		[[nodiscard]] bool Empty() const noexcept { return committed.empty() && scanning.empty(); }

		void Promote(std::uint64_t completeScan, std::uint64_t currentGeneration)
		{
			if (Current(currentGeneration) && completeScan && seen == completeScan && verified != completeScan) {
				committed = std::move(scanning);
				verified = completeScan;
			}
		}
		/** Activate a new generation only after its previous references are retired. */
		[[nodiscard]] bool Observe(std::uint64_t scan, std::uint64_t completeScan, std::uint64_t currentGeneration)
		{
			if (!Current(currentGeneration)) {
				if (!currentGeneration || !Empty())
					return false;
				generation = currentGeneration;
				seen = verified = 0;
			}
			if (seen != scan) {
				Promote(completeScan, currentGeneration);
				scanning.clear();
				seen = scan;
			}
			return true;
		}
		/** Release at most one consumer without activating a newer inventory. */
		bool RetireOne()
		{
			const bool released = !Empty();
			if (!scanning.empty())
				scanning.pop_back();
			else if (!committed.empty())
				committed.pop_back();
			if (Empty())
				seen = verified = 0;
			return released;
		}
		void Clear()
		{
			committed.clear();
			scanning.clear();
			seen = verified = generation = 0;
		}
		[[nodiscard]] bool Expired(std::uint64_t completeScan, std::uint64_t currentGeneration) const noexcept
		{
			return !Current(currentGeneration) || !seen || seen < completeScan;
		}
	};
}
