#pragma once

#include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kCharacterMaskRoiTileSize = 32;
	inline constexpr std::uint32_t kCharacterMaskRoiMaximumExtent = 16384;

	/** GPU diagnostic ABI: nonzero-pixel enclosure in each row-major tile. */
	struct CharacterMaskRoiTileBounds
	{
		std::uint32_t minX = 0;
		std::uint32_t minY = 0;
		std::uint32_t maxX = 0;
		std::uint32_t maxY = 0;
		bool operator==(const CharacterMaskRoiTileBounds&) const = default;
	};
	static_assert(sizeof(CharacterMaskRoiTileBounds) == 16);
}
