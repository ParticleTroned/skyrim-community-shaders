#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stop_token>
#include <winrt/base.h>

struct ID3D10Blob;

namespace Util::ShaderCreation
{
	/** Runtime publication is independent of successful bytecode compilation. */
	enum class Result
	{
		Published,
		Stale,
		CompileFailed,
		ReflectionFailed,
		CreationFailed
	};

	/** A worker retains verified bytecode across bounded runtime-object retries. */
	struct Attempt
	{
		Result result = Result::CompileFailed;
		winrt::com_ptr<ID3D10Blob> bytecode;
	};

	/** Wait without allocating another shared wait state after a creation allocation failure. */
	inline bool WaitForRetry(std::stop_token a_stop, std::chrono::milliseconds a_delay)
	{
		std::mutex mutex;
		std::condition_variable wake;
		std::stop_callback onStop(a_stop, [&] {
			std::lock_guard lock(mutex);
			wake.notify_all();
		});
		std::unique_lock lock(mutex);
		wake.wait_for(lock, a_delay, [&] { return a_stop.stop_requested(); });
		return !a_stop.stop_requested();
	}

	/** Retry only runtime creation, retaining one dispatch slot and the exact bytecode. */
	template <class Perform, class Wait>
	Result Run(Attempt& a_attempt, Perform&& a_perform, Wait&& a_wait)
	{
		constexpr std::array delays{ std::chrono::milliseconds(100), std::chrono::milliseconds(500), std::chrono::milliseconds(2000) };
		for (std::size_t retry = 0;; ++retry) {
			a_perform();
			if (a_attempt.result != Result::CreationFailed || !a_attempt.bytecode || retry == delays.size())
				return a_attempt.result;
			if (!a_wait(delays[retry]))
				return Result::Stale;
		}
	}
}
