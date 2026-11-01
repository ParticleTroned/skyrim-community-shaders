#pragma once

#include <cstddef>
#ifdef _MSC_VER
#	include <excpt.h>
#endif

namespace CSX::NvidiaBoundedLog
{
	enum class CopyResult { Complete, Truncated, Unreadable };
	[[nodiscard]] inline CopyResult Copy(
		const char* a_message,
		char* a_destination,
		std::size_t a_capacity) noexcept
	{
		if (!a_message || !a_destination || a_capacity < 2)
			return CopyResult::Unreadable;
#ifdef _MSC_VER
		__try {
#endif
			std::size_t length = 0;
			while (length + 1 < a_capacity && a_message[length] != '\0') {
				a_destination[length] = a_message[length];
				++length;
			}
			a_destination[length] = '\0';
			return a_message[length] == '\0' ? CopyResult::Complete : CopyResult::Truncated;
#ifdef _MSC_VER
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_destination[0] = '\0';
			return CopyResult::Unreadable;
		}
#endif
	}
}
