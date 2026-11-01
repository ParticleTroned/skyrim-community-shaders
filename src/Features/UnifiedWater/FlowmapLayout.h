#pragma once
#include <cstdint>

namespace WaterFlowmapLayout
{
	constexpr std::int32_t MaxCells = 16384 / 64;

	constexpr bool ValidCells(std::int32_t width, std::int32_t height) noexcept
	{
		return width > 0 && height > 0 && width <= MaxCells && height <= MaxCells;
	}

	constexpr bool MatchesTexture(std::uint32_t width, std::uint32_t height,
		std::int32_t cellsX, std::int32_t cellsY) noexcept
	{
		if (!ValidCells(cellsX, cellsY))
			return false;
		for (std::uint32_t mip = 0; mip < 6; ++mip) {
			if (width == (static_cast<std::uint32_t>(cellsX) * 64 >> mip) &&
				height == (static_cast<std::uint32_t>(cellsY) * 64 >> mip))
				return true;
		}
		return false;
	}
}
