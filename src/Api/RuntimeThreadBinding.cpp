#include "Api/RuntimeThreadAffinity.h"

namespace CSX::Api
{
	void ScheduleRuntimeMainThreadBinding()
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			logger::warn("Runtime API main-thread affinity could not be scheduled: SKSE task interface unavailable");
			return;
		}

		tasks->AddTask([] {
			const auto result = BindRuntimeMainThread();
			if (result == ThreadBindResult::kBound)
				logger::info("Runtime API main-thread affinity bound from SKSE task queue");
			else if (result == ThreadBindResult::kDifferentThread)
				logger::critical("SKSE task queue changed threads after runtime API affinity was bound");
		});
	}
}
