#pragma once

#include "Utils/StringUtils.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

namespace TerrainVariationPolicy
{
	/** @brief Normalizes game texture names to case-insensitive, texture-relative paths. */
	inline std::string CanonicaliseTexturePath(std::string_view a_path)
	{
		auto canonical = Util::ToLowerAscii(a_path);
		std::replace(canonical.begin(), canonical.end(), '\\', '/');
		if (canonical.starts_with("data/"))
			canonical.erase(0, 5);
		if (canonical.starts_with("textures/"))
			canonical.erase(0, 9);
		return canonical;
	}

	/** @brief Matches landscape maps while keeping tree textures and ambiguous paths unchanged. */
	inline bool IsLandscapeDiffusePath(const std::string& a_canonical, const std::unordered_set<std::string>& a_landscapePaths)
	{
		if (a_canonical.empty() || a_canonical.ends_with('/') || a_canonical.starts_with('/') || a_canonical.find(':') != std::string::npos ||
			a_canonical.find("//") != std::string::npos || a_canonical.starts_with("./") ||
			a_canonical.starts_with("../") || a_canonical.find("/./") != std::string::npos ||
			a_canonical.find("/../") != std::string::npos || a_canonical.ends_with("/.") || a_canonical.ends_with("/..") ||
			a_canonical.starts_with("landscape/trees/"))
			return false;
		return a_canonical.starts_with("landscape/") || a_landscapePaths.contains(a_canonical);
	}
}
