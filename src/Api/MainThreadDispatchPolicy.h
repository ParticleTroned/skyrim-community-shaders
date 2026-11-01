#pragma once

#include <atomic>
#include <cstdint>

namespace CSX::Api
{
	enum class MainThreadDispatchPhase : std::uint8_t
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
			auto expected = MainThreadDispatchPhase::Queued;
			return state.compare_exchange_strong(
				expected,
				MainThreadDispatchPhase::Running,
				std::memory_order_acq_rel,
				std::memory_order_acquire);
		}

		[[nodiscard]] bool TryCancel() noexcept
		{
			auto expected = MainThreadDispatchPhase::Queued;
			return state.compare_exchange_strong(
				expected,
				MainThreadDispatchPhase::Cancelled,
				std::memory_order_acq_rel,
				std::memory_order_acquire);
		}

		void Complete() noexcept
		{
			state.store(MainThreadDispatchPhase::Completed, std::memory_order_release);
		}

		[[nodiscard]] MainThreadDispatchPhase Get() const noexcept
		{
			return state.load(std::memory_order_acquire);
		}

	private:
		std::atomic<MainThreadDispatchPhase> state{ MainThreadDispatchPhase::Queued };
	};
}
