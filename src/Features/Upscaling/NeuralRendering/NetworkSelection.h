#pragma once

#include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kNetworkDefault = 0;
	inline constexpr std::uint32_t kNetworkLegacy = 1;
	inline constexpr std::uint32_t kNetworkNewer = 2;

	/** Explicit network choices require the 310.9 runtime family. */
	[[nodiscard]] constexpr bool SupportsNetworkSelection(std::uint32_t major, std::uint32_t minor) noexcept
	{
		return major == 310 && minor == 9;
	}

	/** Invalid persisted choices return to Default. */
	[[nodiscard]] constexpr std::uint32_t SanitizeNetworkSelection(std::uint32_t selection) noexcept
	{
		return selection <= kNetworkNewer ? selection : kNetworkDefault;
	}

	/** Saved Default selects lightweight preset B; Legacy selects A, independently of appearance style. */
	[[nodiscard]] constexpr std::uint32_t ResolveNetworkPreset(std::uint32_t selection, bool supported) noexcept
	{
		return supported ? (SanitizeNetworkSelection(selection) == kNetworkLegacy ? kNetworkLegacy : kNetworkNewer) : kNetworkDefault;
	}
}
