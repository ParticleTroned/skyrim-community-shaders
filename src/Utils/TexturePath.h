#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace Util
{
	/** Normalizes game texture names to lowercase paths below textures. */
	inline std::string NormalizeTexturePath(std::string path)
	{
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
		if (path.starts_with("data\\"))
			path.erase(0, 5);
		if (!path.starts_with("textures\\"))
			path = "textures\\" + path;
		return path;
	}
}
