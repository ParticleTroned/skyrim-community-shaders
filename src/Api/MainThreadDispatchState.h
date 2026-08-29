#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace CSX::Api
{
	/** Admission state shared by the caller and its queued runtime-main task. */
	template <class Result>
	class MainThreadDispatchState
	{
	public:
		enum class Phase
		{
			queued,
			running,
			cancelled,
			completed
		};

		bool TryBegin()
		{
			std::lock_guard lock(mutex);
			if (phase != Phase::queued)
				return false;
			phase = Phase::running;
			condition.notify_all();
			return true;
		}

		bool CancelIfQueued()
		{
			std::lock_guard lock(mutex);
			if (phase != Phase::queued)
				return false;
			phase = Phase::cancelled;
			condition.notify_all();
			return true;
		}

		void Complete(Result a_result)
		{
			std::lock_guard lock(mutex);
			result.emplace(std::move(a_result));
			phase = Phase::completed;
			condition.notify_all();
		}

		void Fail(std::exception_ptr a_error)
		{
			std::lock_guard lock(mutex);
			error = std::move(a_error);
			phase = Phase::completed;
			condition.notify_all();
		}

		template <class Clock, class Duration>
		Phase WaitUntil(const std::chrono::time_point<Clock, Duration>& a_deadline)
		{
			std::unique_lock lock(mutex);
			condition.wait_until(lock, a_deadline, [this] {
				return phase != Phase::queued;
			});
			return phase;
		}

		Result WaitForCompletion()
		{
			std::unique_lock lock(mutex);
			condition.wait(lock, [this] {
				return phase == Phase::completed || phase == Phase::cancelled;
			});
			if (error)
				std::rethrow_exception(error);
			if (!result)
				throw std::runtime_error("runtime-main dispatch was cancelled");
			return std::move(*result);
		}

	private:
		std::mutex mutex;
		std::condition_variable condition;
		Phase phase = Phase::queued;
		std::optional<Result> result;
		std::exception_ptr error;
	};
}
