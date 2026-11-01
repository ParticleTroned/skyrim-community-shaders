#include "Api/RuntimeThreadAffinity.h"

namespace CSX::Api
{
	void EnterRuntimeMainThreadTask()
	{
		const auto result = AdoptRuntimeMainThreadFromTaskQueue();
		if (result == ThreadBindResult::kBound)
			logger::info("Runtime API main-thread affinity bound from SKSE task queue");
		else if (result == ThreadBindResult::kRebound)
			logger::info("Runtime API main-thread affinity followed an SKSE task-queue thread transition");
	}

	void ScheduleRuntimeMainThreadBinding()
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			logger::warn("Runtime API main-thread affinity could not be scheduled: SKSE task interface unavailable");
			return;
		}

		tasks->AddTask([] { EnterRuntimeMainThreadTask(); });
	}
}
