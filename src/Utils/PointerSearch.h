#pragma once

#include <cstddef>

namespace Util::PointerSearch
{
	/** @brief Whether both the CPU and operating system support AVX2 execution. */
	bool HasAvx2() noexcept;

	/** @brief Search a contiguous array of pointer-sized values without reading beyond its end.
	 * Requires HasAvx2() and a readable array of a_count pointers; null storage is allowed only when empty.
	 */
	bool ContainsAvx2(const void* a_data, std::size_t a_count, const void* a_value) noexcept;
}
