#include "PointerSearch.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace Util::PointerSearch
{
#if defined(__clang__) || defined(__GNUC__)
	__attribute__((target("avx2"), noinline))
#else
	__declspec(noinline)
#endif
	bool ContainsAvx2(const void* a_data, std::size_t a_count, const void* a_value) noexcept
	{
		static_assert(sizeof(void*) == sizeof(std::int64_t));
		if (a_count == 0)
			return false;
		const auto* bytes = static_cast<const std::byte*>(a_data);
		const auto value = reinterpret_cast<std::uintptr_t>(a_value);
		const auto target = _mm256_set1_epi64x(std::bit_cast<std::int64_t>(value));
		std::size_t i = 0;
		for (; a_count - i >= 4; i += 4) {
			const auto pointers = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bytes + i * sizeof(void*)));
			if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(pointers, target)) != 0) {
				_mm256_zeroupper();
				return true;
			}
		}
		_mm256_zeroupper();
		for (; i < a_count; ++i) {
			std::uintptr_t pointer;
			std::memcpy(&pointer, bytes + i * sizeof(void*), sizeof(pointer));
			if (pointer == value)
				return true;
		}
		return false;
	}
}
