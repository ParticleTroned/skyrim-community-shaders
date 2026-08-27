#include "Api/MainThreadDispatchPolicy.h"

#include <barrier>
#include <stdexcept>
#include <thread>

using CSX::Api::MainThreadDispatchAdmission;

int main()
{
	{
		MainThreadDispatchAdmission admission;
		if (!admission.CancelIfQueued() || admission.TryClaim() ||
			admission.Get() != MainThreadDispatchAdmission::State::kCancelled)
			throw std::runtime_error("queued cancellation was not exclusive");
	}
	{
		MainThreadDispatchAdmission admission;
		if (!admission.TryClaim() || admission.CancelIfQueued())
			throw std::runtime_error("claimed work was cancelled after admission");
		admission.Complete();
		if (admission.Get() != MainThreadDispatchAdmission::State::kCompleted)
			throw std::runtime_error("claimed work did not complete");
	}
	for (int iteration = 0; iteration < 1000; ++iteration) {
		MainThreadDispatchAdmission admission;
		std::barrier gate(3);
		bool claimed = false;
		bool cancelled = false;
		std::jthread worker([&] {
			gate.arrive_and_wait();
			claimed = admission.TryClaim();
		});
		std::jthread timeout([&] {
			gate.arrive_and_wait();
			cancelled = admission.CancelIfQueued();
		});
		gate.arrive_and_wait();
		worker.join();
		timeout.join();
		if (claimed == cancelled || (!claimed && !cancelled))
			throw std::runtime_error("admission race did not produce exactly one winner");
		if (claimed)
			admission.Complete();
		const auto expected = claimed ? MainThreadDispatchAdmission::State::kCompleted :
			MainThreadDispatchAdmission::State::kCancelled;
		if (admission.Get() != expected)
			throw std::runtime_error("admission race published the wrong terminal state");
	}
	return 0;
}
