#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace SunShadowReceiver
{
	inline constexpr std::uint32_t LogIntervalMilliseconds = 10000;
	inline constexpr std::size_t MaximumLogExamples = 4;

	struct RepairExample
	{
		std::uint32_t frame{}, referenceFormId{}, maskBefore{}, maskAfter{}, sunMask{};
		std::uintptr_t geometry{}, property{};
		double distanceSquared{}, rangeSquared{};
		std::array<char, 96> name{};
	};

	/** A lifetime repair count and four immutable, asynchronously published examples. */
	class RepairLog
	{
		struct Slot
		{
			RepairExample example{};
			std::atomic_bool ready{};
			bool reported{};
		};
		std::atomic_uint64_t total{};
		std::array<Slot, MaximumLogExamples> examples{};
		std::uint64_t reportedTotal{};
		bool reportedOnce{};

	public:
		/** Render callers perform one atomic increment; only the first four build examples. */
		template <class Builder>
		void Record(Builder&& build) noexcept
		{
			static_assert(std::is_nothrow_invocable_r_v<RepairExample, Builder>);
			const auto index = total.fetch_add(1, std::memory_order_relaxed);
			if (index < examples.size()) {
				auto& slot = examples[index];
				slot.example = build();
				slot.ready.store(true, std::memory_order_release);
			}
		}

		/** Read the number of successful repair calls, not unique meshes. */
		std::uint64_t Total() const noexcept { return total.load(std::memory_order_relaxed); }

		/** One serialized reporter emits changes and new examples; callbacks may throw for retry. */
		template <class SummaryWriter, class ExampleWriter>
		void Report(SummaryWriter&& summary, ExampleWriter&& example)
		{
			const auto current = Total();
			if (!reportedOnce || current != reportedTotal) {
				summary(current - reportedTotal, current);
				reportedTotal = current;
				reportedOnce = true;
			}
			for (std::size_t i = 0; i < examples.size(); ++i) {
				auto& slot = examples[i];
				if (!slot.reported && slot.ready.load(std::memory_order_acquire)) {
					example(i + 1, slot.example);
					slot.reported = true;
				}
			}
		}
	};
}
