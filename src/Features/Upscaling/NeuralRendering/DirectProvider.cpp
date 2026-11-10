#include "DirectProvider.h"
#include "Utils/PathValidation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <softpub.h>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>
#include <wintrust.h>

namespace NeuralRendering
{
	namespace
	{
		constexpr std::array kParameterCoreBasenames{ L"_nvngx.dll", L"nvngx.dll" };

		template <class T>
		const T* ReadImageArray(HMODULE module, std::size_t imageSize, std::size_t offset, std::size_t count = 1)
		{
			if (!module || !count || offset > imageSize || count > (imageSize - offset) / sizeof(T))
				return nullptr;
			const auto base = reinterpret_cast<std::uintptr_t>(module);
			const auto bytes = count * sizeof(T);
			if (offset > std::numeric_limits<std::uintptr_t>::max() - base)
				return nullptr;
			const auto begin = base + offset;
			if (begin % alignof(T) || bytes > std::numeric_limits<std::uintptr_t>::max() - begin)
				return nullptr;
			const auto end = begin + bytes;
			for (auto cursor = begin; cursor < end;) {
				MEMORY_BASIC_INFORMATION memory{};
				constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
				                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
				if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) != sizeof(memory) ||
					memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.AllocationBase != module ||
					!(memory.Protect & readable) || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
					return nullptr;
				const auto region = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
				if (region > cursor || memory.RegionSize <= cursor - region || memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - region)
					return nullptr;
				cursor = std::min(end, region + memory.RegionSize);
			}
			return reinterpret_cast<const T*>(begin);
		}

		const IMAGE_NT_HEADERS64* ReadImageHeaders(HMODULE module)
		{
			constexpr auto unbounded = std::numeric_limits<std::size_t>::max();
			const auto* dos = ReadImageArray<IMAGE_DOS_HEADER>(module, unbounded, 0);
			if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
				return nullptr;
			const auto* nt = ReadImageArray<IMAGE_NT_HEADERS64>(module, unbounded, static_cast<std::size_t>(dos->e_lfanew));
			if (!nt || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
				!(nt->FileHeader.Characteristics & IMAGE_FILE_DLL) || nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
				nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT ||
				nt->OptionalHeader.SizeOfImage < static_cast<std::size_t>(dos->e_lfanew) + sizeof(*nt))
				return nullptr;
			return nt;
		}

