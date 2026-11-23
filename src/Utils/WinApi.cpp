#include "WinApi.h"
#include "StringUtils.h"

#include "ShaderCompilationSchedulingPolicy.h"

#include <ShlObj.h>
#include <format>
#include <shellapi.h>
#include <stdexcept>
#include <wrl/client.h>

namespace Util
{
	std::filesystem::path ChooseExportFolder(const std::filesystem::path& initial)
	{
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(initialized))
			throw std::runtime_error("The export folder picker could not initialize.");
		struct Apartment
		{
			~Apartment() { CoUninitialize(); }
		} apartment;
		Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
		auto check = [](HRESULT result) {
			if (FAILED(result))
				throw std::runtime_error(std::format("The export folder picker failed (0x{:08X}).", static_cast<unsigned long>(result)));
		};
		check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
		check(dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR));
		check(dialog->SetTitle(L"Export CSX performance results"));
		Microsoft::WRL::ComPtr<IShellItem> start;
		check(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&start)));
		check(dialog->SetFolder(start.Get()));
		const HRESULT shown = dialog->Show(nullptr);
		if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
			return {};
		check(shown);
		Microsoft::WRL::ComPtr<IShellItem> selected;
		check(dialog->GetResult(&selected));
		PWSTR path = nullptr;
		check(selected->GetDisplayName(SIGDN_FILESYSPATH, &path));
		const std::filesystem::path result(path);
		CoTaskMemFree(path);
		return result;
	}

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

	bool OpenInShell(const std::filesystem::path& path, std::string& error)
	{
		if (path.empty()) {
			error = "No file or directory was specified.";
			return false;
		}
		const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
		if (result <= 32) {
			error = std::format("Windows could not open {} (shell error {}).", PathToUtf8(path), result);
			return false;
		}
		error.clear();
		return true;
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
	std::optional<std::filesystem::path> GetDocumentsPath()
	{
		return GetKnownFolderPath(FOLDERID_Documents);
	}
}  // namespace Util
