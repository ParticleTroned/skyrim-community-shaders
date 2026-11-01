#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace LightLimitFixDetail::ShadowCulling
{
	/** Publish immutable shadow demand without locking the publication for each caster. */
	template <class Snapshot>
	class SnapshotPublication
	{
	public:
		/** One cache per reader thread; the publication must outlive its readers. */
		class Reader
		{
		public:
			explicit Reader(const SnapshotPublication& a_source) : source(a_source) {}

			/** Retain demand for this check without keeping retired engine objects alive while idle. */
			[[nodiscard]] std::shared_ptr<const Snapshot> Acquire() noexcept
			{
				const auto published = source.generation.load(std::memory_order_acquire);
				if (initialized && generation == published) {
					// An expired cache conservatively keeps casters while publication finishes.
					return cached.lock();
				}

				// Sample the generation first so a concurrent replacement cannot tag old demand as new.
				auto captured = source.Read();
				cached = captured;
				generation = published;
				initialized = true;
				return captured;
			}

		private:
			const SnapshotPublication& source;
			std::weak_ptr<const Snapshot> cached;
			std::uint64_t generation = 0;
			bool initialized = false;
		};

		/** Replacement and clearing invalidate every thread's cached publication. */
		void Publish(std::shared_ptr<const Snapshot> a_snapshot) noexcept
		{
			snapshot.store(std::move(a_snapshot), std::memory_order_release);
			generation.fetch_add(1, std::memory_order_release);
		}

		/** Uncached inspection retains the current publication independently of geometry readers. */
		[[nodiscard]] std::shared_ptr<const Snapshot> Read() const noexcept
		{
			return snapshot.load(std::memory_order_acquire);
		}

	private:
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
		std::atomic<std::shared_ptr<const Snapshot>> snapshot;
		std::atomic<std::uint64_t> generation{ 0 };
	};
}
