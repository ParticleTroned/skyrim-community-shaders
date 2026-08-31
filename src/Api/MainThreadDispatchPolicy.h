#pragma once

#include <atomic>
#include <cstdint>

namespace CSX::Api
{
	enum class MainThreadDispatchState : std::uint8_t
	{
		Queued,
		Running,
		Cancelled,
		Completed,
	};

	class MainThreadDispatchClaim
	{
	public:
		[[nodiscard]] bool TryClaim() noexcept
		{
			auto expected = MainThreadDispatchState::Queued;
			return state.compare_exchange_strong(
				expected,
				MainThreadDispatchState::Running,
				std::memory_order_acq_rel,
				std::memory_order_acquire);
		}

		[[nodiscard]] bool TryCancel() noexcept
		{
			auto expected = MainThreadDispatchState::Queued;
			return state.compare_exchange_strong(
				expected,
				MainThreadDispatchState::Cancelled,
				std::memory_order_acq_rel,
				std::memory_order_acquire);
		}

		void Complete() noexcept
		{
			state.store(MainThreadDispatchState::Completed, std::memory_order_release);
		}

		[[nodiscard]] MainThreadDispatchState Get() const noexcept
		{
			return state.load(std::memory_order_acquire);
		}

	private:
		std::atomic<MainThreadDispatchState> state{ MainThreadDispatchState::Queued };
	};
}
