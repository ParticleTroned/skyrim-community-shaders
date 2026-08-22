#include "Api/RuntimeThreadAffinity.h"

namespace CSX::Api
{
	ThreadBindResult RuntimeThreadAffinity::BindCurrentThread()
	{
		std::lock_guard lock(mutex);
		const auto current = std::this_thread::get_id();
		if (!ownerThread) {
			ownerThread = current;
			return ThreadBindResult::kBound;
		}
		return *ownerThread == current ? ThreadBindResult::kAlreadyBound : ThreadBindResult::kDifferentThread;
	}

	ThreadBindResult RuntimeThreadAffinity::AdoptCurrentThreadFromTaskQueue()
	{
		std::lock_guard lock(mutex);
		const auto current = std::this_thread::get_id();
		if (!ownerThread) {
			ownerThread = current;
			return ThreadBindResult::kBound;
		}
		if (*ownerThread == current)
			return ThreadBindResult::kAlreadyBound;
		ownerThread = current;
		return ThreadBindResult::kRebound;
	}

	bool RuntimeThreadAffinity::IsBound() const
	{
		std::lock_guard lock(mutex);
		return ownerThread.has_value();
	}

	bool RuntimeThreadAffinity::IsCurrentThread() const
	{
		std::lock_guard lock(mutex);
		return ownerThread && *ownerThread == std::this_thread::get_id();
	}

	RuntimeThreadAffinity& GetRuntimeMainThreadAffinity()
	{
		static RuntimeThreadAffinity affinity;
		return affinity;
	}

	ThreadBindResult BindRuntimeMainThread()
	{
		return GetRuntimeMainThreadAffinity().BindCurrentThread();
	}

	ThreadBindResult AdoptRuntimeMainThreadFromTaskQueue()
	{
		return GetRuntimeMainThreadAffinity().AdoptCurrentThreadFromTaskQueue();
	}

	bool IsRuntimeMainThread()
	{
		return GetRuntimeMainThreadAffinity().IsCurrentThread();
	}
}
