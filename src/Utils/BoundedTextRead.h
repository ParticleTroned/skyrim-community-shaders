#pragma once

#include <algorithm>
#include <array>
#include <istream>
#include <limits>
#include <string>

namespace Util::BoundedTextRead
{
	enum class Result
	{
		Success,
		LimitExceeded,
		ReadError,
	};

	/**
	 * Retains at most maximumBytes and may consume one additional byte to
	 * distinguish an exact-boundary stream from an oversized stream.
	 */
	[[nodiscard]] inline Result Read(
		std::istream& a_input,
		std::size_t a_maximumBytes,
		std::string& a_contents)
	{
		a_contents.clear();
		std::array<char, 4096> buffer{};
		for (;;) {
			const std::size_t remaining = a_maximumBytes - a_contents.size();
			const std::size_t probeBudget = remaining == (std::numeric_limits<std::size_t>::max)() ?
			                                    remaining :
			                                    remaining + 1u;
			const std::size_t request = std::min(buffer.size(), probeBudget);
			if (request == 0u)
				return Result::Success;

			a_input.read(buffer.data(), static_cast<std::streamsize>(request));
			const auto bytesRead = static_cast<std::size_t>(a_input.gcount());
			if (bytesRead > remaining)
				return Result::LimitExceeded;
			a_contents.append(buffer.data(), bytesRead);

			if (a_input.bad())
				return Result::ReadError;
			if (a_input.eof())
				return Result::Success;
			if (a_input.fail() || bytesRead == 0u)
				return Result::ReadError;
		}
	}
}
