#include "Api/MainThreadDispatchPolicy.h"
#include "Api/MainThreadDispatchState.h"

#include <atomic>
#include <chrono>
#include <latch>
#include <stdexcept>
#include <string_view>
#include <thread>

using State = CSX::Api::MainThreadDispatchResultState<int>;
using namespace std::chrono_literals;

int main()
{
	{
		auto state = std::make_shared<State>();
		std::atomic_bool mutated = false;
		if (state->WaitUntil(std::chrono::steady_clock::now()) != State::Phase::queued ||
			!state->CancelIfQueued()) {
			throw std::runtime_error("queued deadline must cancel before execution");
		}
		std::thread worker([&] {
			if (!state->TryBegin())
				return;
			mutated.store(true, std::memory_order_release);
			state->Complete(1);
		});
		worker.join();
		if (mutated.load(std::memory_order_acquire) || state->TryBegin())
			throw std::runtime_error("cancel must make queued work permanently ineligible");
	}
	{
		auto state = std::make_shared<State>();
		std::atomic_bool mutated = false;
		std::latch admitted{ 1 };
		std::latch release{ 1 };
		std::thread worker([&] {
			if (!state->TryBegin())
				return;
			admitted.count_down();
			release.wait();
			mutated.store(true, std::memory_order_release);
			state->Complete(42);
		});
		admitted.wait();
		if (state->WaitUntil(std::chrono::steady_clock::now() + 1s) == State::Phase::queued)
			throw std::runtime_error("admission was not published");
		if (state->CancelIfQueued())
			throw std::runtime_error("cancellation must lose after admission");
		release.count_down();
		if (state->WaitForCompletion() != 42 || !mutated.load(std::memory_order_acquire))
			throw std::runtime_error("admitted work did not publish its response");
		worker.join();
	}
	{
		State state;
		if (state.WaitUntil(std::chrono::steady_clock::now()) != State::Phase::queued ||
			!state.TryBegin() || state.CancelIfQueued() || state.TryBegin()) {
			throw std::runtime_error("admission must win cancellation after the deadline observation");
		}
		state.Complete(7);
		if (state.WaitForCompletion() != 7 || state.TryBegin())
			throw std::runtime_error("completed work must not be admitted again");
	}
	{
		auto state = std::make_shared<State>();
		if (!state->TryBegin())
			throw std::runtime_error("failure case was not admitted");
		state->Fail(std::make_exception_ptr(std::runtime_error("expected failure")));
		try {
			(void)state->WaitForCompletion();
			throw std::runtime_error("failed work returned a result");
		} catch (const std::runtime_error& error) {
			if (std::string_view(error.what()) != "expected failure")
				throw;
		}
	}
	return 0;
}
