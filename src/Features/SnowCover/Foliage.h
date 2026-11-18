#pragma once

#include "Utils/StringUtils.h"

namespace SnowFoliage
{
	/** Bounded fallback for static plants using conventional model directories. */
	inline bool IsVegetationModel(std::string_view path)
	{
		for (auto end = path.find_first_of("/\\"); end != std::string_view::npos; end = path.find_first_of("/\\")) {
			const auto component = path.substr(0, end);
			if ((component.size() == 6 && Util::IEndsWithAsciiInsensitive(component, "plants")) ||
				(component.size() == 5 && Util::IEndsWithAsciiInsensitive(component, "trees")))
				return true;
			path.remove_prefix(end + 1);
		}
		return false;
	}

	/** Plant references admit one-sided cards; model-path fallbacks require two sides. */
	inline bool IsFoliageCard(bool alphaTested, bool twoSided, bool softLighting, bool plant, bool plantReference = false)
	{
		return alphaTested && (softLighting || (plant && (twoSided || plantReference)));
	}
}
