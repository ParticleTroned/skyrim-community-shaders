#pragma once

#include "Api/MainThreadDispatchState.h"
#include "Api/RuntimeThreadAffinity.h"

#include <SKSE/SKSE.h>

namespace CSX::Api
{
	/** @brief Marshal legacy calls; unadmitted work is cancelled before returning the safe fallback. */
	template <class Run, class Result>
	Result DispatchLegacyRuntimeCall(Run run, Result fallback)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return fallback;
		try {
			return DispatchMainThreadTask(
				[tasks](auto task) { tasks->AddTask(std::move(task)); },
				[run = std::move(run)]() mutable {
					EnterRuntimeMainThreadTask();
					return run();
				},
				std::chrono::seconds(5));
		} catch (...) {
			// The legacy ABI has no error result; report conservative defaults on failure.
			return fallback;
		}
	}
}
