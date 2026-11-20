#pragma once

#include <array>
#include <cstddef>

namespace Util
{
	/** Visit broad-to-specific location layers with a bounded, cycle-safe ancestry walk. */
	template <class World, class Location, class Cell, class Visitor>
	void VisitLocationLayers(const World* world, const Location* location, const Cell* cell, Visitor visit)
	{
		const auto parents = [&](const auto* first, auto parent) {
			std::array<decltype(first), 64> chain{};
			size_t count = 0;
			for (auto* item = first; item && count < chain.size(); item = parent(item)) {
				bool seen = false;
				for (size_t i = 0; i < count; ++i) seen |= chain[i] == item;
				if (seen)
					break;
				chain[count++] = item;
			}
			while (count) visit(chain[--count]);
		};
		parents(world, [](const World* item) { return item->parentWorld; });
		parents(location, [](const Location* item) { return item->parentLoc; });
		if (cell)
			visit(cell);
	}
}
