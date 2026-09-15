#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
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

	class MainThreadDispatchTimeout : public std::runtime_error
	{
	public:
		MainThreadDispatchTimeout() : std::runtime_error("runtime-main task was not admitted before its deadline") {}
	};

	class MainThreadDispatchRejected : public std::runtime_error
	{
	public:
		MainThreadDispatchRejected() : std::runtime_error("runtime-main task submission failed") {}
	};

	// A submission error is terminal only if cancellation wins before admission,
	// just like a deadline. A queue may retain or begin its callback before throwing.
	template <class Submit, class Run, class Rep, class Period>
	auto DispatchMainThreadTask(Submit&& a_submit, Run&& a_run, std::chrono::duration<Rep, Period> a_timeout)
	{
		using State = MainThreadDispatchState<std::invoke_result_t<Run&>>;
		auto state = std::make_shared<State>();
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		try {
			std::invoke(std::forward<Submit>(a_submit), [state, run = std::forward<Run>(a_run)]() mutable {
				if (!state->TryBegin())
					return;
				try {
					state->Complete(std::invoke(run));
				} catch (...) {
					state->Fail(std::current_exception());
				}
			});
		} catch (...) {
			if (state->CancelIfQueued())
				throw MainThreadDispatchRejected();
		}
		if (state->WaitUntil(deadline) == State::Phase::queued && state->CancelIfQueued())
			throw MainThreadDispatchTimeout();
		// Admitted work must publish its exact result before the caller returns.
		return state->WaitForCompletion();
	}
}
