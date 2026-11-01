#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace CSX::Screenshot
{
	inline constexpr std::uint32_t MaximumDimension = 16384;
	inline constexpr std::uint64_t MaximumJobBytes = 128ull * 1024 * 1024;
	inline constexpr std::uint64_t MaximumStagingBytes = 512ull * 1024 * 1024;
	inline constexpr std::uint32_t NormalJobCapacity = 2;
	struct PixelRect { std::uint32_t x, y, width, height; };
	/** Validate normalized bounds before integer conversion, including under fast floating-point builds. */
	inline PixelRect ResolveCrop(std::uint32_t width, std::uint32_t height, float x, float y, float w, float h)
	{
		const auto finite = [](float value) { return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u; };
		if (!width || !height || width > MaximumDimension || height > MaximumDimension ||
			!finite(x) || !finite(y) || !finite(w) || !finite(h) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
			double(x) + w > 1.0 + 1e-7 || double(y) + h > 1.0 + 1e-7)
			throw std::invalid_argument("capture crop is outside the native source");
		const auto left = std::min(width - 1, static_cast<std::uint32_t>(std::lround(double(x) * width)));
		const auto top = std::min(height - 1, static_cast<std::uint32_t>(std::lround(double(y) * height)));
		const auto right = std::clamp(static_cast<std::uint32_t>(std::lround((double(x) + w) * width)), left + 1, width);
		const auto bottom = std::clamp(static_cast<std::uint32_t>(std::lround((double(y) + h) * height)), top + 1, height);
		return { left, top, right - left, bottom - top };
	}
	inline bool CanReserve(std::uint64_t current, std::uint64_t bytes) noexcept
	{
		return bytes > 0 && bytes <= MaximumJobBytes && current <= MaximumStagingBytes && bytes <= MaximumStagingBytes - current;
	}
}
