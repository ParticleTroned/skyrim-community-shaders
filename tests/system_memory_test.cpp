#include <Windows.h>

#include <Psapi.h>
#include <array>
#include <iostream>
#include <limits>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <stdexcept>

namespace
{
	PERFORMANCE_INFORMATION performance{};
	bool performanceAvailable = true;
	bool installedAvailable = true;
	bool pagefilesAvailable = true;
	DWORD pageSize = 4096;
	std::array<ENUM_PAGE_FILE_INFORMATION, 2> fixturePagefiles{};
	std::size_t pagefileCount = 0;

	BOOL ReadPerformance(PPERFORMANCE_INFORMATION a_result, DWORD)
	{
		if (!performanceAvailable) {
			::SetLastError(ERROR_ACCESS_DENIED);
			return FALSE;
		}
		*a_result = performance;
		return TRUE;
	}

	BOOL ReadInstalled(PULONGLONG a_result)
	{
		if (!installedAvailable) {
			::SetLastError(ERROR_INVALID_DATA);
			return FALSE;
		}
		*a_result = 32ull * 1024ull * 1024ull;
		return TRUE;
	}

	BOOL ReadPagefiles(PENUM_PAGE_FILE_CALLBACKW a_callback, LPVOID a_context)
	{
		for (std::size_t i = 0; i < pagefileCount; ++i) {
			if (!a_callback(a_context, &fixturePagefiles[i], L"private-pagefile-name"))
				return FALSE;
		}
		if (!pagefilesAvailable)
			::SetLastError(ERROR_ACCESS_DENIED);
		return pagefilesAvailable ? TRUE : FALSE;
	}

	void ReadSystemInfo(LPSYSTEM_INFO a_result)
	{
		a_result->dwPageSize = pageSize;
	}

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}
}

#undef GetPerformanceInfo
#undef EnumPageFilesW
#define GetPerformanceInfo ReadPerformance
#define GetPhysicallyInstalledSystemMemory ReadInstalled
#define EnumPageFilesW ReadPagefiles
#define GetSystemInfo ReadSystemInfo
#include "Utils/SystemMemory.cpp"
#undef GetPerformanceInfo
#undef GetPhysicallyInstalledSystemMemory
#undef EnumPageFilesW
#undef GetSystemInfo

int main()
{
	try {
		performance.PageSize = 4096;
		performance.CommitTotal = 12ull * 1024ull * 256ull;
		performance.CommitLimit = 48ull * 1024ull * 256ull;
		performance.PhysicalTotal = 32ull * 1024ull * 256ull;
		performance.PhysicalAvailable = 4ull * 1024ull * 256ull;
		const auto valid = Util::QuerySystemMemory();
		Require(valid.valid && valid.commitUsedBytes == 12ull * 1024ull * 1024ull * 1024ull &&
					valid.commitLimitBytes == 48ull * 1024ull * 1024ull * 1024ull &&
					valid.physicalAvailableBytes == 4ull * 1024ull * 1024ull * 1024ull,
			"Windows page counters must be converted to bytes");
		performanceAvailable = false;
		const auto failed = Util::QuerySystemMemory();
		Require(!failed.valid && failed.error == ERROR_ACCESS_DENIED, "Query failures must retain their Windows error");
		performanceAvailable = true;
		performance.PageSize = 0;
		Require(!Util::QuerySystemMemory().valid, "A zero page size must fail safely");
		performance.PageSize = 4096;
		performance.CommitLimit = std::numeric_limits<SIZE_T>::max();
		Require(!Util::QuerySystemMemory().valid, "Byte conversion overflow must remain unavailable");
		performance.CommitLimit = 48ull * 1024ull * 256ull;

		std::ostringstream output;
		auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
		auto log = std::make_shared<spdlog::logger>("memory-test", sink);
		spdlog::set_default_logger(log);
		const auto capture = [&]() {
			output.str({});
			Util::LogSystemMemory("test");
			return output.str();
		};
		const auto noPagefile = capture();
		Require(noPagefile.find("count=0 allocated=0 MiB inUse=0 MiB") != std::string::npos,
			"A system without pagefiles must report a valid zero");
		Require(noPagefile.find("Installed RAM=32768 MiB") != std::string::npos &&
					noPagefile.find("commit used=12288 MiB limit=49152 MiB headroom=36864 MiB") != std::string::npos,
			"Startup logs must distinguish RAM from commit capacity");
		performance.CommitTotal = performance.CommitLimit + 1;
		Require(capture().find("headroom=0 MiB") != std::string::npos,
			"Commit pressure must not underflow headroom when the limit changes");
		performance.CommitTotal = 12ull * 1024ull * 256ull;
		performanceAvailable = false;
		const auto missingCommit = capture();
		Require(missingCommit.find("RAM/commit sample unavailable: Windows error 5") != std::string::npos &&
					missingCommit.find("Pagefiles count=0") != std::string::npos,
			"Commit sample failure must preserve independently available inventory");
		performanceAvailable = true;
		pagefileCount = 2;
		fixturePagefiles[0].TotalSize = 1024 * 256;
		fixturePagefiles[0].TotalInUse = 128 * 256;
		fixturePagefiles[1].TotalSize = 3072 * 256;
		fixturePagefiles[1].TotalInUse = 256 * 256;
		const auto multiple = capture();
		Require(multiple.find("count=2 allocated=4096 MiB inUse=384 MiB") != std::string::npos &&
					multiple.find("private-pagefile-name") == std::string::npos,
			"Pagefiles must be aggregated without recording their paths");
		pagefilesAvailable = false;
		const auto partial = capture();
		Require(partial.find("Pagefile totals unavailable: Windows error 5") != std::string::npos &&
					partial.find("Pagefiles count=") == std::string::npos,
			"Partial enumeration must never be reported as a complete total");
		pagefilesAvailable = true;
		fixturePagefiles[0].TotalSize = std::numeric_limits<SIZE_T>::max();
		Require(capture().find("invalidSize=true") != std::string::npos, "Pagefile accumulation must detect overflow");
		pagefileCount = 0;
		pageSize = 0;
		Require(capture().find("invalidSize=true") != std::string::npos, "Zero page size must not divide by zero");
		pageSize = 4096;
		installedAvailable = false;
		Require(capture().find("Installed RAM unavailable") != std::string::npos,
			"Unavailable installed RAM must not be reported as zero");
		log->set_level(spdlog::level::warn);
		Require(capture().empty(), "Memory inventory must honor the configured logging level");
		std::cout << "System memory query and logging checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
