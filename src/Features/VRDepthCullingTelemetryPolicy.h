#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace VRDepthCullingTelemetryPolicy
{
	inline constexpr std::array<std::uint64_t, 7> DurationUpperBoundsNanoseconds{
		1'000, 2'000, 4'000, 8'000, 16'000, 32'000, 64'000
	};

	constexpr std::size_t DurationBin(std::uint64_t a_nanoseconds)
	{
		for (std::size_t index = 0; index < DurationUpperBoundsNanoseconds.size(); ++index) {
			if (a_nanoseconds <= DurationUpperBoundsNanoseconds[index])
				return index;
		}
		return DurationUpperBoundsNanoseconds.size();
	}

	/** Coordinate lock-free render-thread samples with an infrequent reset. */
	class WriterGate
	{
	public:
		[[nodiscard]] bool TryEnter() noexcept
		{
			if (!enabled.load(std::memory_order_relaxed) || resetting.load(std::memory_order_acquire))
				return false;
			writers.fetch_add(1, std::memory_order_acq_rel);
			if (resetting.load(std::memory_order_acquire)) {
				writers.fetch_sub(1, std::memory_order_release);
				return false;
			}
			return true;
		}

		void Leave() noexcept
		{
			writers.fetch_sub(1, std::memory_order_release);
		}

		[[nodiscard]] bool TryLockForReset() noexcept
		{
			bool expected = false;
			if (!resetting.compare_exchange_strong(
					expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
				return false;
			}
			if (writers.load(std::memory_order_acquire) == 0)
				return true;
			resetting.store(false, std::memory_order_release);
			return false;
		}

		void UnlockAfterReset() noexcept
		{
			resetting.store(false, std::memory_order_release);
		}

		void SetEnabled(bool a_enabled) noexcept
		{
			enabled.store(a_enabled, std::memory_order_release);
		}

		[[nodiscard]] bool IsEnabled() const noexcept
		{
			return enabled.load(std::memory_order_acquire);
		}

	private:
		std::atomic_bool enabled{ true };
		std::atomic_bool resetting{ false };
		std::atomic_uint32_t writers{ 0 };
	};
}
