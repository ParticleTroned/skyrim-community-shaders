#pragma once

#include <array>
#include <chrono>
#include <winrt/base.h>

struct ID3DBlob;

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
		winrt::com_ptr<ID3DBlob> bytecode;
	};

	/** Retry only runtime creation, retaining one dispatch slot and the exact bytecode. */
	template <class Perform, class Wait>
	Result Run(Attempt& a_attempt, Perform&& a_perform, Wait&& a_wait)
	{
		constexpr std::array delays{ std::chrono::milliseconds(100), std::chrono::milliseconds(500), std::chrono::milliseconds(2000) };
		for (size_t retry = 0;; ++retry) {
			a_perform();
			if (a_attempt.result != Result::CreationFailed || !a_attempt.bytecode || retry == delays.size())
				return a_attempt.result;
			if (!a_wait(delays[retry]))
				return Result::Stale;
		}
	}
}
