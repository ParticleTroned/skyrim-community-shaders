#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace Diagnostics
{
	/** Preallocated per-thread records; readers close admission and await quiescence. */
	template <class Event, std::size_t ThreadCount, std::size_t Capacity>
	class BoundedThreadTrace
	{
		static constexpr std::uint64_t OpenBit = std::uint64_t{ 1 } << 63;
		std::atomic_uint64_t admission{};

	public:
		struct Lane
		{
			std::atomic_uint32_t owner{};
			std::atomic_size_t count{}, dropped{};
			std::array<Event, Capacity> events{};
		};
		std::unique_ptr<Lane[]> lanes;
		std::atomic_size_t unassigned{};

		class Lease
		{
			BoundedThreadTrace* trace{};

		public:
			explicit Lease(BoundedThreadTrace& value) noexcept
			{
				auto state = value.admission.load(std::memory_order_relaxed);
				while (state & OpenBit) {
					if (value.admission.compare_exchange_weak(state, state + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
						trace = &value;
						break;
					}
				}
			}
			Lease(const Lease&) = delete;
			Lease& operator=(const Lease&) = delete;
			~Lease()
			{
				if (trace)
					trace->admission.fetch_sub(1, std::memory_order_release);
			}
			explicit operator bool() const noexcept { return trace != nullptr; }
		};

		void Reset()
		{
			if (!Quiescent())
				throw std::logic_error("Trace still owns writers");
			if (!lanes)
				lanes = std::make_unique<Lane[]>(ThreadCount);
			for (std::size_t i = 0; i < ThreadCount; ++i) {
				lanes[i].owner = 0;
				lanes[i].count = 0;
				lanes[i].dropped = 0;
			}
			unassigned = 0;
		}
		void Open()
		{
			std::uint64_t expected = 0;
			if (!lanes || !admission.compare_exchange_strong(expected, OpenBit, std::memory_order_release))
				throw std::logic_error("Trace is not ready to open");
		}
		void Close() noexcept { admission.fetch_and(~OpenBit, std::memory_order_acq_rel); }
		bool IsOpen() const noexcept { return (admission.load(std::memory_order_acquire) & OpenBit) != 0; }
		bool Quiescent() const noexcept { return admission.load(std::memory_order_acquire) == 0; }

		/** Call under a Lease; one native thread owns each lane until Reset. */
		void Append(std::uint32_t thread, const Event& event) noexcept
		{
			for (std::size_t i = 0; i < ThreadCount; ++i) {
				auto& lane = lanes[i];
				auto owner = lane.owner.load(std::memory_order_relaxed);
				if (owner == 0 && thread != 0) {
					lane.owner.compare_exchange_strong(owner, thread, std::memory_order_relaxed);
					owner = lane.owner.load(std::memory_order_relaxed);
				}
				if (owner != thread || thread == 0)
					continue;
				const auto index = lane.count.load(std::memory_order_relaxed);
				if (index == Capacity) {
					lane.dropped.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				lane.events[index] = event;
				lane.count.store(index + 1, std::memory_order_release);
				return;
			}
			unassigned.fetch_add(1, std::memory_order_relaxed);
		}
	};
}
