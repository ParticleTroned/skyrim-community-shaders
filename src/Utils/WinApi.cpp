#include "WinApi.h"

#include "ShaderCompilationSchedulingPolicy.h"

#include <ShlObj.h>

namespace Util
{
	namespace
	{
		std::optional<std::filesystem::path> GetKnownFolderPath(REFKNOWNFOLDERID a_folderId)
		{
			PWSTR path = nullptr;
			if (FAILED(SHGetKnownFolderPath(a_folderId, KF_FLAG_DEFAULT, nullptr, &path)) || !path)
				return std::nullopt;
			const std::filesystem::path result(path);
			CoTaskMemFree(path);
			return result;
		}
	}

	std::optional<REL::Version> GetDllVersion(const std::wstring& dllPath)
	{
		DWORD handle = 0;
		DWORD size = GetFileVersionInfoSize(dllPath.c_str(), &handle);
		if (size == 0) {
			return std::nullopt;
		}

		std::vector<BYTE> buffer(size);
		if (!GetFileVersionInfo(dllPath.c_str(), handle, size, buffer.data())) {
			return std::nullopt;
		}

		VS_FIXEDFILEINFO* fileInfo = nullptr;
		UINT fileInfoSize = 0;
		if (!VerQueryValue(buffer.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &fileInfoSize)) {
			return std::nullopt;
		}

		if (fileInfoSize == sizeof(VS_FIXEDFILEINFO)) {
			return REL::Version(HIWORD(fileInfo->dwFileVersionMS), LOWORD(fileInfo->dwFileVersionMS), HIWORD(fileInfo->dwFileVersionLS), LOWORD(fileInfo->dwFileVersionLS));
		}

		return std::nullopt;
	}

	uint32_t GetPerformanceCoreCount()
	{
		// Cache the result — CPU topology never changes at runtime.
		// C++11 guarantees thread-safe initialisation of static locals.
		static const uint32_t cached = []() -> uint32_t {
			const uint32_t fallback = std::max(1u, std::thread::hardware_concurrency());

			DWORD size = 0;
			GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
				return fallback;

			std::vector<uint8_t> buf(size);
			auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data());
			if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size))
				return fallback;

			// First pass: find the highest efficiency class present.
			BYTE maxClass = 0;
			for (DWORD offset = 0; offset < size;) {
				auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + offset);
				if (entry->Processor.EfficiencyClass > maxClass)
					maxClass = entry->Processor.EfficiencyClass;
				offset += entry->Size;
			}

			// Second pass: count logical processors on those (P-)cores.
			uint32_t count = 0;
			for (DWORD offset = 0; offset < size;) {
				auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + offset);
				if (entry->Processor.EfficiencyClass == maxClass) {
					for (WORD g = 0; g < entry->Processor.GroupCount; ++g)
						count += static_cast<uint32_t>(std::popcount(entry->Processor.GroupMask[g].Mask));
				}
				offset += entry->Size;
			}

			return count > 0 ? count : fallback;
		}();
		return cached;
	}

	int GetCooperativeBackgroundThreadPriority(DWORD a_processPriorityClass)
	{
		using namespace ShaderCompilationSchedulingPolicy;

		ProcessPriorityBand processPriorityBand = ProcessPriorityBand::Standard;
		switch (a_processPriorityClass) {
		case HIGH_PRIORITY_CLASS:
			processPriorityBand = ProcessPriorityBand::High;
			break;
		case REALTIME_PRIORITY_CLASS:
			processPriorityBand = ProcessPriorityBand::Realtime;
			break;
		case ABOVE_NORMAL_PRIORITY_CLASS:
			processPriorityBand = ProcessPriorityBand::AboveNormal;
			break;
		default:
			break;
		}

		switch (SelectCooperativeThreadPriority(processPriorityBand)) {
		case CooperativeThreadPriority::Idle:
			// HIGH + BELOW_NORMAL has base priority 12 and still outranks a normal
			// desktop application's base priority 8. IDLE is the only relative
			// priority in HIGH_PRIORITY_CLASS that yields cooperatively.
			return THREAD_PRIORITY_IDLE;
		case CooperativeThreadPriority::Lowest:
			// ABOVE_NORMAL + LOWEST has base priority 8, equal to a normal app.
			return THREAD_PRIORITY_LOWEST;
		default:
			// NORMAL + BELOW_NORMAL has base priority 7. Lower process classes are
			// already cooperative and remain safely below ordinary applications.
			return THREAD_PRIORITY_BELOW_NORMAL;
		}
	}

	bool SetCurrentThreadCooperativeBackgroundPriority()
	{
		const DWORD processPriorityClass = GetPriorityClass(GetCurrentProcess());
		const int threadPriority = GetCooperativeBackgroundThreadPriority(processPriorityClass);
		const bool priorityApplied =
			SetThreadPriority(GetCurrentThread(), threadPriority) != FALSE;
		// THREAD_PRIORITY_IDLE is still base priority 16 in REALTIME_PRIORITY_CLASS;
		// Windows offers no relative thread priority that can make such a process
		// cooperative with an ordinary desktop application. Apply the least
		// aggressive available value, but report that the guarantee is unavailable.
		return processPriorityClass != 0 &&
		       processPriorityClass != REALTIME_PRIORITY_CLASS &&
		       priorityApplied;
	}

	std::optional<std::filesystem::path> GetPicturesPath()
	{
		return GetKnownFolderPath(FOLDERID_Pictures);
	}

	std::optional<std::filesystem::path> GetVideosPath()
	{
		return GetKnownFolderPath(FOLDERID_Videos);
	}
}  // namespace Util
