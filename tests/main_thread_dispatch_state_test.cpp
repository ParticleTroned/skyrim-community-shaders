#include "Api/MainThreadDispatchState.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using State = CSX::Api::MainThreadDispatchState<int>;
using namespace std::chrono_literals;

int main()
{
	{
		auto state = std::make_shared<State>();
		if (!state->CancelIfQueued() || state->TryBegin())
			throw std::runtime_error("cancel must make queued work permanently ineligible");
	}
	{
		auto state = std::make_shared<State>();
		std::atomic_bool mutated = false;
		std::thread worker([&] {
			if (!state->TryBegin())
				return;
			mutated.store(true, std::memory_order_release);
			state->Complete(42);
		});
		if (state->WaitUntil(std::chrono::steady_clock::now() + 1s) == State::Phase::queued)
			throw std::runtime_error("admission was not published");
		if (state->CancelIfQueued())
			throw std::runtime_error("cancellation must lose after admission");
		if (state->WaitForCompletion() != 42 || !mutated.load(std::memory_order_acquire))
			throw std::runtime_error("admitted work did not publish its response");
		worker.join();
	}
	return 0;
}
