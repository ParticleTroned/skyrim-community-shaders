#include "OpenVRDetection.h"
#include <cctype>
#include <format>
#include <initializer_list>
#include <openvr.h>
#include <string_view>
#include <vector>
#include <windows.h>
#include <winver.h>
#pragma comment(lib, "version.lib")

namespace VRDetection
{
	namespace
	{
		bool TryReadFileBytes(const std::string& path, std::string& bytes)
		{
			HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 64LL * 1024LL * 1024LL) {
				CloseHandle(file);
				return false;
			}

			bytes.assign(static_cast<size_t>(size.QuadPart), '\0');
			DWORD bytesRead = 0;
			const bool readOk = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, nullptr);
			CloseHandle(file);
			if (!readOk) {
				return false;
			}
			bytes.resize(bytesRead);
			return true;
		}

		bool BytesContainAnyString(const std::string& bytes, std::initializer_list<std::string_view> needles)
		{
			for (auto needle : needles) {
				if (bytes.find(needle) != std::string::npos) {
					return true;
				}
			}
			return false;
		}
	}

	const char* RuntimeTypeToString(RuntimeType type)
	{
		switch (type) {
		case RuntimeType::SteamVR:
			return "SteamVR";
		case RuntimeType::OpenComposite:
			return "OpenComposite";
		default:
			return "Unknown";
		}
	}

	bool ProbeRuntimeInterfaces(OpenVRDetectionResult& result)
	{
		HMODULE hModule = GetModuleHandleA("openvr_api.dll");
		if (!hModule)
			return false;

		using pfnIsValid = bool(__cdecl*)(const char*);
		auto IsValid = reinterpret_cast<pfnIsValid>(GetProcAddress(hModule, "VR_IsInterfaceVersionValid"));
		if (!IsValid)
			return false;

		result.hasOverlayInterface = IsValid(vr::IVROverlay_Version);
		result.hasSystemInterface = IsValid(vr::IVRSystem_Version);
		result.hasCompositorInterface = IsValid(vr::IVRCompositor_Version);

		result.probingSucceeded = result.hasSystemInterface && result.hasCompositorInterface;
		return result.probingSucceeded;
	}

	void GatherDLLInfo(OpenVRDetectionResult& result)
	{
		HMODULE hModule = GetModuleHandleA("openvr_api.dll");
		if (!hModule) {
			result.isAvailable = false;
			return;
		}

		result.isAvailable = true;

		char dllPath[MAX_PATH];
		DWORD fileLength = GetModuleFileNameA(hModule, dllPath, MAX_PATH);
		if (fileLength == 0 || (fileLength == MAX_PATH && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
			result.isAvailable = false;
			return;
		}

		result.dllPath = dllPath;

		DWORD dwSize = GetFileVersionInfoSizeA(dllPath, nullptr);
		if (dwSize > 0) {
			std::vector<BYTE> buffer(dwSize);
			if (GetFileVersionInfoA(dllPath, 0, dwSize, buffer.data())) {
				VS_FIXEDFILEINFO* pFileInfo = nullptr;
				UINT len = 0;
				if (VerQueryValueA(buffer.data(), "\\", reinterpret_cast<LPVOID*>(&pFileInfo), &len)) {
					DWORD major = HIWORD(pFileInfo->dwFileVersionMS);
					DWORD minor = LOWORD(pFileInfo->dwFileVersionMS);
					DWORD build = HIWORD(pFileInfo->dwFileVersionLS);
					DWORD revision = LOWORD(pFileInfo->dwFileVersionLS);
					result.version = std::format("{}.{}.{}.{}", major, minor, build, revision);
				}
			}
		}

		if (result.version.empty())
			result.version = "Unknown";

		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA(dllPath, &findData);
		if (hFind != INVALID_HANDLE_VALUE) {
			FindClose(hFind);
			ULARGE_INTEGER fileSize;
			fileSize.LowPart = findData.nFileSizeLow;
			fileSize.HighPart = findData.nFileSizeHigh;
			result.fileSize = fileSize.QuadPart;

			SYSTEMTIME st;
			FileTimeToSystemTime(&findData.ftLastWriteTime, &st);
			result.modificationTime = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}
	}

	RuntimeType DetectRuntimeType(const std::string& dllPath, const std::string& version, uint64_t fileSize)
	{
		std::string lowerPath = dllPath;
		for (auto& c : lowerPath)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		if (lowerPath.find("opencomposite") != std::string::npos)
			return RuntimeType::OpenComposite;

		std::string dllBytes;
		const bool hasDllBytes = TryReadFileBytes(dllPath, dllBytes);

		const bool hasOpenCompositeMarker = hasDllBytes && BytesContainAnyString(dllBytes, { "OpenComposite",
																							   "opencomposite",
																							   "OpenOVR",
																							   "openovr",
																							   "OCUnleashed",
																							   "ocunleashed",
																							   "OCUnleashedSKSE.log" });
		if (hasOpenCompositeMarker)
			return RuntimeType::OpenComposite;

		const bool hasSteamVRLoaderMarker = hasDllBytes && BytesContainAnyString(dllBytes, { "vrclient_x64.dll" });
		if (hasSteamVRLoaderMarker)
			return RuntimeType::SteamVR;

		if (lowerPath.find("steamvr") != std::string::npos)
			return RuntimeType::SteamVR;

		// Unmarked OpenComposite DLLs commonly mimic the old OpenVR 1.0.10 loader.
		if (version == "1.0.10.0" && fileSize < 700000)
			return RuntimeType::OpenComposite;

		// Higher version numbers suggest SteamVR
		if (!version.empty() && version != "Unknown" && version != "1.0.10.0")
			return RuntimeType::SteamVR;

		return RuntimeType::Unknown;
	}

	OpenVRDetectionResult Detect()
	{
		OpenVRDetectionResult result;

		GatherDLLInfo(result);
		if (!result.isAvailable)
			return result;

		result.runtimeType = DetectRuntimeType(result.dllPath, result.version, result.fileSize);

		// Detect compatibility via runtime interface probing
		result.isCompatible = ProbeRuntimeInterfaces(result);

		return result;
	}
}
