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

	/// Selects a cooperative priority for CPU-intensive background workers.
	/// Windows combines process and thread priority, so BELOW_NORMAL is still
	/// higher than ordinary desktop applications when Skyrim itself is High.
	int GetCooperativeBackgroundThreadPriority(DWORD a_processPriorityClass);

	/// Applies the process-class-aware cooperative priority to the calling thread.
	bool SetCurrentThreadCooperativeBackgroundPriority();

	/** Windows user-owned capture roots; unavailable only when shell discovery fails. */
	std::optional<std::filesystem::path> GetPicturesPath();
	std::optional<std::filesystem::path> GetVideosPath();
}  // namespace Util
