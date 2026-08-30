#include "Api/MainThreadDispatchPolicy.h"

#include <future>

int main()
{
	using CSX::Api::MainThreadDispatchClaim;
	using CSX::Api::MainThreadDispatchState;

	MainThreadDispatchClaim cancelled;
	if (!cancelled.TryCancel() || cancelled.TryClaim() ||
		cancelled.Get() != MainThreadDispatchState::Cancelled) {
		return 1;
	}

	MainThreadDispatchClaim running;
	if (!running.TryClaim() || running.TryCancel() ||
		running.Get() != MainThreadDispatchState::Running) {
		return 1;
	}
	running.Complete();
	if (running.Get() != MainThreadDispatchState::Completed)
		return 1;

	for (int iteration = 0; iteration < 256; ++iteration) {
		MainThreadDispatchClaim raced;
		auto claim = std::async(std::launch::async, [&] { return raced.TryClaim(); });
		auto cancel = std::async(std::launch::async, [&] { return raced.TryCancel(); });
		if (claim.get() == cancel.get())
			return 1;
	}
	return 0;
}
