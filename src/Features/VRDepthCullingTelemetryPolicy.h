#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace VRDepthCullingTelemetryPolicy
{
	inline constexpr std::array<std::uint64_t, 7> DurationUpperBoundsNanoseconds{
		1'000, 2'000, 4'000, 8'000, 16'000, 32'000, 64'000
	};
	inline constexpr std::size_t DurationBinCount = DurationUpperBoundsNanoseconds.size() + 1;

	template <std::size_t Count>
	constexpr std::size_t DurationBin(std::uint64_t a_nanoseconds, const std::array<std::uint64_t, Count>& a_upperBounds)
	{
		for (std::size_t index = 0; index < a_upperBounds.size(); ++index) {
			if (a_nanoseconds <= a_upperBounds[index])
				return index;
		}
		return a_upperBounds.size();
	}

	constexpr std::size_t DurationBin(std::uint64_t a_nanoseconds) { return DurationBin(a_nanoseconds, DurationUpperBoundsNanoseconds); }

	/** Publish a maximum without losing a concurrent larger observation. */
	inline void UpdateMaximum(std::atomic_uint64_t& a_target, std::uint64_t a_value) noexcept
	{
		auto current = a_target.load(std::memory_order_relaxed);
		while (current < a_value &&
			   !a_target.compare_exchange_weak(current, a_value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
	}

	/** Coordinate lock-free render-thread samples with an infrequent reset. */
	class WriterGate
	{
	public:
		[[nodiscard]] bool TryEnter() noexcept
		{
			auto current = state.load(std::memory_order_relaxed);
			for (;;) {
				if ((current & (Disabled | Resetting)) != 0 || (current & WriterMask) == WriterMask)
					return false;
				if (state.compare_exchange_weak(current, current + 1, std::memory_order_acquire, std::memory_order_relaxed))
					return true;
			}
		}

		void Leave() noexcept
		{
			state.fetch_sub(1, std::memory_order_release);
		}

		[[nodiscard]] bool TryLockForReset() noexcept
		{
			auto current = state.load(std::memory_order_relaxed);
			if ((current & (Resetting | WriterMask)) != 0)
				return false;
			return state.compare_exchange_strong(current, current | Resetting, std::memory_order_acquire, std::memory_order_relaxed);
		}

		void UnlockAfterReset() noexcept
		{
			state.fetch_and(~Resetting, std::memory_order_release);
		}

		void SetEnabled(bool a_enabled) noexcept
		{
			if (a_enabled)
				state.fetch_and(~Disabled, std::memory_order_release);
			else
				state.fetch_or(Disabled, std::memory_order_release);
		}

		[[nodiscard]] bool IsEnabled() const noexcept
		{
			return (state.load(std::memory_order_acquire) & Disabled) == 0;
		}

		/** Disabled admission is stable only after all admitted writers finish. */
		[[nodiscard]] bool IsFrozen() const noexcept
		{
			return state.load(std::memory_order_acquire) == Disabled;
		}

	private:
		static constexpr std::uint32_t Disabled = 1u << 31;
		static constexpr std::uint32_t Resetting = 1u << 30;
		static constexpr std::uint32_t WriterMask = Resetting - 1;
		// One modification order prevents a reset and a writer from both entering
		// after observing stale values of separate reset/writer atomics.
		std::atomic_uint32_t state{ 0 };
	};

	/** Keep an admitted sample protected until its final counters are published. */
	class WriterScope
	{
	public:
		explicit WriterScope(WriterGate& a_gate) noexcept : gate(a_gate.TryEnter() ? &a_gate : nullptr) {}
		~WriterScope()
		{
			if (gate)
				gate->Leave();
		}
		WriterScope(const WriterScope&) = delete;
		WriterScope& operator=(const WriterScope&) = delete;
		explicit operator bool() const noexcept { return gate != nullptr; }

	private:
		WriterGate* gate;
	};

	/** Reset all participating counters together, or leave every counter untouched. */
	template <class Reset>
	[[nodiscard]] bool TryReset(WriterGate& a_gate, Reset&& a_reset) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<Reset>);
		if (!a_gate.TryLockForReset())
			return false;
		a_reset();
		a_gate.UnlockAfterReset();
		return true;
	}
}
