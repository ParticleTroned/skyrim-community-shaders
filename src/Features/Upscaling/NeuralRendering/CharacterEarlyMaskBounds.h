#pragma once

#include "CharacterMaskRoi.h"

#include <cmath>

namespace NeuralRendering
{
	/** Projects a current-source category superset through the mask sampling footprint. */
	[[nodiscard]] inline bool MapEarlyCharacterMaskBounds(
		std::span<const CharacterMaskRoiTileBounds> a_sourceTiles,
		std::uint32_t a_sourceWidth, std::uint32_t a_sourceHeight,
		ComputeSubrect a_inputCrop, std::uint32_t a_outputWidth, std::uint32_t a_outputHeight,
		float a_jitterX, float a_jitterY, std::uint32_t a_featherRadius,
		std::vector<CharacterMaskRoiTileBounds>& a_outputTiles)
	{
		a_outputTiles.clear();
		if (!a_sourceWidth || !a_sourceHeight || !a_outputWidth || !a_outputHeight ||
			std::max({ a_sourceWidth, a_sourceHeight, a_outputWidth, a_outputHeight }) > kCharacterMaskRoiMaximumExtent ||
			!a_inputCrop.Fits(a_sourceWidth, a_sourceHeight) ||
			!std::isfinite(a_jitterX) || !std::isfinite(a_jitterY) || a_featherRadius > 4u)
			return false;
		const auto sourceColumns = (a_sourceWidth + 31u) / 32u;
		const auto sourceRows = (a_sourceHeight + 31u) / 32u;
		if (a_sourceTiles.size() != static_cast<std::size_t>(sourceColumns) * sourceRows)
			return false;
		const auto outputColumns = (a_outputWidth + 31u) / 32u;
		const auto outputRows = (a_outputHeight + 31u) / 32u;
		a_outputTiles.resize(static_cast<std::size_t>(outputColumns) * outputRows);
		const auto guard = [&](float jitter, std::uint32_t extent) {
			// Bilinear reconstruction and continuous feathering read at most
			// radius+2 source pixels away; jitter can shift either edge outward.
			return a_featherRadius + 2u + static_cast<std::uint32_t>(std::min(static_cast<double>(extent), std::ceil(std::abs(static_cast<double>(jitter)))));
		};
		const auto mapAxis = [](std::uint32_t minimum, std::uint32_t maximum,
								 std::uint32_t cropBase, std::uint32_t cropExtent,
								 std::uint32_t outputExtent, std::uint32_t padding) {
			const auto left = minimum - cropBase;
			const auto right = maximum - cropBase;
			const auto paddedLeft = left > padding ? left - padding : 0u;
			const auto paddedRight = std::min<std::uint64_t>(cropExtent, static_cast<std::uint64_t>(right) + padding);
			return std::array{
				static_cast<std::uint32_t>(static_cast<std::uint64_t>(paddedLeft) * outputExtent / cropExtent),
				static_cast<std::uint32_t>((paddedRight * outputExtent + cropExtent - 1u) / cropExtent),
			};
		};
		const auto guardX = guard(a_jitterX, a_inputCrop.width);
		const auto guardY = guard(a_jitterY, a_inputCrop.height);
		for (std::size_t index = 0; index < a_sourceTiles.size(); ++index) {
			const auto& tile = a_sourceTiles[index];
			if (tile == CharacterMaskRoiTileBounds{})
				continue;
			const auto tileX = static_cast<std::uint32_t>(index % sourceColumns) * 32u;
			const auto tileY = static_cast<std::uint32_t>(index / sourceColumns) * 32u;
			if (tile.minX >= tile.maxX || tile.minY >= tile.maxY || tile.minX < tileX || tile.minY < tileY ||
				tile.maxX > std::min(tileX + 32u, a_sourceWidth) || tile.maxY > std::min(tileY + 32u, a_sourceHeight)) {
				a_outputTiles.clear();
				return false;
			}
			const auto minX = std::max(tile.minX, a_inputCrop.baseX);
			const auto minY = std::max(tile.minY, a_inputCrop.baseY);
			const auto maxX = std::min(tile.maxX, a_inputCrop.baseX + a_inputCrop.width);
			const auto maxY = std::min(tile.maxY, a_inputCrop.baseY + a_inputCrop.height);
			if (minX >= maxX || minY >= maxY)
				continue;
			const auto x = mapAxis(minX, maxX, a_inputCrop.baseX, a_inputCrop.width, a_outputWidth, guardX);
			const auto y = mapAxis(minY, maxY, a_inputCrop.baseY, a_inputCrop.height, a_outputHeight, guardY);
			for (auto row = y[0] / 32u; row <= (y[1] - 1u) / 32u; ++row) {
				for (auto column = x[0] / 32u; column <= (x[1] - 1u) / 32u; ++column) {
					CharacterMaskRoiTileBounds part{
						std::max(x[0], column * 32u),
						std::max(y[0], row * 32u),
						std::min(x[1], (column + 1u) * 32u),
						std::min(y[1], (row + 1u) * 32u),
					};
					auto& destination = a_outputTiles[row * outputColumns + column];
					if (destination == CharacterMaskRoiTileBounds{})
						destination = part;
					else
						destination = { std::min(destination.minX, part.minX), std::min(destination.minY, part.minY),
							std::max(destination.maxX, part.maxX), std::max(destination.maxY, part.maxY) };
				}
			}
		}
		return true;
	}
}
