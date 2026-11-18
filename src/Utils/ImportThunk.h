#pragma once

#include <cstdint>
#include <cstring>
#include <span>

namespace Util
{
	/** Verifies a complete x64 import jump resolves to the expected IAT slot. */
	inline bool IsImportJumpThunk(std::span<const std::uint8_t> a_code,
		std::uintptr_t a_address, std::uintptr_t a_import) noexcept
	{
		if (a_code.size() < 6 || !a_address || !a_import || a_code[0] != 0xFF || a_code[1] != 0x25)
			return false;
		std::int32_t displacement{};
		std::memcpy(&displacement, a_code.data() + 2, sizeof(displacement));
		return a_address + 6 + displacement == a_import;
	}
}
