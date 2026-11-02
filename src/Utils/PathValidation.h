#pragma once

#include <filesystem>

namespace Util
{
	/** Existing symlinks and junctions are resolved; invalid paths fail closed. */
	bool IsPathWithinDirectory(const std::filesystem::path& basePath, const std::filesystem::path& testPath);
}
