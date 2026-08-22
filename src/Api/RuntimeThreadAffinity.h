#pragma once

#include <mutex>
#include <optional>
#include <thread>

namespace CSX::Api
{
	enum class ThreadBindResult
	{
		kBound,
		kAlreadyBound,
		kDifferentThread,
	};

	// Runtime thread identity must come from SKSE's game-task queue. Lifecycle
	// callbacks are not guaranteed to run on that thread, so API services must
	// not capture their owner in a constructor.
	class RuntimeThreadAffinity
	{
	public:
		ThreadBindResult BindCurrentThread();
		bool IsBound() const;
		bool IsCurrentThread() const;

	private:
		mutable std::mutex mutex;
		std::optional<std::thread::id> ownerThread;
	};

	RuntimeThreadAffinity& GetRuntimeMainThreadAffinity();
	ThreadBindResult BindRuntimeMainThread();
	bool IsRuntimeMainThread();
	void ScheduleRuntimeMainThreadBinding();
}
