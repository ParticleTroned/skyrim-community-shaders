#pragma once

#include <array>
#include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::array<const char*, 3> kStyleNames{ "Standard", "Natural", "Cinematic" };
	inline constexpr std::uint32_t kMaximumStyle = static_cast<std::uint32_t>(kStyleNames.size() - 1);
	inline constexpr std::uint32_t kDefaultStyle = 1;

	/** Keep persisted styles within the provider's three distinct appearances. */
	[[nodiscard]] constexpr std::uint32_t ClampStyle(std::uint32_t style) noexcept
	{
		return style <= kMaximumStyle ? style : kMaximumStyle;
	}
}
