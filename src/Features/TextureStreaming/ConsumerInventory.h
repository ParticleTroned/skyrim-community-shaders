#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace StreamingTextures
{
	/** Publish complete scene scans lazily without discarding the previous consumers mid-scan. */
	template <class Consumer>
	struct ConsumerInventory
	{
		std::vector<Consumer> committed, scanning;
		std::uint64_t seen = 0, verified = 0;

		void Promote(std::uint64_t completeScan)
		{
			if (completeScan && seen == completeScan && verified != completeScan) {
				committed = std::move(scanning);
				verified = completeScan;
			}
		}
		void Observe(std::uint64_t scan, std::uint64_t completeScan)
		{
			if (seen != scan) {
				Promote(completeScan);
				scanning.clear();
				seen = scan;
			}
		}
		void Clear()
		{
			committed.clear();
			scanning.clear();
			seen = verified = 0;
		}
		[[nodiscard]] bool Expired(std::uint64_t completeScan) const noexcept { return !seen || seen < completeScan; }
	};
}
