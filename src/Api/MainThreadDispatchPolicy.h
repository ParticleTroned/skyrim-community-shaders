#pragma once

#include <atomic>
#include <cstdint>

namespace CSX::Api
{
	/**
	 * One-shot admission state for work queued to the runtime main thread.
	 *
	 * A timeout may cancel only an operation that is still queued. Once the
	 * runtime thread claims an operation, its result is authoritative and the
	 * caller must wait for that result instead of reporting a false cancellation.
	 */
	class MainThreadDispatchAdmission
	{
	public:
		enum class State : std::uint8_t
		{
			kQueued,
			kClaimed,
			kCancelled,
			kCompleted,
		};

		bool TryClaim() noexcept
		{
			auto expected = State::kQueued;
			return state.compare_exchange_strong(
				expected, State::kClaimed, std::memory_order_acq_rel, std::memory_order_acquire);
		}

		bool CancelIfQueued() noexcept
		{
			auto expected = State::kQueued;
			return state.compare_exchange_strong(
				expected, State::kCancelled, std::memory_order_acq_rel, std::memory_order_acquire);
		}

		void Complete() noexcept { state.store(State::kCompleted, std::memory_order_release); }
		State Get() const noexcept { return state.load(std::memory_order_acquire); }

	private:
		std::atomic<State> state{ State::kQueued };
	};
}
