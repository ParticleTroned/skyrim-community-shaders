#pragma once

#include <filesystem>
#include <optional>
#include <ShlObj.h>

namespace Util
{
	std::optional<REL::Version> GetDllVersion(const std::wstring& dllPath);

	/// Returns the number of logical processors on the highest-efficiency cores
	/// (P-cores on Intel hybrid CPUs). On non-hybrid CPUs all cores share the
	/// same efficiency class, so this returns std::thread::hardware_concurrency().
	/// Falls back to hardware_concurrency() on any API failure.
	uint32_t GetPerformanceCoreCount();

	/** Resolves a redirected per-user Windows Known Folder. */
	std::optional<std::filesystem::path> GetWindowsKnownFolderPath(REFKNOWNFOLDERID folderId);
}  // namespace Util
