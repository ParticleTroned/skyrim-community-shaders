#pragma once

#include "Utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace NrReplay::ProviderFloor
{
	inline constexpr std::string_view kProviderSha256 = "8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206";
	inline constexpr std::string_view kCodeSha256 = "0d0543585e6765a87886678efbdb6462f02be2a3b6c26c9454c0e9f304194dc5";
	inline constexpr std::size_t kCodeRva = 0x1000, kCodeBytes = 700416, kModuleBytes = 165945344;
	inline constexpr std::size_t kGuardRva = 0x3c837, kPatchRva = 0x3c83e;
	inline constexpr std::array<std::uint8_t, 39> kGuard{
		0x44, 0x2b, 0xd2, 0x99, 0x41, 0xf7, 0xfb, 0xb8, 0x40, 0x01, 0x00, 0x00, 0x44,
		0x2b, 0xca, 0x44, 0x3b, 0xd0, 0x44, 0x0f, 0x4c, 0xd0, 0x44, 0x3b, 0xc8, 0x44,
		0x89, 0x93, 0x80, 0x02, 0x00, 0x00, 0x44, 0x0f, 0x4c, 0xc8, 0x41, 0x8b, 0xc2
	};
	inline constexpr std::array<std::uint8_t, 5> kOriginal{ 0xb8, 0x40, 0x01, 0x00, 0x00 };
	inline constexpr std::array<std::uint8_t, 5> kReplacement{ 0xb8, 0x00, 0x01, 0x00, 0x00 };

	inline void Require(bool value, std::string_view reason)
	{
		if (!value)
			throw std::runtime_error(std::string(reason));
	}
	/** Canonicalizes representation without admitting whitespace, prefixes or non-hex digits. */
	inline std::string CanonicalSha256(std::string_view hash)
	{
		Require(hash.size() == 64 && std::ranges::all_of(hash, [](char digit) {
			return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F');
		}),
			"provider floor identity must be exactly 64 hexadecimal digits");
		return Util::ToLowerAscii(hash);
	}
	/** Admits only the pinned provider and one explicitly requested experimental floor. */
	inline void ValidateIdentity(unsigned floor, std::string_view provider, std::string_view code)
	{
		Require(floor == 256, "experimental provider floor must be exactly 256");
		Require(provider == kProviderSha256, "experimental provider floor requires the pinned provider disk SHA256");
		Require(code == kCodeSha256, "experimental provider floor requires the pinned unmodified code SHA256");
	}
	/** Validates the entire instruction window, including the shared clamp operand. */
	inline void ValidateGuard(std::size_t patchRva, std::span<const std::uint8_t> guard)
	{
		Require(patchRva == kPatchRva && guard.size() == kGuard.size() && std::ranges::equal(guard, kGuard),
			"experimental provider floor instruction guard mismatch");
	}
	/** Rejects unreadable, writable, non-image or foreign allocations before touching code. */
	inline void ValidateMemory(std::size_t moduleBytes, std::size_t rva, std::size_t length,
		bool committedImage, bool sameAllocation, bool executableReadOnly, bool guarded)
	{
		Require(moduleBytes == kModuleBytes && length && rva < moduleBytes && length <= moduleBytes - rva &&
					committedImage && sameAllocation && executableReadOnly && !guarded,
			"experimental provider floor memory guard rejected the loaded module");
	}
	struct Shape
	{
		unsigned width, height;
		bool operator==(const Shape&) const = default;
	};
	/** Models the observed floor/alignment branch; it does not measure GPU dispatch geometry. */
	inline Shape PaddedShape(unsigned width, unsigned height, unsigned floor)
	{
		Require(width && height && width <= 16384 && height <= 16384 && (floor == 256 || floor == 320),
			"provider floor shape is outside the bounded model");
		Shape result{ std::max((width + 63u) / 64u * 64u, floor), std::max((height + 63u) / 64u * 64u, floor) };
		if (result.height % 256u == 0 && result.width % 256u == 0)
			result.width += 64u;
		return result;
	}
}
