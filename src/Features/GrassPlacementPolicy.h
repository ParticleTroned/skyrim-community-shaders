#pragma once

#include "GrassPlacementSettings.h"
#include <optional>
#include <span>
#include <utility>

namespace GrassPlacement
{
	struct Layer
	{
		Settings settings;
		bool layered = true;
	};

	/** Clamp only the final sum so narrower corrections can undo broader adjustments. */
	inline float Resolve(const Settings& base, std::span<const Layer* const> layers, std::string_view model)
	{
		const auto sum = [&](const Settings& settings) {
			const auto found = settings.modelOffsets.find(model);
			return settings.verticalOffset + (found == settings.modelOffsets.end() ? 0 : found->second);
		};
		const float global = sum(base);
		float result = global;
		for (const auto* layer : layers) result = (layer->layered ? result : global) + sum(layer->settings);
		return Clamp(result);
	}

	/** Keep native recomputation authoritative and never accumulate our own padding. */
	struct Envelope
	{
		float base = 0, applied = 0;
		bool known = false;
		void Apply(float& value, float padding)
		{
			if (!known || value != applied)
				base = value;
			known = std::isfinite(base) && base >= 0;
			if (known)
				value = applied = base + padding;
		}
		void Restore(float& value)
		{
			if (known && value == applied)
				value = base;
			known = false;
		}
	};

	using CellKey = std::pair<int32_t, int32_t>;
	inline std::optional<CellKey> SourceCell(float x, float y)
	{
		const double cellX = std::floor(double(x) / 4096.0), cellY = std::floor(double(y) / 4096.0);
		if (!std::isfinite(cellX) || !std::isfinite(cellY) ||
			cellX < INT16_MIN || cellX > INT16_MAX || cellY < INT16_MIN || cellY > INT16_MAX)
			return std::nullopt;
		return CellKey{ int32_t(cellX), int32_t(cellY) };
	}
}
