#pragma once

#include "CacheInvalidation.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace Util
{
	/** @brief Publish cache rollback flags and mismatch details as one state. */
	class CacheRollbackState
	{
	public:
		struct Flags
		{
			bool diskCacheHeld = false;
			bool featureSetChanged = false;
			bool featureSetRevertPending = false;
			bool featureSetCacheBackedUp = false;
			bool featureSetCacheSelectivelySeeded = false;
			bool previousDiskCacheAvailable = false;

			/** @brief Whether rollback permits disk-cache reads and writes. */
			bool AllowsDiskCache() const { return !diskCacheHeld && !featureSetRevertPending; }
			bool operator==(const Flags&) const = default;
		};

		struct Snapshot
		{
			Flags flags;
			std::vector<CacheInvalidation::CacheMismatch> cacheMismatches;
			std::vector<CacheInvalidation::CacheMismatch> previousCacheMismatches;
		};

		/** @brief Read one coherent flag set without locking or copying lists. */
		Flags GetFlags() const
		{
			const auto value = flags.load(std::memory_order_acquire);
			return {
				.diskCacheHeld = (value & kHeld) != 0,
				.featureSetChanged = (value & kChanged) != 0,
				.featureSetRevertPending = (value & kRevertPending) != 0,
				.featureSetCacheBackedUp = (value & kBackedUp) != 0,
				.featureSetCacheSelectivelySeeded = (value & kSelectivelySeeded) != 0,
				.previousDiskCacheAvailable = (value & kPreviousAvailable) != 0,
			};
		}

		/** @brief Copy owned details and flags from the same publication. */
		Snapshot GetSnapshot() const
		{
			std::lock_guard lock(mutex);
			return snapshot;
		}

		/** @brief Publish completed state; callers serialize read/modify/write operations. */
		void Publish(Snapshot a_snapshot)
		{
			const auto& next = a_snapshot.flags;
			const std::uint32_t value =
				(next.diskCacheHeld ? kHeld : 0) |
				(next.featureSetChanged ? kChanged : 0) |
				(next.featureSetRevertPending ? kRevertPending : 0) |
				(next.featureSetCacheBackedUp ? kBackedUp : 0) |
				(next.featureSetCacheSelectivelySeeded ? kSelectivelySeeded : 0) |
				(next.previousDiskCacheAvailable ? kPreviousAvailable : 0);
			{
				std::lock_guard lock(mutex);
				std::swap(snapshot, a_snapshot);
				flags.store(value, std::memory_order_release);
			}
		}

	private:
		static constexpr std::uint32_t kHeld = 1u << 0;
		static constexpr std::uint32_t kChanged = 1u << 1;
		static constexpr std::uint32_t kRevertPending = 1u << 2;
		static constexpr std::uint32_t kBackedUp = 1u << 3;
		static constexpr std::uint32_t kSelectivelySeeded = 1u << 4;
		static constexpr std::uint32_t kPreviousAvailable = 1u << 5;
		static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

		std::atomic<std::uint32_t> flags{ 0 };
		mutable std::mutex mutex;
		Snapshot snapshot;
	};
}
