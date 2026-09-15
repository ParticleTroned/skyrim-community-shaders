#pragma once

#include <filesystem>
#include <optional>

namespace Util
{
	std::optional<REL::Version> GetDllVersion(const std::wstring& dllPath);

	/// Returns the number of logical processors on the highest-efficiency cores
	/// (P-cores on Intel hybrid CPUs). On non-hybrid CPUs all cores share the
	/// same efficiency class, so this returns std::thread::hardware_concurrency().
	/// Falls back to hardware_concurrency() on any API failure.
	uint32_t GetPerformanceCoreCount();
	/** Windows capture roots, absent when shell discovery fails. */
	std::optional<std::filesystem::path> GetPicturesPath();
	std::optional<std::filesystem::path> GetVideosPath();
}  // namespace Util
