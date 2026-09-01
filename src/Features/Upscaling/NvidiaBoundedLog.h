#pragma once

#include "NvidiaPipelinePolicy.h"

#include <cstddef>
#ifdef _MSC_VER
#	include <excpt.h>
#endif

namespace CSX::NvidiaBoundedLog
{
	[[nodiscard]] inline NvidiaPipelinePolicy::BoundedCopyResult Copy(
		const char* a_message,
		char* a_destination,
		std::size_t a_capacity) noexcept
	{
		if (!a_message || !a_destination || a_capacity < 2)
			return NvidiaPipelinePolicy::BoundedCopyResult::Unreadable;
#ifdef _MSC_VER
		__try {
#endif
			std::size_t length = 0;
			while (length + 1 < a_capacity && a_message[length] != '\0') {
				a_destination[length] = a_message[length];
				++length;
			}
			a_destination[length] = '\0';
			return NvidiaPipelinePolicy::ClassifyBoundedCopy(
				true, a_message[length] == '\0');
#ifdef _MSC_VER
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_destination[0] = '\0';
			return NvidiaPipelinePolicy::BoundedCopyResult::Unreadable;
		}
#endif
	}
}
