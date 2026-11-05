#pragma once

#include <cstdint>

namespace Util::ShaderDescriptor
{
	/** @brief Keeps explicitly runtime-read bits when removing compile-time lookup bits. */
	[[nodiscard]] constexpr std::uint32_t UploadedPixelDescriptor(
		std::uint32_t a_requested, std::uint32_t a_lookup, std::uint32_t a_runtimeBits)
	{
		return a_requested & ~(a_lookup & ~a_runtimeBits);
	}
}