		bool IsExecutableAddress(const void* address, HMODULE owner = nullptr)
		{
			MEMORY_BASIC_INFORMATION memory{};
			constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
			return address && VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory) &&
			       memory.State == MEM_COMMIT && (memory.Protect & executable) && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
			       (!owner || (memory.Type == MEM_IMAGE && memory.AllocationBase == owner));
		}

		template <class Function>
		bool BindExecutableExport(HMODULE module, const char* name, Function& function, std::string& detail)
		{
			function = nullptr;
			const auto address = GetProcAddress(module, name);
			if (!address) {
				detail = std::format("NR module is missing required export {}", name);
				return false;
			}
			if (!IsExecutableAddress(reinterpret_cast<const void*>(address), module)) {
				detail = std::format("NR export {} must resolve to executable code in its owning module", name);
				return false;
			}
			function = reinterpret_cast<Function>(address);
			return true;
		}

		struct FileIdentity
		{
			DWORD volumeSerialNumber = 0;
			DWORD fileIndexHigh = 0;
			DWORD fileIndexLow = 0;
			DWORD fileSizeHigh = 0;
			DWORD fileSizeLow = 0;

			[[nodiscard]] bool operator==(const FileIdentity&) const = default;
		};

		bool QueryFileIdentity(HANDLE a_file, FileIdentity& a_identity, std::string& a_error)
		{
			BY_HANDLE_FILE_INFORMATION information{};
			if (!GetFileInformationByHandle(a_file, &information)) {
				a_error = std::format(
					"GetFileInformationByHandle failed with {}", GetLastError());
				return false;
			}
			a_identity = {
				.volumeSerialNumber = information.dwVolumeSerialNumber,
				.fileIndexHigh = information.nFileIndexHigh,
				.fileIndexLow = information.nFileIndexLow,
				.fileSizeHigh = information.nFileSizeHigh,
				.fileSizeLow = information.nFileSizeLow,
			};
			return true;
		}

		bool VerifyLoadedModuleIdentity(
			HMODULE a_module, HANDLE a_verifiedFile, std::string& a_error)
		{
			FileIdentity verifiedIdentity;
			if (!QueryFileIdentity(a_verifiedFile, verifiedIdentity, a_error))
				return false;

			std::array<wchar_t, 32768> loadedPath{};
			const DWORD pathLength = GetModuleFileNameW(
				a_module, loadedPath.data(), static_cast<DWORD>(loadedPath.size()));
			if (!pathLength || pathLength >= loadedPath.size()) {
				a_error = std::format("GetModuleFileNameW failed with {}", GetLastError());
				return false;
			}

			const HANDLE loadedFile = CreateFileW(
				loadedPath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (loadedFile == INVALID_HANDLE_VALUE) {
				a_error = std::format(
					"could not open the loaded module for identity verification: {}",
					GetLastError());
				return false;
			}

			FileIdentity loadedIdentity;
			const bool queried = QueryFileIdentity(loadedFile, loadedIdentity, a_error);
			CloseHandle(loadedFile);
			if (!queried)
				return false;
			if (loadedIdentity != verifiedIdentity) {
				a_error = "loaded module identity differs from the file that was hashed";
				return false;
			}
			return true;
		}

		struct VersionTranslation
		{
			WORD language = 0;
			WORD codePage = 0;
		};

		bool QueryVersionString(
			const std::vector<std::byte>& a_versionData,
			const VersionTranslation& a_translation,
			std::wstring_view a_name,
			std::wstring& a_value)
		{
			std::wostringstream query;
			query << L"\\StringFileInfo\\" << std::uppercase << std::hex << std::setfill(L'0') << std::setw(4) << a_translation.language << std::setw(4) << a_translation.codePage << L'\\' << a_name;

			void* rawValue = nullptr;
			UINT valueCharacters = 0;
			if (!VerQueryValueW(
					a_versionData.data(), query.str().c_str(), &rawValue,
					&valueCharacters) ||
				!rawValue || !valueCharacters) {
				return false;
			}

			a_value.assign(static_cast<const wchar_t*>(rawValue), valueCharacters);
			while (!a_value.empty() && a_value.back() == L'\0')
				a_value.pop_back();
			return !a_value.empty();
		}

		bool VerifyNvidiaParameterCoreVersionResource(
			const std::filesystem::path& a_path, std::string& a_error)
		{
			DWORD ignored = 0;
			const DWORD versionBytes = GetFileVersionInfoSizeW(a_path.c_str(), &ignored);
			constexpr DWORD kMaximumVersionResourceBytes = 4u * 1024u * 1024u;
			if (!versionBytes) {
				a_error = std::format(
					"NGX parameter core has no readable version resource: {}",
					GetLastError());
				return false;
			}
			if (versionBytes > kMaximumVersionResourceBytes) {
				a_error = std::format(
					"NGX parameter core version resource is too large: {} bytes",
					versionBytes);
				return false;
			}

			std::vector<std::byte> versionData(versionBytes);
			if (!GetFileVersionInfoW(
					a_path.c_str(), 0, versionBytes, versionData.data())) {
				a_error = std::format(
					"GetFileVersionInfoW failed with {}", GetLastError());
				return false;
			}

			void* rawTranslations = nullptr;
			UINT translationBytes = 0;
			if (!VerQueryValueW(
					versionData.data(), L"\\VarFileInfo\\Translation", &rawTranslations,
					&translationBytes) ||
				!rawTranslations || translationBytes < sizeof(VersionTranslation) ||
				translationBytes % sizeof(VersionTranslation) != 0) {
				a_error = "NGX parameter core version-resource translations are missing or malformed";
				return false;
			}

			const auto* translations = static_cast<const VersionTranslation*>(rawTranslations);
			const std::size_t translationCount = translationBytes / sizeof(VersionTranslation);
			for (std::size_t index = 0; index < translationCount; ++index) {
				std::wstring companyName;
				std::wstring productName;
				std::wstring originalFilename;
				if (QueryVersionString(
						versionData, translations[index], L"CompanyName", companyName) &&
					QueryVersionString(
						versionData, translations[index], L"ProductName", productName) &&
					QueryVersionString(
						versionData, translations[index], L"OriginalFilename", originalFilename) &&
					_wcsicmp(companyName.c_str(), L"NVIDIA Corporation") == 0 &&
					_wcsicmp(productName.c_str(), L"NGX") == 0 &&
					_wcsicmp(originalFilename.c_str(), L"nvngx.dll") == 0) {
					return true;
				}
			}

			a_error =
				"NGX parameter core version identity must be CompanyName=NVIDIA Corporation, ProductName=NGX, OriginalFilename=nvngx.dll";
			return false;
		}

		bool VerifyAuthenticodeOffline(
			const std::filesystem::path& a_path, HANDLE a_file, std::string& a_error)
		{
			WINTRUST_FILE_INFO fileInfo{};
			fileInfo.cbStruct = sizeof(fileInfo);
			fileInfo.pcwszFilePath = a_path.c_str();
			fileInfo.hFile = a_file;

			WINTRUST_DATA trustData{};
			trustData.cbStruct = sizeof(trustData);
			trustData.dwUIChoice = WTD_UI_NONE;
			trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
			trustData.dwUnionChoice = WTD_CHOICE_FILE;
			trustData.pFile = &fileInfo;
			trustData.dwStateAction = WTD_STATEACTION_VERIFY;
			trustData.dwProvFlags =
				WTD_REVOCATION_CHECK_NONE | WTD_CACHE_ONLY_URL_RETRIEVAL;
			trustData.dwUIContext = WTD_UICONTEXT_EXECUTE;

			GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
			const LONG trustResult = WinVerifyTrust(nullptr, &action, &trustData);
			trustData.dwStateAction = WTD_STATEACTION_CLOSE;
			(void)WinVerifyTrust(nullptr, &action, &trustData);
			if (trustResult != ERROR_SUCCESS) {
				a_error = std::format(
					"offline Authenticode verification failed 0x{:08X}",
					static_cast<std::uint32_t>(trustResult));
				return false;
			}
			return true;
		}

		using GetModuleFileNameWFunction = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

		struct PathProxyInvocation
		{
			HMODULE runtimeModule = nullptr;
			HMODULE callerModule = nullptr;
			HMODULE retainedCallerModule = nullptr;
			std::wstring spoofedPath;
		};

		std::recursive_mutex g_pathProxyMutex;
		std::atomic<std::shared_ptr<PathProxyInvocation>> g_activePathProxy;
		std::atomic<GetModuleFileNameWFunction> g_originalGetModuleFileNameW = nullptr;
		HMODULE g_pathProxyRuntime = nullptr;
		void** g_pathProxySlot = nullptr;
		bool g_pathProxyHookInstalled = false;

		DWORD WINAPI RuntimeCallerGetModuleFileNameW(HMODULE a_module, LPWSTR a_filename, DWORD a_size)
		{
			const auto invocation = g_activePathProxy.load(std::memory_order_acquire);
			if (invocation && a_module == invocation->callerModule && a_filename && a_size &&
				!invocation->spoofedPath.empty()) {
				const DWORD length = static_cast<DWORD>(invocation->spoofedPath.size());
				const DWORD copyLength = std::min(length, a_size - 1);
				std::memcpy(a_filename, invocation->spoofedPath.data(), copyLength * sizeof(wchar_t));
				a_filename[copyLength] = L'\0';
				return copyLength < length ? a_size : length;
			}

			const auto original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
			return original ? original(a_module, a_filename, a_size) : 0;
		}

		struct ImportSlotExchangeResult
		{
			bool exchanged = false;
			bool protectionRestored = false;
			bool rolledBack = false;
		};

		ImportSlotExchangeResult ProtectAndExchangeImportSlot(
			void** a_slot, void* a_expected, void* a_value)
		{
			if (!a_slot || !a_expected || !a_value)
				return {};
			DWORD oldProtection = 0;
			if (!VirtualProtect(a_slot, sizeof(*a_slot), PAGE_READWRITE, &oldProtection))
				return {};
			const auto previous = InterlockedCompareExchangePointer(
				reinterpret_cast<void* volatile*>(a_slot), a_value, a_expected);
			DWORD ignoredProtection = 0;
			ImportSlotExchangeResult result{
				.exchanged = previous == a_expected,
				.protectionRestored = VirtualProtect(
										  a_slot, sizeof(*a_slot), oldProtection, &ignoredProtection) != FALSE,
			};
			if (result.exchanged && !result.protectionRestored) {
				const auto rollbackPrevious = InterlockedCompareExchangePointer(
					reinterpret_cast<void* volatile*>(a_slot), a_expected, a_value);
				result.rolledBack = rollbackPrevious == a_value;
				DWORD ignoredRollbackProtection = 0;
				result.protectionRestored = VirtualProtect(
												a_slot, sizeof(*a_slot), oldProtection,
												&ignoredRollbackProtection) != FALSE;
			}
			if (result.exchanged && result.protectionRestored && !result.rolledBack)
				FlushInstructionCache(GetCurrentProcess(), a_slot, sizeof(*a_slot));
			return result;
		}

		void* ReadImportSlot(void** a_slot) noexcept
		{
			// Pointer-sized aligned loads are atomic on the supported x64 runtime;
			// unlike CMPXCHG this does not write to a read-only IAT page.
			return a_slot ? *reinterpret_cast<void* volatile*>(a_slot) : nullptr;
		}

		void** FindGetModuleFileNameWImportSlot(HMODULE a_runtime)
		{
			const auto* ntHeaders = ReadImageHeaders(a_runtime);
			if (!ntHeaders)
				return nullptr;

			const std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;
			const auto& imports = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!imports.VirtualAddress || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
				!ReadImageArray<std::byte>(a_runtime, imageSize, imports.VirtualAddress, imports.Size)) {
				return nullptr;
			}

			const std::size_t descriptorCount = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
			const auto* descriptor = ReadImageArray<IMAGE_IMPORT_DESCRIPTOR>(a_runtime, imageSize, imports.VirtualAddress, descriptorCount);
			if (!descriptor)
				return nullptr;
			for (std::size_t descriptorIndex = 0; descriptorIndex < descriptorCount && descriptor[descriptorIndex].Name; ++descriptorIndex) {
				const auto& current = descriptor[descriptorIndex];
				if (!current.OriginalFirstThunk || current.OriginalFirstThunk >= imageSize ||
					!current.FirstThunk || current.FirstThunk >= imageSize) {
					continue;
				}

				const auto thunkCount = std::min(imageSize - current.OriginalFirstThunk, imageSize - current.FirstThunk) / sizeof(IMAGE_THUNK_DATA64);
				for (std::size_t thunkIndex = 0; thunkIndex < thunkCount; ++thunkIndex) {
					const auto* name = ReadImageArray<IMAGE_THUNK_DATA64>(a_runtime, imageSize, current.OriginalFirstThunk + thunkIndex * sizeof(IMAGE_THUNK_DATA64));
					const auto* function = ReadImageArray<IMAGE_THUNK_DATA64>(a_runtime, imageSize, current.FirstThunk + thunkIndex * sizeof(IMAGE_THUNK_DATA64));
					if (!name || !function)
						return nullptr;
					if (!name->u1.AddressOfData) {
						break;
					}
					if (IMAGE_SNAP_BY_ORDINAL64(name->u1.Ordinal))
						continue;
					const auto importOffset = static_cast<std::size_t>(name->u1.AddressOfData);
					if (importOffset > imageSize || sizeof(WORD) > imageSize - importOffset)
						return nullptr;
					constexpr char target[] = "GetModuleFileNameW";
					const auto* importName = ReadImageArray<char>(a_runtime, imageSize, importOffset + sizeof(WORD), sizeof(target));
					if (importName && std::memcmp(importName, target, sizeof(target)) == 0)
						return reinterpret_cast<void**>(const_cast<ULONGLONG*>(&function->u1.Function));
				}
			}
			return nullptr;
		}

		bool InstallRuntimeCallerPathHook(
			HMODULE a_runtime, const std::filesystem::path& a_spoofedPath)
		{
			if (!a_runtime)
				return false;

			std::scoped_lock lock(g_pathProxyMutex);
			const auto active = g_activePathProxy.load(std::memory_order_acquire);
			if (g_pathProxyHookInstalled) {
				return g_pathProxyRuntime == a_runtime && active &&
				       active->runtimeModule == a_runtime &&
				       active->retainedCallerModule && g_pathProxySlot &&
				       ReadImportSlot(g_pathProxySlot) ==
				           reinterpret_cast<void*>(&RuntimeCallerGetModuleFileNameW) &&
				       active->spoofedPath == a_spoofedPath.wstring();
			}

			void** slot = FindGetModuleFileNameWImportSlot(a_runtime);
			if (!slot)
				return false;
			const auto original = reinterpret_cast<GetModuleFileNameWFunction>(
				ReadImportSlot(slot));
			if (!IsExecutableAddress(reinterpret_cast<const void*>(original)) || original == &RuntimeCallerGetModuleFileNameW)
				return false;

			auto invocation = std::make_shared<PathProxyInvocation>();
			invocation->runtimeModule = a_runtime;
			invocation->spoofedPath = a_spoofedPath.wstring();
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_PIN,
					reinterpret_cast<LPCWSTR>(&RuntimeCallerGetModuleFileNameW),
					&invocation->retainedCallerModule)) {
				return false;
			}
			invocation->callerModule = invocation->retainedCallerModule;

			g_originalGetModuleFileNameW.store(original, std::memory_order_release);
			g_activePathProxy.store(invocation, std::memory_order_release);
			const auto exchange = ProtectAndExchangeImportSlot(
				slot, reinterpret_cast<void*>(original),
				reinterpret_cast<void*>(&RuntimeCallerGetModuleFileNameW));
			if (!exchange.exchanged || !exchange.protectionRestored ||
				exchange.rolledBack) {
				if (ReadImportSlot(slot) ==
					reinterpret_cast<void*>(&RuntimeCallerGetModuleFileNameW)) {
					g_pathProxyRuntime = a_runtime;
					g_pathProxySlot = slot;
					g_pathProxyHookInstalled = true;
					return false;
				}
				g_activePathProxy.store({}, std::memory_order_release);
				return false;
			}

			g_pathProxyRuntime = a_runtime;
			g_pathProxySlot = slot;
			g_pathProxyHookInstalled = true;
			return true;
		}

		bool UninstallRuntimeCallerPathHook(HMODULE a_runtime)
		{
			std::scoped_lock lock(g_pathProxyMutex);
			if (!g_pathProxyHookInstalled)
				return true;
			if (!a_runtime || g_pathProxyRuntime != a_runtime)
				return false;

			const auto original =
				g_originalGetModuleFileNameW.load(std::memory_order_acquire);
			const auto active = g_activePathProxy.load(std::memory_order_acquire);
			if (!original || !g_pathProxySlot || !active ||
				!active->retainedCallerModule) {
				return false;
			}
			const auto exchange = ProtectAndExchangeImportSlot(
				g_pathProxySlot,
				reinterpret_cast<void*>(&RuntimeCallerGetModuleFileNameW),
				reinterpret_cast<void*>(original));
			if (!exchange.exchanged ||
				!exchange.protectionRestored || exchange.rolledBack) {
				return false;
			}

			g_pathProxySlot = nullptr;
			g_pathProxyRuntime = nullptr;
			g_pathProxyHookInstalled = false;
			g_activePathProxy.store({}, std::memory_order_release);
			// The proxy module was pinned before exchange and remains resident until
			// process exit, covering callers that already fetched the proxy address.
			return true;
		}

	}

	DirectProvider::~DirectProvider()
	{
		// Failed retirement retains both module code and the import target until process exit.
		if (!initialized_)
			Unload();
	}

	NVSDK_NGX_Result DirectProvider::Initialize(const std::filesystem::path& path, HANDLE verifiedFile,
		ID3D12Device* device, const std::filesystem::path& dataPath, std::string& detail)
	{
		if (module_ || !device || !verifiedFile || verifiedFile == INVALID_HANDLE_VALUE) {
			detail = "direct NR provider initialization prerequisites are invalid";
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		}
		initializationAttempted_ = false;
		for (const auto* name : kParameterCoreBasenames) {
			const auto adjacent = path.parent_path() / name;
			const auto attributes = GetFileAttributesW(adjacent.c_str());
			const auto error = GetLastError();
			if (attributes != INVALID_FILE_ATTRIBUTES || (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
				detail = "legacy NR requires parameter cores to come from the provider or the NVIDIA DriverStore; adjacent NGX cores are forbidden";
				return NVSDK_NGX_Result_FAIL_InvalidParameter;
			}
		}
		module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module_) {
			detail = std::format("direct NR provider load failed: Windows error {}", GetLastError());
			return NVSDK_NGX_Result_FAIL_FeatureNotFound;
		}
		if (!VerifyLoadedModuleIdentity(module_, verifiedFile, detail))
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		if (!ReadImageHeaders(module_)) {
			detail = "direct NR provider requires valid AMD64 PE32+ DLL headers";
			return NVSDK_NGX_Result_FAIL_NotImplemented;
		}

		using GetUnsigned = unsigned(NVSDK_CONV*)();
		using Init = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
		GetUnsigned api = nullptr, application = nullptr;
		Init init = nullptr;
		ScratchSize providerScratch = nullptr;
		Create providerCreate = nullptr;
		Evaluate providerEvaluate = nullptr;
		Release providerRelease = nullptr;
		if (!BindExecutableExport(module_, "NVSDK_NGX_GetAPIVersion", api, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_GetApplicationId", application, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_Init_Ext", init, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_PopulateParameters_Impl", populate_, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_GetScratchBufferSize", providerScratch, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_CreateFeature", providerCreate, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_EvaluateFeature", providerEvaluate, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_ReleaseFeature", providerRelease, detail) ||
			!BindExecutableExport(module_, "NVSDK_NGX_D3D12_Shutdown1", shutdown_, detail)) {
			return NVSDK_NGX_Result_FAIL_NotImplemented;
		}
		if (!BindParameterCore(detail))
			return NVSDK_NGX_Result_FAIL_FeatureNotFound;
		// The provider's local caller-name guard expects the NGX entry point basename.
		if (!InstallRuntimeCallerPathHook(module_, path.parent_path() / L"nvngx.dll")) {
			detail = "direct NR provider caller compatibility import could not be installed";
			return NVSDK_NGX_Result_FAIL_PlatformError;
		}
		// The snippet's exported metadata defines its initializer ABI.
		initializationAttempted_ = true;
		const auto result = init(application(), dataPath.c_str(), device, static_cast<NVSDK_NGX_Version>(api()), nullptr);
		if (NVSDK_NGX_FAILED(result)) {
			detail = std::format("direct NR provider Init_Ext failed 0x{:08X}", static_cast<unsigned>(result));
			return result;
		}
		initialized_ = true;
		scratchSize = providerScratch;
		create = providerCreate;
		evaluate = providerEvaluate;
		release = providerRelease;
		detail = "legacy NR provider initialized; parameter maps retain their allocating core";
		return result;
	}

	bool DirectProvider::BindParameterCore(std::string& detail)
	{
		struct ModuleDeleter
		{
			void operator()(HMODULE module) const { FreeLibrary(module); }
		};
		struct FileDeleter
		{
			void operator()(HANDLE file) const { CloseHandle(file); }
		};
		using Module = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleDeleter>;
		using File = std::unique_ptr<void, FileDeleter>;
		Module selected;
		File selectedFile;
		Allocate selectedAllocate = nullptr;
		Destroy selectedDestroy = nullptr;
		for (std::size_t i = 0; i <= kParameterCoreBasenames.size(); ++i) {
			HMODULE candidate = nullptr;
			const DWORD flags = i == 0 ? GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS : 0;
			const auto identifier = i == 0 ? reinterpret_cast<LPCWSTR>(module_) : kParameterCoreBasenames[i - 1];
			// Discovery must retain the candidate before inspecting any of its exports.
			if (!GetModuleHandleExW(flags, identifier, &candidate))
				continue;
			Module retained(candidate);
			if (candidate == selected.get())
				continue;
			Allocate allocate = nullptr;
			Destroy destroy = nullptr;
			if (!BindExecutableExport(candidate, "NVSDK_NGX_D3D12_AllocateParameters", allocate, detail) ||
				!BindExecutableExport(candidate, "NVSDK_NGX_D3D12_DestroyParameters", destroy, detail))
				continue;
			File file;
			if (candidate != module_) {
				std::array<wchar_t, 32768> image{}, system{};
				const auto imageLength = GetModuleFileNameW(candidate, image.data(), static_cast<DWORD>(image.size()));
				const auto systemLength = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
				if (!imageLength || imageLength >= image.size() || !systemLength || systemLength >= system.size())
					continue;
				const std::filesystem::path path(std::wstring(image.data(), imageLength));
				const auto driverStore = std::filesystem::path(std::wstring(system.data(), systemLength)) / L"DriverStore" / L"FileRepository";
				const auto name = path.filename().wstring();
				if (!Util::IsPathWithinDirectory(driverStore, path) || std::ranges::none_of(kParameterCoreBasenames,
																		   [&](const auto* basename) { return _wcsicmp(name.c_str(), basename) == 0; })) {
					detail = "loaded parameter core is outside the NVIDIA DriverStore location";
					continue;
				}
				const auto opened = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (opened == INVALID_HANDLE_VALUE) {
					detail = std::format("could not lock the driver parameter core: Windows error {}", GetLastError());
					continue;
				}
				file.reset(opened);
				if (!VerifyLoadedModuleIdentity(candidate, opened, detail) ||
					!VerifyNvidiaParameterCoreVersionResource(path, detail) || !VerifyAuthenticodeOffline(path, opened, detail))
					continue;
			}
			if (selected) {
				detail = "multiple verified NVIDIA parameter cores are loaded";
				return false;
			}
			selected = std::move(retained);
			selectedFile = std::move(file);
			selectedAllocate = allocate;
			selectedDestroy = destroy;
			if (candidate == module_)
				break;
		}
		if (!selected) {
			detail = "legacy parameter allocator unavailable" + (detail.empty() ? std::string{} : ": " + detail);
			return false;
		}
		parameterCore_ = selected.release();
		parameterCoreFile_ = selectedFile ? selectedFile.release() : INVALID_HANDLE_VALUE;
		allocate_ = selectedAllocate;
		destroy_ = selectedDestroy;
		return true;
	}

	NVSDK_NGX_Result DirectProvider::AllocateParameters(NVSDK_NGX_Parameter** parameters, std::string& detail)
	{
		detail.clear();
		if (!initialized_ || !parameters)
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		*parameters = nullptr;
		const auto slot = std::find(parameters_.begin(), parameters_.end(), nullptr);
		if (slot == parameters_.end())
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		if (!allocate_)
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		const auto result = allocate_(parameters);
		if (*parameters && std::find(parameters_.begin(), parameters_.end(), *parameters) != parameters_.end()) {
			// An existing map has one owner even when the allocator repeats its address.
			*parameters = nullptr;
			detail = "parameter core returned an already owned map";
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		}
		if (*parameters)
			*slot = *parameters;
		if (NVSDK_NGX_FAILED(result))
			detail = std::format("parameter core allocation failed 0x{:08X}", static_cast<unsigned>(result));
		else if (!*parameters)
			detail = "parameter core returned a null map";
		return result;
	}

	NVSDK_NGX_Result DirectProvider::DestroyParameters(NVSDK_NGX_Parameter* parameters)
	{
		const auto slot = std::find(parameters_.begin(), parameters_.end(), parameters);
		if (!parameters || !destroy_ || slot == parameters_.end())
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		const auto result = destroy_(parameters);
		if (!NVSDK_NGX_FAILED(result))
			*slot = nullptr;
		return result;
	}

	NVSDK_NGX_Result DirectProvider::PopulateParameters(NVSDK_NGX_Parameter* parameters)
	{
		return initialized_ && parameters && populate_ && std::find(parameters_.begin(), parameters_.end(), parameters) != parameters_.end() ?
		           populate_(parameters) :
		           NVSDK_NGX_Result_FAIL_InvalidParameter;
	}

	NVSDK_NGX_Result DirectProvider::ShutdownDevice(ID3D12Device* device)
	{
		if (std::ranges::any_of(parameters_, [](auto* parameter) { return parameter != nullptr; }))
			return NVSDK_NGX_Result_FAIL_InvalidParameter;
		if (!initialized_)
			return NVSDK_NGX_Result_Success;
		const auto result = shutdown_(device);
		if (!NVSDK_NGX_FAILED(result))
			initialized_ = false;
		return result;
	}

	bool DirectProvider::Unload()
	{
		if (initialized_ || std::ranges::any_of(parameters_, [](auto* parameter) { return parameter != nullptr; }))
			return false;
		if (!module_)
			return true;
		if (!UninstallRuntimeCallerPathHook(module_))
			return false;
		if (parameterCoreFile_ != INVALID_HANDLE_VALUE) {
			if (!CloseHandle(parameterCoreFile_))
				return false;
			parameterCoreFile_ = INVALID_HANDLE_VALUE;
		}
		if (parameterCore_) {
			if (!FreeLibrary(parameterCore_))
				return false;
			parameterCore_ = nullptr;
			allocate_ = nullptr;
			destroy_ = nullptr;
		}
		if (!FreeLibrary(module_))
			return false;
		module_ = nullptr;
		populate_ = nullptr;
		scratchSize = nullptr;
		create = nullptr;
		evaluate = nullptr;
		release = nullptr;
		return true;
	}
}
