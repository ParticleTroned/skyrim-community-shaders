#include "SystemMemory.h"

#include <Windows.h>

#include <Psapi.h>
#include <algorithm>
#include <limits>
#include <spdlog/spdlog.h>

#pragma comment(lib, "Psapi.lib")

namespace
{
	constexpr std::uint64_t kMiB = 1024ull * 1024ull;

	struct PagefileSample
	{
		std::uint64_t totalPages = 0;
		std::uint64_t usedPages = 0;
		std::uint32_t count = 0;
		bool overflow = false;
	};

	BOOL CALLBACK AccumulatePagefile(LPVOID a_context, PENUM_PAGE_FILE_INFORMATION a_information, LPCWSTR)
	{
		auto& sample = *static_cast<PagefileSample*>(a_context);
		constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
		if (a_information->TotalSize > maximum - sample.totalPages ||
			a_information->TotalInUse > maximum - sample.usedPages ||
			sample.count == std::numeric_limits<std::uint32_t>::max()) {
			sample.overflow = true;
			return FALSE;
		}
		sample.totalPages += a_information->TotalSize;
		sample.usedPages += a_information->TotalInUse;
		++sample.count;
		return TRUE;
	}
}

namespace Util
{
	SystemMemorySample QuerySystemMemory() noexcept
	{
		PERFORMANCE_INFORMATION information{};
		information.cb = sizeof(information);
		if (!::GetPerformanceInfo(&information, sizeof(information)))
			return { .error = ::GetLastError() };

		constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
		if (information.PageSize == 0 || information.CommitLimit == 0 ||
			information.CommitTotal > maximum / information.PageSize ||
			information.CommitLimit > maximum / information.PageSize ||
			information.PhysicalTotal > maximum / information.PageSize ||
			information.PhysicalAvailable > maximum / information.PageSize) {
			return { .error = ERROR_INVALID_DATA };
		}
		return {
			.valid = true,
			.commitUsedBytes = static_cast<std::uint64_t>(information.CommitTotal) * information.PageSize,
			.commitLimitBytes = static_cast<std::uint64_t>(information.CommitLimit) * information.PageSize,
			.physicalTotalBytes = static_cast<std::uint64_t>(information.PhysicalTotal) * information.PageSize,
			.physicalAvailableBytes = static_cast<std::uint64_t>(information.PhysicalAvailable) * information.PageSize,
		};
	}

	void LogSystemMemory(std::string_view a_context)
	{
		if (!spdlog::should_log(spdlog::level::info))
			return;

		ULONGLONG installedKiB = 0;
		if (::GetPhysicallyInstalledSystemMemory(&installedKiB)) {
			SPDLOG_INFO("[Memory][{}] Installed RAM={} MiB.", a_context, installedKiB / 1024u);
		} else {
			SPDLOG_INFO("[Memory][{}] Installed RAM unavailable: Windows error {}.", a_context, ::GetLastError());
		}

		const auto sample = QuerySystemMemory();
		if (sample.valid) {
			const auto headroom = sample.commitLimitBytes - std::min(sample.commitUsedBytes, sample.commitLimitBytes);
			SPDLOG_INFO("[Memory][{}] RAM usable={} MiB available={} MiB; system commit used={} MiB limit={} MiB headroom={} MiB.",
				a_context, sample.physicalTotalBytes / kMiB, sample.physicalAvailableBytes / kMiB,
				sample.commitUsedBytes / kMiB, sample.commitLimitBytes / kMiB, headroom / kMiB);
		} else {
			SPDLOG_INFO("[Memory][{}] RAM/commit sample unavailable: Windows error {}.", a_context, sample.error);
		}

		SYSTEM_INFO system{};
		::GetSystemInfo(&system);
		PagefileSample pagefiles{};
		const bool enumerated = ::EnumPageFilesW(AccumulatePagefile, &pagefiles) != FALSE;
		const auto error = enumerated ? ERROR_SUCCESS : ::GetLastError();
		constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
		const bool invalidSize = pagefiles.overflow || system.dwPageSize == 0 ||
		                         pagefiles.totalPages > maximum / system.dwPageSize ||
		                         pagefiles.usedPages > maximum / system.dwPageSize;
		if (!enumerated || invalidSize) {
			SPDLOG_INFO("[Memory][{}] Pagefile totals unavailable: Windows error {} invalidSize={}.",
				a_context, error, invalidSize);
			return;
		}
		SPDLOG_INFO("[Memory][{}] Pagefiles count={} allocated={} MiB inUse={} MiB (current allocation, not configured maximum).",
			a_context, pagefiles.count, pagefiles.totalPages * system.dwPageSize / kMiB,
			pagefiles.usedPages * system.dwPageSize / kMiB);
	}
}
