#pragma once

#include <filesystem>

namespace Util
{
	/** Resolve the deepest existing parent, including virtual mappings and links; invalid paths fail closed. */
	bool IsPathWithinDirectory(const std::filesystem::path& basePath, const std::filesystem::path& testPath);
}
