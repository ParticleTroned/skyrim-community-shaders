#include "Runtime.h"

#include "Util.h"

#include <Psapi.h>
#include <Softpub.h>
#include <Windows.h>
#include <Wintrust.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <d3d12.h>
#include <format>
#include <iomanip>
#include <memory>
#include <nvsdk_ngx.h>
#include <sstream>
#include <utility>
#include <vector>

namespace NeuralRendering
{
	namespace
	{
		constexpr wchar_t kRuntimeName[] = L"nvngx_dlssnr.dll";
		constexpr std::array<std::wstring_view, 2> kParameterCoreBasenames{
			L"nvngx.dll",
			L"_nvngx.dll",
		};
		constexpr auto kFeatureDlssNr = static_cast<NVSDK_NGX_Feature>(18);
		constexpr std::array<const char*, 5> kRequiredExports{
			"NVSDK_NGX_D3D12_Init_Ext",
			"NVSDK_NGX_D3D12_CreateFeature",
			"NVSDK_NGX_D3D12_EvaluateFeature",
			"NVSDK_NGX_D3D12_ReleaseFeature",
			"NVSDK_NGX_D3D12_Shutdown1",
		};

		class ScopedHandle
		{
		public:
			explicit ScopedHandle(HANDLE a_handle = INVALID_HANDLE_VALUE) : handle_(a_handle) {}
			~ScopedHandle()
			{
				if (handle_ && handle_ != INVALID_HANDLE_VALUE)
					CloseHandle(handle_);
			}

			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;

			[[nodiscard]] HANDLE Get() const { return handle_; }
			[[nodiscard]] HANDLE Release()
			{
				const HANDLE handle = handle_;
				handle_ = INVALID_HANDLE_VALUE;
				return handle;
			}
			void Reset(HANDLE a_handle)
			{
				if (handle_ && handle_ != INVALID_HANDLE_VALUE)
					CloseHandle(handle_);
				handle_ = a_handle;
			}

		private:
			HANDLE handle_ = INVALID_HANDLE_VALUE;
		};

		struct FileIdentity
		{
			DWORD volumeSerialNumber = 0;
			DWORD fileIndexHigh = 0;
			DWORD fileIndexLow = 0;
			DWORD fileSizeHigh = 0;
			DWORD fileSizeLow = 0;

			[[nodiscard]] bool operator==(const FileIdentity&) const = default;
		};

		using GetUnsignedValue = unsigned int(NVSDK_CONV*)();
		using InitD3D12 = NVSDK_NGX_Result(NVSDK_CONV*)(
			unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version,
			const NVSDK_NGX_Parameter*);
		using ShutdownD3D12 = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
		using AllocateParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
		using DestroyParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
		using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
			ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*,
			NVSDK_NGX_Handle**);
		using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
			ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
			const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
		using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
		using GetModuleFileNameWFunction = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

		struct PathProxyInvocation
		{
			HMODULE callerModule = nullptr;
			std::wstring spoofedPath;
			std::atomic<std::uint32_t> hits = 0;
		};

		std::recursive_mutex g_pathProxyMutex;
		std::shared_ptr<PathProxyInvocation> g_activePathProxy;
		std::atomic<GetModuleFileNameWFunction> g_originalGetModuleFileNameW = nullptr;
		HMODULE g_pathProxyRuntime = nullptr;
		void** g_pathProxySlot = nullptr;
		std::size_t g_pathProxyDepth = 0;
		bool g_pathProxyHookInstalled = false;

		DWORD WINAPI RuntimeCallerGetModuleFileNameW(HMODULE a_module, LPWSTR a_filename, DWORD a_size)
		{
			const auto invocation = std::atomic_load_explicit(
				&g_activePathProxy, std::memory_order_acquire);
			if (invocation && a_module == invocation->callerModule && a_filename && a_size &&
				!invocation->spoofedPath.empty()) {
				invocation->hits.fetch_add(1, std::memory_order_relaxed);
				const DWORD length = static_cast<DWORD>(invocation->spoofedPath.size());
				const DWORD copyLength = std::min(length, a_size - 1);
				std::memcpy(a_filename, invocation->spoofedPath.data(), copyLength * sizeof(wchar_t));
				a_filename[copyLength] = L'\0';
				return copyLength < length ? a_size : length;
			}

			const auto original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
			return original ? original(a_module, a_filename, a_size) : 0;
		}

		bool ProtectAndWriteImportSlot(void** a_slot, void* a_value)
		{
			if (!a_slot)
				return false;
			DWORD oldProtection = 0;
			if (!VirtualProtect(a_slot, sizeof(*a_slot), PAGE_READWRITE, &oldProtection))
				return false;
			*a_slot = a_value;
			DWORD ignoredProtection = 0;
			(void)VirtualProtect(
				a_slot, sizeof(*a_slot), oldProtection, &ignoredProtection);
			FlushInstructionCache(GetCurrentProcess(), a_slot, sizeof(*a_slot));
			return true;
		}

		void** FindGetModuleFileNameWImportSlot(HMODULE a_runtime)
		{
			if (!a_runtime)
				return nullptr;
			auto* base = reinterpret_cast<std::byte*>(a_runtime);
			const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
				return nullptr;
			const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
			if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
				return nullptr;

			const std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;
			const auto& imports = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!imports.VirtualAddress || imports.VirtualAddress >= imageSize ||
				imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
				return nullptr;
			}

			const std::size_t descriptorCount = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
			auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
			for (std::size_t descriptorIndex = 0; descriptorIndex < descriptorCount && descriptor[descriptorIndex].Name; ++descriptorIndex) {
				const auto& current = descriptor[descriptorIndex];
				if (!current.OriginalFirstThunk || current.OriginalFirstThunk >= imageSize ||
					!current.FirstThunk || current.FirstThunk >= imageSize) {
					continue;
				}

				auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + current.OriginalFirstThunk);
				auto* functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + current.FirstThunk);
				for (std::size_t thunkIndex = 0;; ++thunkIndex) {
					const auto nameOffset = current.OriginalFirstThunk + thunkIndex * sizeof(IMAGE_THUNK_DATA);
					const auto functionOffset = current.FirstThunk + thunkIndex * sizeof(IMAGE_THUNK_DATA);
					if (nameOffset + sizeof(IMAGE_THUNK_DATA) > imageSize ||
						functionOffset + sizeof(IMAGE_THUNK_DATA) > imageSize ||
						!names[thunkIndex].u1.AddressOfData) {
						break;
					}
					if (IMAGE_SNAP_BY_ORDINAL(names[thunkIndex].u1.Ordinal))
						continue;
					const auto importOffset = static_cast<std::size_t>(names[thunkIndex].u1.AddressOfData);
					if (importOffset + sizeof(IMAGE_IMPORT_BY_NAME) > imageSize)
						continue;
					const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + importOffset);
					if (std::strcmp(reinterpret_cast<const char*>(import->Name), "GetModuleFileNameW") == 0)
						return reinterpret_cast<void**>(&functions[thunkIndex].u1.Function);
				}
			}
			return nullptr;
		}

		class RuntimeCallerPathScope
		{
		public:
			RuntimeCallerPathScope(HMODULE a_runtime, const std::filesystem::path& a_spoofedPath) :
				lock_(g_pathProxyMutex)
			{
				if (!a_runtime)
					return;

				invocation_ = std::make_shared<PathProxyInvocation>();
				if (!GetModuleHandleExW(
						GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
							GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(&RuntimeCallerGetModuleFileNameW),
						&invocation_->callerModule)) {
					return;
				}
				invocation_->spoofedPath = a_spoofedPath.wstring();

				if (g_pathProxyHookInstalled) {
					if (g_pathProxyRuntime != a_runtime)
						return;
				} else {
					void** slot = FindGetModuleFileNameWImportSlot(a_runtime);
					if (!slot)
						return;
					const auto original = reinterpret_cast<GetModuleFileNameWFunction>(*slot);
					if (!original)
						return;
					g_originalGetModuleFileNameW.store(original, std::memory_order_release);
					if (!ProtectAndWriteImportSlot(
							slot, reinterpret_cast<void*>(&RuntimeCallerGetModuleFileNameW))) {
						g_originalGetModuleFileNameW.store(nullptr, std::memory_order_release);
						return;
					}
					g_pathProxyRuntime = a_runtime;
					g_pathProxySlot = slot;
					g_pathProxyHookInstalled = true;
				}

				previousInvocation_ = std::atomic_load_explicit(
					&g_activePathProxy, std::memory_order_acquire);
				std::atomic_store_explicit(
					&g_activePathProxy, invocation_, std::memory_order_release);
				++g_pathProxyDepth;
				installed_ = true;
			}

			~RuntimeCallerPathScope()
			{
				if (!installed_)
					return;
				std::atomic_store_explicit(
					&g_activePathProxy, previousInvocation_, std::memory_order_release);
				if (g_pathProxyDepth)
					--g_pathProxyDepth;
				if (!g_pathProxyDepth && g_pathProxyHookInstalled) {
					const auto original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
					if (ProtectAndWriteImportSlot(g_pathProxySlot, reinterpret_cast<void*>(original))) {
						g_pathProxySlot = nullptr;
						g_pathProxyRuntime = nullptr;
						g_pathProxyHookInstalled = false;
						g_originalGetModuleFileNameW.store(nullptr, std::memory_order_release);
					}
				}
			}

			RuntimeCallerPathScope(const RuntimeCallerPathScope&) = delete;
			RuntimeCallerPathScope& operator=(const RuntimeCallerPathScope&) = delete;

			[[nodiscard]] bool IsInstalled() const { return installed_; }
			[[nodiscard]] std::uint32_t Hits() const
			{
				return invocation_ ? invocation_->hits.load(std::memory_order_relaxed) : 0;
			}

		private:
			std::unique_lock<std::recursive_mutex> lock_;
			std::shared_ptr<PathProxyInvocation> invocation_;
			std::shared_ptr<PathProxyInvocation> previousInvocation_;
			bool installed_ = false;
		};

		bool IsRuntimeCallerPathHookActive(HMODULE a_runtime)
		{
			std::scoped_lock lock(g_pathProxyMutex);
			return g_pathProxyHookInstalled && g_pathProxyRuntime == a_runtime;
		}

		std::filesystem::path ResolveRuntimePath(const std::filesystem::path& a_explicitPath)
		{
			std::error_code error;
			const auto dataPath = Util::PathHelpers::GetDataPath();
			const auto expected =
				dataPath / L"Shaders/Upscaling/Streamline" / kRuntimeName;
			if (!a_explicitPath.empty()) {
				auto candidate = a_explicitPath;
				if (std::filesystem::is_directory(candidate, error))
					candidate /= kRuntimeName;
				error.clear();
				const auto candidateAbsolute =
					std::filesystem::absolute(candidate, error).lexically_normal().wstring();
				if (error)
					return {};
				const auto expectedAbsolute =
					std::filesystem::absolute(expected, error).lexically_normal().wstring();
				if (error || _wcsicmp(candidateAbsolute.c_str(), expectedAbsolute.c_str()) != 0)
					return {};
			}
			error.clear();
			if (std::filesystem::is_regular_file(expected, error))
				return expected;
			return {};
		}

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

		bool ComputeFileSha256(HANDLE a_file, std::string& a_hash, std::string& a_error)
		{
			LARGE_INTEGER beginning{};
			if (!SetFilePointerEx(a_file, beginning, nullptr, FILE_BEGIN)) {
				a_error = std::format("SetFilePointerEx failed with {}", GetLastError());
				return false;
			}

			BCRYPT_ALG_HANDLE algorithm = nullptr;
			NTSTATUS status = BCryptOpenAlgorithmProvider(
				&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
			if (status < 0) {
				a_error = std::format("BCryptOpenAlgorithmProvider failed 0x{:08X}", static_cast<std::uint32_t>(status));
				return false;
			}

			DWORD objectBytes = 0;
			DWORD copiedBytes = 0;
			status = BCryptGetProperty(
				algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
				sizeof(objectBytes), &copiedBytes, 0);
			if (status < 0) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
				a_error = std::format("BCryptGetProperty failed 0x{:08X}", static_cast<std::uint32_t>(status));
				return false;
			}

			std::vector<UCHAR> hashObject(objectBytes);
			BCRYPT_HASH_HANDLE hash = nullptr;
			status = BCryptCreateHash(
				algorithm, &hash, hashObject.data(),
				static_cast<ULONG>(hashObject.size()), nullptr, 0, 0);
			if (status < 0) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
				a_error = std::format("BCryptCreateHash failed 0x{:08X}", static_cast<std::uint32_t>(status));
				return false;
			}

			std::vector<UCHAR> buffer(1024 * 1024);
			while (status >= 0) {
				DWORD bytesRead = 0;
				if (!ReadFile(
						a_file, buffer.data(), static_cast<DWORD>(buffer.size()),
						&bytesRead, nullptr)) {
					a_error = std::format("ReadFile failed with {}", GetLastError());
					BCryptDestroyHash(hash);
					BCryptCloseAlgorithmProvider(algorithm, 0);
					return false;
				}
				if (!bytesRead)
					break;
				status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
			}

			std::array<UCHAR, 32> digest{};
			if (status >= 0) {
				status = BCryptFinishHash(
					hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
			}
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			if (status < 0) {
				a_error = std::format("SHA-256 hashing failed 0x{:08X}", static_cast<std::uint32_t>(status));
				return false;
			}

			std::ostringstream hashText;
			hashText << std::hex << std::uppercase << std::setfill('0');
			for (const auto byte : digest)
				hashText << std::setw(2) << static_cast<unsigned int>(byte);
			a_hash = hashText.str();
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

			ScopedHandle loadedFile(CreateFileW(
				loadedPath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr));
			if (loadedFile.Get() == INVALID_HANDLE_VALUE) {
				a_error = std::format(
					"could not open the loaded module for identity verification: {}",
					GetLastError());
				return false;
			}

			FileIdentity loadedIdentity;
			if (!QueryFileIdentity(loadedFile.Get(), loadedIdentity, a_error))
				return false;
			if (loadedIdentity != verifiedIdentity) {
				a_error = "loaded module identity differs from the file that was hashed";
				return false;
			}
			return true;
		}

		bool HasParameterApi(HMODULE a_module)
		{
			return a_module &&
			       GetProcAddress(a_module, "NVSDK_NGX_D3D12_AllocateParameters") &&
			       GetProcAddress(a_module, "NVSDK_NGX_D3D12_DestroyParameters");
		}

		class ScopedModuleReference
		{
		public:
			explicit ScopedModuleReference(HMODULE a_module = nullptr) : module_(a_module) {}
			~ScopedModuleReference()
			{
				if (module_)
					FreeLibrary(module_);
			}

			ScopedModuleReference(const ScopedModuleReference&) = delete;
			ScopedModuleReference& operator=(const ScopedModuleReference&) = delete;

			[[nodiscard]] HMODULE Get() const { return module_; }
			[[nodiscard]] HMODULE Release()
			{
				const HMODULE module = module_;
				module_ = nullptr;
				return module;
			}
			void Reset(HMODULE a_module)
			{
				if (module_)
					FreeLibrary(module_);
				module_ = a_module;
			}

		private:
			HMODULE module_ = nullptr;
		};

		struct ParameterCoreSelection
		{
			~ParameterCoreSelection()
			{
				if (file && file != INVALID_HANDLE_VALUE)
					CloseHandle(file);
				if (module)
					FreeLibrary(module);
			}

			ParameterCoreSelection() = default;
			ParameterCoreSelection(const ParameterCoreSelection&) = delete;
			ParameterCoreSelection& operator=(const ParameterCoreSelection&) = delete;

			[[nodiscard]] HMODULE ReleaseModule()
			{
				const HMODULE selected = module;
				module = nullptr;
				return selected;
			}

			[[nodiscard]] HANDLE ReleaseFile()
			{
				const HANDLE selected = file;
				file = INVALID_HANDLE_VALUE;
				return selected;
			}

			HMODULE module = nullptr;
			HANDLE file = INVALID_HANDLE_VALUE;
			std::filesystem::path path;
			std::string hash;
			ParameterCoreTrust trust = ParameterCoreTrust::Unknown;
			ParameterCoreSource source = ParameterCoreSource::None;
		};

		std::vector<HMODULE> EnumerateLoadedModules(std::string& a_error)
		{
			std::vector<HMODULE> modules(128);
			DWORD bytesNeeded = 0;
			for (;;) {
				if (!K32EnumProcessModules(
						GetCurrentProcess(), modules.data(),
						static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &bytesNeeded)) {
					a_error = std::format("K32EnumProcessModules failed with {}", GetLastError());
					return {};
				}
				if (bytesNeeded <= modules.size() * sizeof(HMODULE))
					break;
				modules.resize((bytesNeeded / sizeof(HMODULE)) + 16);
			}
			modules.resize(std::min<std::size_t>(modules.size(), bytesNeeded / sizeof(HMODULE)));
			return modules;
		}

		bool RetainModuleReference(
			HMODULE a_module, ScopedModuleReference& a_retained, std::string& a_error)
		{
			HMODULE retained = nullptr;
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
					reinterpret_cast<LPCWSTR>(a_module), &retained) ||
				retained != a_module) {
				const DWORD error = retained ? ERROR_INVALID_HANDLE : GetLastError();
				if (retained)
					FreeLibrary(retained);
				a_error = std::format("GetModuleHandleExW failed with {}", error);
				return false;
			}
			a_retained.Reset(retained);
			return true;
		}

		bool GetLoadedModulePath(
			HMODULE a_module, std::filesystem::path& a_path, std::string& a_error)
		{
			std::array<wchar_t, 32768> path{};
			const DWORD length = GetModuleFileNameW(
				a_module, path.data(), static_cast<DWORD>(path.size()));
			if (!length || length >= path.size()) {
				a_error = std::format("GetModuleFileNameW failed with {}", GetLastError());
				return false;
			}
			a_path = std::filesystem::path(std::wstring(path.data(), length));
			return true;
		}

		std::filesystem::path StripExtendedPathPrefix(std::filesystem::path a_path)
		{
			auto value = a_path.wstring();
			if (value.starts_with(L"\\\\?\\UNC\\")) {
				value = L"\\\\" + value.substr(8);
			} else if (value.starts_with(L"\\\\?\\")) {
				value.erase(0, 4);
			}
			return std::filesystem::path(std::move(value)).lexically_normal();
		}

		bool GetFinalFilePath(
			HANDLE a_file, std::filesystem::path& a_path, std::string& a_error)
		{
			constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
			const DWORD required = GetFinalPathNameByHandleW(a_file, nullptr, 0, flags);
			if (!required) {
				a_error = std::format(
					"GetFinalPathNameByHandleW size query failed with {}", GetLastError());
				return false;
			}
			std::vector<wchar_t> path(static_cast<std::size_t>(required) + 1u);
			const DWORD length = GetFinalPathNameByHandleW(
				a_file, path.data(), static_cast<DWORD>(path.size()), flags);
			if (!length || length >= path.size()) {
				a_error = std::format("GetFinalPathNameByHandleW failed with {}", GetLastError());
				return false;
			}
			a_path = StripExtendedPathPrefix(
				std::filesystem::path(std::wstring(path.data(), length)));
			return true;
		}

		bool EqualPathComponent(
			const std::filesystem::path& a_left,
			const std::filesystem::path& a_right)
		{
			return _wcsicmp(a_left.c_str(), a_right.c_str()) == 0;
		}

		bool IsPathWithinDirectory(
			const std::filesystem::path& a_path,
			const std::filesystem::path& a_directory)
		{
			const auto path = a_path.lexically_normal();
			const auto directory = a_directory.lexically_normal();
			auto pathPart = path.begin();
			for (auto directoryPart = directory.begin(); directoryPart != directory.end();
				++directoryPart, ++pathPart) {
				if (pathPart == path.end() || !EqualPathComponent(*pathPart, *directoryPart))
					return false;
			}
			return pathPart != path.end();
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

		bool RequireAbsentAdjacentParameterCore(
			const std::filesystem::path& a_path, std::string& a_error)
		{
			const DWORD attributes = GetFileAttributesW(a_path.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES) {
				a_error = std::format(
					"adjacent NGX parameter core is forbidden: {}", a_path.string());
				return false;
			}

			const DWORD error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
				return true;
			a_error = std::format(
				"could not establish absence of adjacent NGX parameter core {}: {}",
				a_path.string(), error);
			return false;
		}

		bool IsTrustedDriverStorePath(
			const std::filesystem::path& a_path, std::string& a_error)
		{
			std::array<wchar_t, 32768> systemDirectory{};
			const UINT length = GetSystemDirectoryW(
				systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
			if (!length || length >= systemDirectory.size()) {
				a_error = std::format("GetSystemDirectoryW failed with {}", GetLastError());
				return false;
			}
			const auto driverStore =
				std::filesystem::path(std::wstring(systemDirectory.data(), length)) /
				L"DriverStore" / L"FileRepository";
			if (!IsPathWithinDirectory(a_path, driverStore)) {
				a_error = "module is outside System32\\DriverStore\\FileRepository";
				return false;
			}
			const auto moduleBasename = a_path.filename().wstring();
			const bool knownDriverStoreBasename = std::ranges::any_of(
				kParameterCoreBasenames,
				[&](const std::wstring_view a_basename) {
					return _wcsicmp(moduleBasename.c_str(), a_basename.data()) == 0;
				});
			if (!knownDriverStoreBasename) {
				a_error = "module basename is neither nvngx.dll nor _nvngx.dll";
				return false;
			}
			return true;
		}

		bool VerifyAuthenticodeOffline(
			const std::filesystem::path& a_path, std::string& a_error)
		{
			WINTRUST_FILE_INFO fileInfo{};
			fileInfo.cbStruct = sizeof(fileInfo);
			fileInfo.pcwszFilePath = a_path.c_str();

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

		bool OpenAndVerifyModuleFile(
			HMODULE a_module,
			bool a_requireTrustedDriverStore,
			ScopedHandle& a_file,
			std::filesystem::path& a_path,
			std::string& a_hash,
			std::string& a_error)
		{
			std::filesystem::path loadedPath;
			if (!GetLoadedModulePath(a_module, loadedPath, a_error))
				return false;

			ScopedHandle lockedFile(CreateFileW(
				loadedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
			if (lockedFile.Get() == INVALID_HANDLE_VALUE) {
				a_error = std::format(
					"could not lock module against writes/deletion: {}", GetLastError());
				return false;
			}
			if (!GetFinalFilePath(lockedFile.Get(), a_path, a_error))
				return false;
			if (a_requireTrustedDriverStore && !IsTrustedDriverStorePath(a_path, a_error))
				return false;
			if (!VerifyLoadedModuleIdentity(a_module, lockedFile.Get(), a_error))
				return false;
			if (!ComputeFileSha256(lockedFile.Get(), a_hash, a_error))
				return false;
			if (a_requireTrustedDriverStore && !VerifyAuthenticodeOffline(a_path, a_error))
				return false;
			if (a_requireTrustedDriverStore &&
				!VerifyNvidiaParameterCoreVersionResource(a_path, a_error)) {
				return false;
			}

			a_file.Reset(lockedFile.Release());
			return true;
		}

		std::string JoinRejections(const std::vector<std::string>& a_rejections)
		{
			std::ostringstream description;
			for (std::size_t index = 0; index < a_rejections.size(); ++index) {
				if (index)
					description << "; ";
				description << a_rejections[index];
			}
			return description.str();
		}

		bool SelectParameterCore(
			HMODULE a_runtime,
			RuntimeTrust a_runtimeTrust,
			std::string_view a_runtimeHash,
			ParameterCoreSelection& a_selection,
			std::string& a_error)
		{
			if (HasParameterApi(a_runtime)) {
				if (a_runtimeTrust == RuntimeTrust::Unknown || a_runtimeHash.empty()) {
					a_error = "runtime parameter exports lack an allowlisted runtime identity";
					return false;
				}

				ScopedModuleReference retainedRuntime;
				if (!RetainModuleReference(a_runtime, retainedRuntime, a_error))
					return false;
				ScopedHandle lockedFile;
				std::filesystem::path corePath;
				std::string coreHash;
				if (!OpenAndVerifyModuleFile(
						a_runtime, false, lockedFile, corePath, coreHash, a_error)) {
					return false;
				}
				if (coreHash != a_runtimeHash) {
					a_error = "runtime parameter-core hash differs from the probed allowlisted hash";
					return false;
				}

				a_selection.module = retainedRuntime.Release();
				a_selection.file = lockedFile.Release();
				a_selection.path = std::move(corePath);
				a_selection.hash = std::move(coreHash);
				a_selection.trust = ParameterCoreTrust::RuntimeHashAllowlisted;
				a_selection.source = ParameterCoreSource::Runtime;
				return true;
			}

			std::string enumerationError;
			const auto candidates = EnumerateLoadedModules(enumerationError);
			if (!enumerationError.empty()) {
				a_error = std::move(enumerationError);
				return false;
			}

			std::vector<std::string> rejections;
			std::size_t exporterCount = 0;
			for (const HMODULE candidate : candidates) {
				ScopedModuleReference retainedCandidate;
				std::string candidateError;
				if (!RetainModuleReference(candidate, retainedCandidate, candidateError))
					continue;
				if (!HasParameterApi(retainedCandidate.Get()))
					continue;
				++exporterCount;

				ScopedHandle lockedFile;
				std::filesystem::path corePath;
				std::string coreHash;
				if (!OpenAndVerifyModuleFile(
						candidate, true, lockedFile, corePath, coreHash, candidateError)) {
					if (rejections.size() < 4) {
						rejections.push_back(std::format(
							"candidate={}: {}",
							corePath.empty() ?
								std::format("0x{:X}", reinterpret_cast<std::uintptr_t>(candidate)) :
								corePath.string(),
							candidateError));
					}
					continue;
				}

				if (a_selection.module) {
					a_error = std::format(
						"multiple trusted DriverStore parameter cores are loaded: {} and {}",
						a_selection.path.string(), corePath.string());
					return false;
				}

				a_selection.module = retainedCandidate.Release();
				a_selection.file = lockedFile.Release();
				a_selection.path = std::move(corePath);
				a_selection.hash = std::move(coreHash);
				a_selection.trust = ParameterCoreTrust::AuthenticodeVerified;
				a_selection.source = ParameterCoreSource::DriverStore;
			}

			if (!a_selection.module) {
				a_error = std::format(
					"no uniquely trusted DriverStore NGX parameter core was found among {} exporters{}{}",
					exporterCount,
					rejections.empty() ? "" : ": ",
					JoinRejections(rejections));
				return false;
			}
			return true;
		}
	}

	bool Runtime::FeatureConfiguration::Matches(
		std::uint32_t a_colorWidth, std::uint32_t a_colorHeight,
		std::uint32_t a_guideWidth, std::uint32_t a_guideHeight,
		std::uint32_t a_outputWidth, std::uint32_t a_outputHeight,
		bool a_featureUpscaling) const
	{
		return valid && colorWidth == a_colorWidth && colorHeight == a_colorHeight &&
		       guideWidth == a_guideWidth && guideHeight == a_guideHeight &&
		       outputWidth == a_outputWidth && outputHeight == a_outputHeight &&
		       featureUpscaling == a_featureUpscaling;
	}

	Runtime& Runtime::Instance()
	{
		static Runtime instance;
		return instance;
	}

	Runtime::~Runtime()
	{
		try {
			std::scoped_lock lock(mutex_);
			if (abandonRequested_.load(std::memory_order_acquire) || !ShutdownLocked())
				AbandonLocked();
		} catch (...) {
			// Raw NGX ownership must leak if teardown cannot prove it is safe.
			abandonRequested_.store(true, std::memory_order_release);
			AbandonLocked();
		}
	}

	void Runtime::SetFailureLocked(
		RuntimeStatus a_status, RuntimeFailureStage a_stage, std::string a_detail,
		std::uint32_t a_ngxResult)
	{
		status_ = a_status;
		failureStage_ = a_stage;
		detail_ = std::move(a_detail);
		ngxResult_ = a_ngxResult;

		switch (a_stage) {
		case RuntimeFailureStage::Discovery:
		case RuntimeFailureStage::Hash:
		case RuntimeFailureStage::Trust:
		case RuntimeFailureStage::Version:
		case RuntimeFailureStage::Load:
		case RuntimeFailureStage::Identity:
			LogOnceLocked(probeLogEmitted_, "probe", false);
			break;
		case RuntimeFailureStage::Initialization:
		case RuntimeFailureStage::ParameterCore:
		case RuntimeFailureStage::ParameterAllocation:
			LogOnceLocked(initializationLogEmitted_, "initialize", false);
			break;
		case RuntimeFailureStage::FeatureCreate:
			LogOnceLocked(featureCreateLogEmitted_, "create", false);
			break;
		case RuntimeFailureStage::FeatureConfiguration:
		case RuntimeFailureStage::FeatureEvaluate:
			LogOnceLocked(featureEvaluateLogEmitted_, "evaluate", false);
			break;
		default:
			break;
		}
	}

	void Runtime::LogOnceLocked(
		bool& a_emitted, const char* a_operation, bool a_succeeded)
	{
		if (a_emitted)
			return;
		a_emitted = true;
		if (a_succeeded) {
			logger::info("[DLSSNR] {} succeeded: {}", a_operation, detail_);
		} else {
			logger::error(
				"[DLSSNR] {} failed at {}: {} (status={}, ngx=0x{:08X})",
				a_operation, ToString(failureStage_), detail_, ToString(status_), ngxResult_);
		}
	}

	bool Runtime::ProbeLocked(const std::filesystem::path& a_explicitPath)
	{
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (!ShutdownLocked())
			return false;

		path_ = ResolveRuntimePath(a_explicitPath);
		if (path_.empty()) {
			SetFailureLocked(
				RuntimeStatus::NotFound, RuntimeFailureStage::Discovery,
				"nvngx_dlssnr.dll was not found in Shaders/Upscaling/Streamline");
			return false;
		}

		ScopedHandle verifiedFile(CreateFileW(
			path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (verifiedFile.Get() == INVALID_HANDLE_VALUE) {
			SetFailureLocked(
				RuntimeStatus::HashFailed, RuntimeFailureStage::Hash,
				std::format(
					"could not lock the runtime against writes/deletion: {} path={}",
					GetLastError(), path_.string()));
			return false;
		}

		std::string hashError;
		if (!ComputeFileSha256(verifiedFile.Get(), hash_, hashError)) {
			SetFailureLocked(
				RuntimeStatus::HashFailed, RuntimeFailureStage::Hash,
				std::format("{} path={}", hashError, path_.string()));
			return false;
		}

		if (hash_ == kSignedRuntimeSha256) {
			trust_ = RuntimeTrust::SignedAllowlisted;
		} else if (hash_ == kPatchedRuntimeSha256 ||
				   hash_ == kAlternatePatchedRuntimeSha256) {
			trust_ = RuntimeTrust::PatchedAllowlisted;
		} else {
			trust_ = RuntimeTrust::Unknown;
			SetFailureLocked(
				RuntimeStatus::TrustRejected, RuntimeFailureStage::Trust,
				std::format("runtime SHA-256 {} is not allowlisted", hash_));
			return false;
		}

		const auto version = Util::GetDllVersion(path_.wstring());
		if (!version) {
			SetFailureLocked(
				RuntimeStatus::VersionRejected, RuntimeFailureStage::Version,
				"allowlisted runtime has no readable version resource");
			return false;
		}
		version_ = Util::GetFormattedVersion(*version);
		if (version->major() != 310 || version->minor() != 8) {
			SetFailureLocked(
				RuntimeStatus::VersionRejected, RuntimeFailureStage::Version,
				std::format("expected DLSSNR 310.8.x, found {}", version_));
			return false;
		}

		module_ = LoadLibraryExW(
			path_.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module_) {
			SetFailureLocked(
				RuntimeStatus::LoadFailed, RuntimeFailureStage::Load,
				std::format("LoadLibraryExW failed with {}", GetLastError()));
			return false;
		}

		const auto runtime = static_cast<HMODULE>(module_);
		std::string identityError;
		if (!VerifyLoadedModuleIdentity(runtime, verifiedFile.Get(), identityError)) {
			SetFailureLocked(
				RuntimeStatus::IdentityFailed, RuntimeFailureStage::Identity,
				std::format("{} path={}", identityError, path_.string()));
			FreeLibrary(runtime);
			module_ = nullptr;
			return false;
		}
		for (const char* exportName : kRequiredExports) {
			if (!GetProcAddress(runtime, exportName)) {
				SetFailureLocked(
					RuntimeStatus::MissingExport, RuntimeFailureStage::Load,
					std::format("missing required export {}", exportName));
				FreeLibrary(runtime);
				module_ = nullptr;
				return false;
			}
		}

		const auto getApplicationId = reinterpret_cast<GetUnsignedValue>(
			GetProcAddress(runtime, "NVSDK_NGX_GetApplicationId"));
		const auto getApiVersion = reinterpret_cast<GetUnsignedValue>(
			GetProcAddress(runtime, "NVSDK_NGX_GetAPIVersion"));
		if (!getApplicationId || !getApiVersion) {
			SetFailureLocked(
				RuntimeStatus::IdentityFailed, RuntimeFailureStage::Identity,
				"runtime identity exports are missing");
			FreeLibrary(runtime);
			module_ = nullptr;
			return false;
		}

		applicationId_ = getApplicationId();
		apiVersion_ = getApiVersion();
		status_ = RuntimeStatus::Ready;
		failureStage_ = RuntimeFailureStage::None;
		ngxResult_ = 0;
		detail_ = std::format(
			"trusted runtime ready trust={} hash={} version={} appId=0x{:08X} api=0x{:X}",
			ToString(trust_), hash_, version_, applicationId_, apiVersion_);
		LogOnceLocked(probeLogEmitted_, "probe", true);
		return true;
	}

	bool Runtime::Probe(const std::filesystem::path& a_explicitPath)
	{
		std::scoped_lock lock(mutex_);
		return ProbeLocked(a_explicitPath);
	}

	bool Runtime::Initialize(ID3D12Device* a_device, const std::filesystem::path& a_dataPath)
	{
		std::scoped_lock lock(mutex_);
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (!a_device) {
			SetFailureLocked(
				RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				"D3D12 device is null", static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}
		if (status_ == RuntimeStatus::Initialized && device_ == a_device)
			return true;
		if (device_) {
			const auto runtimePath = path_;
			if (!ShutdownLocked() || !ProbeLocked(runtimePath))
				return false;
		}
		if (!module_ && !ProbeLocked({}))
			return false;

		const auto runtime = static_cast<HMODULE>(module_);
		const auto initialize = reinterpret_cast<InitD3D12>(
			GetProcAddress(runtime, "NVSDK_NGX_D3D12_Init_Ext"));
		if (!initialize) {
			SetFailureLocked(
				RuntimeStatus::MissingExport, RuntimeFailureStage::Load,
				"NVSDK_NGX_D3D12_Init_Ext disappeared after probe");
			return false;
		}

		std::string coreSelectionError;
		for (const auto parameterCoreBasename : kParameterCoreBasenames) {
			const auto adjacentCorePath = path_.parent_path() / parameterCoreBasename;
			if (!RequireAbsentAdjacentParameterCore(adjacentCorePath, coreSelectionError)) {
				SetFailureLocked(
					RuntimeStatus::CoreUnavailable, RuntimeFailureStage::ParameterCore,
					std::move(coreSelectionError));
				return false;
			}
		}

		ParameterCoreSelection coreSelection;
		if (!SelectParameterCore(
				runtime,
				trust_,
				hash_,
				coreSelection,
				coreSelectionError)) {
			SetFailureLocked(
				RuntimeStatus::CoreUnavailable, RuntimeFailureStage::ParameterCore,
				std::format("NGX parameter-core selection failed: {}", coreSelectionError));
			return false;
		}

		std::filesystem::path writablePath = a_dataPath;
		if (writablePath.empty()) {
			std::array<wchar_t, 32768> temporaryPath{};
			const DWORD length = GetTempPathW(
				static_cast<DWORD>(temporaryPath.size()), temporaryPath.data());
			if (!length || length >= temporaryPath.size()) {
				SetFailureLocked(
					RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
					std::format("GetTempPathW failed with {}", GetLastError()));
				return false;
			}
			writablePath = std::filesystem::path(
							   std::wstring(temporaryPath.data(), length)) /
			               L"CommunityShaders-NGX";
		}
		std::error_code directoryError;
		std::filesystem::create_directories(writablePath, directoryError);
		if (directoryError) {
			SetFailureLocked(
				RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				std::format("could not create NGX data directory: {}", directoryError.message()));
			return false;
		}

		NVSDK_NGX_Result initializeResult = NVSDK_NGX_Result_Fail;
		{
			RuntimeCallerPathScope pathProxy(runtime, path_.parent_path() / L"nvngx.dll");
			lastPathProxyInstalled_ = pathProxy.IsInstalled();
			if (!lastPathProxyInstalled_) {
				SetFailureLocked(
					RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
					"failed to install the NGX caller-path proxy");
				return false;
			}
			initializeResult = initialize(
				applicationId_, writablePath.c_str(), a_device,
				static_cast<NVSDK_NGX_Version>(apiVersion_), nullptr);
			lastPathProxyHits_ = pathProxy.Hits();
		}
		ngxResult_ = static_cast<std::uint32_t>(initializeResult);
		if (NVSDK_NGX_FAILED(initializeResult)) {
			SetFailureLocked(
				RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				std::format(
					"NGX initialization failed 0x{:08X} proxyInstalled={} proxyHits={}",
					ngxResult_, lastPathProxyInstalled_, lastPathProxyHits_),
				ngxResult_);
			return false;
		}

		device_ = a_device;
		a_device->AddRef();
		const auto rollbackInitializationFailure = [this]() {
			const RuntimeStatus primaryStatus = status_;
			const RuntimeFailureStage primaryStage = failureStage_;
			const std::string primaryDetail = detail_;
			const std::uint32_t primaryNgxResult = ngxResult_;
			const auto runtimePath = path_;
			const std::string runtimeHash = hash_;
			const std::string runtimeVersion = version_;
			const RuntimeTrust runtimeTrust = trust_;
			const auto corePath = corePath_;
			const std::string coreHash = coreHash_;
			const ParameterCoreTrust coreTrust = coreTrust_;
			const ParameterCoreSource coreSource = coreSource_;
			const std::uint32_t proxyHits = lastPathProxyHits_;
			const bool proxyInstalled = lastPathProxyInstalled_;

			if (ShutdownLocked()) {
				path_ = runtimePath;
				hash_ = runtimeHash;
				version_ = runtimeVersion;
				trust_ = runtimeTrust;
				corePath_ = corePath;
				coreHash_ = coreHash;
				coreTrust_ = coreTrust;
				coreSource_ = coreSource;
				lastPathProxyHits_ = proxyHits;
				lastPathProxyInstalled_ = proxyInstalled;
				status_ = primaryStatus;
				failureStage_ = primaryStage;
				detail_ = primaryDetail;
				ngxResult_ = primaryNgxResult;
				probeLogEmitted_ = true;
				initializationLogEmitted_ = true;
				return;
			}

			const std::string rollbackDetail = detail_;
			detail_ = std::format(
				"{}; NGX initialization rollback failed: {}",
				primaryDetail,
				rollbackDetail);
			logger::critical("[DLSSNR] {}", detail_);
		};

		corePath_ = std::move(coreSelection.path);
		coreHash_ = std::move(coreSelection.hash);
		coreTrust_ = coreSelection.trust;
		coreSource_ = coreSelection.source;
		coreModule_ = coreSelection.ReleaseModule();
		coreFile_ = coreSelection.ReleaseFile();
		const auto core = static_cast<HMODULE>(coreModule_);

		const auto allocateParameters = reinterpret_cast<AllocateParameters>(
			GetProcAddress(core, "NVSDK_NGX_D3D12_AllocateParameters"));
		NVSDK_NGX_Parameter* parameters = nullptr;
		const auto allocationResult = allocateParameters ?
		                                  allocateParameters(&parameters) :
		                                  NVSDK_NGX_Result_Fail;
		ngxResult_ = static_cast<std::uint32_t>(allocationResult);
		parameters_ = parameters;
		if (NVSDK_NGX_FAILED(allocationResult) || !parameters) {
			SetFailureLocked(
				RuntimeStatus::ParameterAllocationFailed,
				RuntimeFailureStage::ParameterAllocation,
				std::format("NGX parameter allocation failed 0x{:08X}", ngxResult_),
				ngxResult_);
			rollbackInitializationFailure();
			return false;
		}

		status_ = RuntimeStatus::Initialized;
		failureStage_ = RuntimeFailureStage::None;
		detail_ = std::format(
			"NGX Feature 18 runtime initialized trust={} proxyHits={} coreSource={} coreTrust={} coreHash={} corePath={}",
			ToString(trust_),
			lastPathProxyHits_,
			ToString(coreSource_),
			ToString(coreTrust_),
			coreHash_,
			corePath_.string());
		LogOnceLocked(initializationLogEmitted_, "initialize", true);
		return true;
	}

	bool Runtime::Execute(
		ID3D12GraphicsCommandList* a_commandList, std::uint32_t a_slot,
		ID3D12Resource* a_color, ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors, ID3D12Resource* a_output,
		ID3D12Resource* a_controlMask,
		std::uint32_t a_colorWidth, std::uint32_t a_colorHeight,
		std::uint32_t a_guideWidth, std::uint32_t a_guideHeight,
		std::uint32_t a_outputWidth, std::uint32_t a_outputHeight,
		std::uint32_t a_controlMaskWidth, std::uint32_t a_controlMaskHeight,
		float a_motionVectorScaleX, float a_motionVectorScaleY,
		bool a_featureUpscaling, const Tuning& a_tuning, bool a_reset,
		bool* a_evaluationAttempted)
	{
		if (a_evaluationAttempted)
			*a_evaluationAttempted = false;
		std::scoped_lock lock(mutex_);
		const bool hasControlMask = a_controlMask != nullptr;
		const bool controlMaskContractValid =
			hasControlMask ?
				(!a_tuning.useAutoMask &&
					a_controlMaskWidth != 0 &&
					a_controlMaskHeight != 0 &&
					a_controlMaskWidth == a_outputWidth &&
					a_controlMaskHeight == a_outputHeight) :
				(a_tuning.useAutoMask &&
					a_controlMaskWidth == 0 &&
					a_controlMaskHeight == 0);
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (status_ != RuntimeStatus::Initialized || !a_commandList ||
			a_slot >= kFeatureSlotCount || !a_color || !a_depth ||
			!a_motionVectors || !a_output || !a_colorWidth || !a_colorHeight ||
			!a_guideWidth || !a_guideHeight || !a_outputWidth || !a_outputHeight ||
			!std::isfinite(a_motionVectorScaleX) || a_motionVectorScaleX <= 0.0f ||
			!std::isfinite(a_motionVectorScaleY) || a_motionVectorScaleY <= 0.0f ||
			!std::isfinite(a_tuning.intensity) ||
			!std::isfinite(a_tuning.localToneStrength) ||
			!std::isfinite(a_tuning.localStructureStrength) ||
			!std::isfinite(a_tuning.skinStructureStrength) ||
			a_tuning.uiCorrection || !controlMaskContractValid) {
			SetFailureLocked(
				RuntimeStatus::FeatureEvaluateFailed,
				RuntimeFailureStage::FeatureEvaluate,
				controlMaskContractValid ?
					"Feature 18 evaluation arguments are invalid" :
					"Feature 18 control-mask presence, dimensions, and automatic-mask mode are inconsistent",
				static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}

		const auto runtime = static_cast<HMODULE>(module_);
		const auto createFeature = reinterpret_cast<CreateFeature>(
			GetProcAddress(runtime, "NVSDK_NGX_D3D12_CreateFeature"));
		const auto evaluateFeature = reinterpret_cast<EvaluateFeature>(
			GetProcAddress(runtime, "NVSDK_NGX_D3D12_EvaluateFeature"));
		if (!createFeature || !evaluateFeature) {
			SetFailureLocked(
				RuntimeStatus::MissingExport, RuntimeFailureStage::Load,
				"Feature 18 create/evaluate exports disappeared after probe");
			return false;
		}

		auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
		RuntimeCallerPathScope pathProxy(runtime, path_.parent_path() / L"nvngx.dll");
		lastPathProxyInstalled_ = pathProxy.IsInstalled();
		if (!lastPathProxyInstalled_) {
			SetFailureLocked(
				RuntimeStatus::FeatureCreateFailed,
				RuntimeFailureStage::FeatureCreate,
				"failed to install the NGX caller-path proxy for Feature 18");
			return false;
		}

		const bool configurationChanged = !featureConfigurations_[a_slot].Matches(
			a_colorWidth, a_colorHeight, a_guideWidth, a_guideHeight,
			a_outputWidth, a_outputHeight, a_featureUpscaling);
		if (featureHandles_[a_slot] && configurationChanged) {
			SetFailureLocked(
				RuntimeStatus::FeatureConfigurationChanged,
				RuntimeFailureStage::FeatureConfiguration,
				std::format(
					"Feature 18 slot {} configuration changed while its handle is live; caller must prove GPU idle and reset the slot",
					a_slot),
				static_cast<std::uint32_t>(E_PENDING));
			return false;
		}

		if (!featureHandles_[a_slot]) {
			parameters->Reset();
			parameters->Set("Width", a_outputWidth);
			parameters->Set("Height", a_outputHeight);
			parameters->Set("OutWidth", a_outputWidth);
			parameters->Set("OutHeight", a_outputHeight);
			parameters->Set("DLSSNR.Width", a_outputWidth);
			parameters->Set("DLSSNR.Height", a_outputHeight);
			parameters->Set("DLSSNR.InputWidth", a_guideWidth);
			parameters->Set("DLSSNR.InputHeight", a_guideHeight);
			parameters->Set("DLSSNR.OutputWidth", a_outputWidth);
			parameters->Set("DLSSNR.OutputHeight", a_outputHeight);
			parameters->Set("DLSSNR.Output.Width", a_outputWidth);
			parameters->Set("DLSSNR.Output.Height", a_outputHeight);
			parameters->Set(
				"DLSSNR.Scale", static_cast<float>(a_outputWidth) / a_guideWidth);
			parameters->Set("DLSSNR.Upscaling", a_featureUpscaling ? 1u : 0u);
			parameters->Set(
				"DLSSNR.ScalingRatio",
				static_cast<float>(a_outputWidth) / a_guideWidth);
			parameters->Set("DLSSNR.Hint.Render.Preset", 0u);

			NVSDK_NGX_Handle* handle = nullptr;
			const auto createResult = createFeature(
				a_commandList, kFeatureDlssNr, parameters, &handle);
			ngxResult_ = static_cast<std::uint32_t>(createResult);
			lastPathProxyHits_ = pathProxy.Hits();
			if (NVSDK_NGX_FAILED(createResult) || !handle) {
				// A failed private feature create may still transfer a handle. Keep it
				// owned so bounded teardown can release or intentionally abandon it.
				if (handle)
					featureHandles_[a_slot] = handle;
				SetFailureLocked(
					RuntimeStatus::FeatureCreateFailed,
					RuntimeFailureStage::FeatureCreate,
					std::format(
						"Feature 18 create failed slot={} result=0x{:08X} trust={} proxyHits={}",
						a_slot, ngxResult_, ToString(trust_), lastPathProxyHits_),
					ngxResult_);
				return false;
			}

			featureHandles_[a_slot] = handle;
			featureConfigurations_[a_slot] = {
				.colorWidth = a_colorWidth,
				.colorHeight = a_colorHeight,
				.guideWidth = a_guideWidth,
				.guideHeight = a_guideHeight,
				.outputWidth = a_outputWidth,
				.outputHeight = a_outputHeight,
				.featureUpscaling = a_featureUpscaling,
				.valid = true,
			};
			detail_ = std::format(
				"Feature 18 created slot={} upscaling={} color={}x{} guides={}x{} output={}x{}",
				a_slot, a_featureUpscaling, a_colorWidth, a_colorHeight,
				a_guideWidth, a_guideHeight, a_outputWidth, a_outputHeight);
			LogOnceLocked(featureCreateLogEmitted_, "create", true);
			a_reset = true;
		}

		parameters->Reset();
		parameters->Set("DLSSNR.Color", a_color);
		parameters->Set("DLSSNR.Depth", a_depth);
		parameters->Set("DLSSNR.MVec", a_motionVectors);
		parameters->Set("DLSSNR.Output", a_output);
		if (hasControlMask) {
			parameters->Set("DLSSNR.ControlMask", a_controlMask);
			parameters->Set("DLSSNR.ControlMaskSubrectBaseX", 0u);
			parameters->Set("DLSSNR.ControlMaskSubrectBaseY", 0u);
			parameters->Set("DLSSNR.ControlMaskSubrectWidth", a_controlMaskWidth);
			parameters->Set("DLSSNR.ControlMaskSubrectHeight", a_controlMaskHeight);
		}
		parameters->Set("DLSSNR.ColorSubrectBaseX", 0u);
		parameters->Set("DLSSNR.ColorSubrectBaseY", 0u);
		parameters->Set("DLSSNR.ColorSubrectWidth", a_colorWidth);
		parameters->Set("DLSSNR.ColorSubrectHeight", a_colorHeight);
		parameters->Set("DLSSNR.DepthSubrectBaseX", 0u);
		parameters->Set("DLSSNR.DepthSubrectBaseY", 0u);
		parameters->Set("DLSSNR.DepthSubrectWidth", a_guideWidth);
		parameters->Set("DLSSNR.DepthSubrectHeight", a_guideHeight);
		parameters->Set("DLSSNR.MVecSubrectBaseX", 0u);
		parameters->Set("DLSSNR.MVecSubrectBaseY", 0u);
		parameters->Set("DLSSNR.MVecSubrectWidth", a_guideWidth);
		parameters->Set("DLSSNR.MVecSubrectHeight", a_guideHeight);
		parameters->Set("DLSSNR.OutputSubrectBaseX", 0u);
		parameters->Set("DLSSNR.OutputSubrectBaseY", 0u);
		parameters->Set("DLSSNR.OutputSubrectWidth", a_outputWidth);
		parameters->Set("DLSSNR.OutputSubrectHeight", a_outputHeight);
		parameters->Set("DLSSNR.MVecScaleX", a_motionVectorScaleX);
		parameters->Set("DLSSNR.MVecScaleY", a_motionVectorScaleY);
		parameters->Set("DLSSNR.DepthInverted", 0u);
		parameters->Set("DLSSNR.Enabled", 1u);
		parameters->Set("DLSSNR.Reset", a_reset ? 1u : 0u);
		parameters->Set("DLSSNR.Intensity", a_tuning.intensity);
		parameters->Set("DLSSNR.LocalToneStrength", a_tuning.localToneStrength);
		parameters->Set(
			"DLSSNR.LocalStructureStrength", a_tuning.localStructureStrength);
		parameters->Set(
			"DLSSNR.SkinStructureStrength", a_tuning.skinStructureStrength);
		parameters->Set("DLSSNR.UseAutoMask", a_tuning.useAutoMask ? 1u : 0u);
		parameters->Set("DLSSNR.Style", a_tuning.style);
		parameters->Set("DLSSNR.UICorrection", a_tuning.uiCorrection ? 1u : 0u);

		if (a_evaluationAttempted)
			*a_evaluationAttempted = true;
		const auto evaluateResult = evaluateFeature(
			a_commandList, static_cast<NVSDK_NGX_Handle*>(featureHandles_[a_slot]),
			parameters, nullptr);
		ngxResult_ = static_cast<std::uint32_t>(evaluateResult);
		lastPathProxyHits_ = pathProxy.Hits();
		if (NVSDK_NGX_FAILED(evaluateResult)) {
			SetFailureLocked(
				RuntimeStatus::FeatureEvaluateFailed,
				RuntimeFailureStage::FeatureEvaluate,
				std::format(
					"Feature 18 evaluate failed slot={} result=0x{:08X} trust={} proxyHits={}",
					a_slot, ngxResult_, ToString(trust_), lastPathProxyHits_),
				ngxResult_);
			return false;
		}

		++successfulFrames_;
		status_ = RuntimeStatus::Initialized;
		failureStage_ = RuntimeFailureStage::None;
		detail_ = std::format(
			"Feature 18 evaluated slot={} upscaling={} color={}x{} guides={}x{} output={}x{} controlMask={}x{} proxyHits={}",
			a_slot, a_featureUpscaling, a_colorWidth, a_colorHeight, a_guideWidth,
			a_guideHeight, a_outputWidth, a_outputHeight, a_controlMaskWidth,
			a_controlMaskHeight, lastPathProxyHits_);
		LogOnceLocked(featureEvaluateLogEmitted_, "evaluate", true);
		return true;
	}

	bool Runtime::ResetFeatureLocked(std::uint32_t a_slot)
	{
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (a_slot >= kFeatureSlotCount) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				"Feature 18 release slot is out of range",
				static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}
		if (!featureHandles_[a_slot]) {
			featureConfigurations_[a_slot] = {};
			return true;
		}
		if (!module_) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				"Feature 18 handle exists without its runtime module");
			return false;
		}

		const auto runtime = static_cast<HMODULE>(module_);
		const auto releaseFeature = reinterpret_cast<ReleaseFeature>(
			GetProcAddress(runtime, "NVSDK_NGX_D3D12_ReleaseFeature"));
		RuntimeCallerPathScope pathProxy(runtime, path_.parent_path() / L"nvngx.dll");
		lastPathProxyInstalled_ = pathProxy.IsInstalled();
		if (!releaseFeature || !lastPathProxyInstalled_) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				"Feature 18 release export or caller-path proxy is unavailable");
			return false;
		}

		const auto releaseResult = releaseFeature(
			static_cast<NVSDK_NGX_Handle*>(featureHandles_[a_slot]));
		ngxResult_ = static_cast<std::uint32_t>(releaseResult);
		lastPathProxyHits_ = pathProxy.Hits();
		if (NVSDK_NGX_FAILED(releaseResult)) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				std::format(
					"Feature 18 release failed slot={} result=0x{:08X} proxyHits={}",
					a_slot, ngxResult_, lastPathProxyHits_),
				ngxResult_);
			return false;
		}

		featureHandles_[a_slot] = nullptr;
		featureConfigurations_[a_slot] = {};
		if (device_) {
			status_ = RuntimeStatus::Initialized;
			failureStage_ = RuntimeFailureStage::None;
		}
		return true;
	}

	bool Runtime::ResetFeature(std::uint32_t a_slot)
	{
		std::scoped_lock lock(mutex_);
		return ResetFeatureLocked(a_slot);
	}

	bool Runtime::ResetFeaturesLocked()
	{
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		bool succeeded = true;
		for (std::uint32_t slot = 0; slot < kFeatureSlotCount; ++slot) {
			if (!ResetFeatureLocked(slot))
				succeeded = false;
		}
		if (succeeded)
			successfulFrames_ = 0;
		return succeeded;
	}

	bool Runtime::ResetFeatures()
	{
		std::scoped_lock lock(mutex_);
		return ResetFeaturesLocked();
	}

	bool Runtime::ShutdownLocked()
	{
		if (abandonRequested_.load(std::memory_order_acquire)) {
			AbandonLocked();
			return false;
		}
		if (abandoned_)
			return true;
		if (device_ && module_) {
			if (!ResetFeaturesLocked())
				return false;

			if (parameters_) {
				const auto core = static_cast<HMODULE>(coreModule_);
				DestroyParameters destroyParameters = nullptr;
				if (core) {
					destroyParameters = reinterpret_cast<DestroyParameters>(
						GetProcAddress(core, "NVSDK_NGX_D3D12_DestroyParameters"));
				}
				if (!destroyParameters) {
					SetFailureLocked(
						RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
						"NGX parameter destroy export is unavailable");
					return false;
				}
				const auto destroyResult = destroyParameters(
					static_cast<NVSDK_NGX_Parameter*>(parameters_));
				if (NVSDK_NGX_FAILED(destroyResult)) {
					SetFailureLocked(
						RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
						std::format(
							"NGX parameter destruction failed 0x{:08X}",
							static_cast<std::uint32_t>(destroyResult)),
						static_cast<std::uint32_t>(destroyResult));
					return false;
				}
				parameters_ = nullptr;
			}

			const auto runtime = static_cast<HMODULE>(module_);
			const auto shutdown = reinterpret_cast<ShutdownD3D12>(
				GetProcAddress(runtime, "NVSDK_NGX_D3D12_Shutdown1"));
			RuntimeCallerPathScope pathProxy(runtime, path_.parent_path() / L"nvngx.dll");
			lastPathProxyInstalled_ = pathProxy.IsInstalled();
			if (!shutdown || !lastPathProxyInstalled_) {
				SetFailureLocked(
					RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					"NGX shutdown export or caller-path proxy is unavailable");
				return false;
			}
			const auto shutdownResult = shutdown(static_cast<ID3D12Device*>(device_));
			ngxResult_ = static_cast<std::uint32_t>(shutdownResult);
			lastPathProxyHits_ = pathProxy.Hits();
			if (NVSDK_NGX_FAILED(shutdownResult)) {
				SetFailureLocked(
					RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					std::format(
						"NGX shutdown failed 0x{:08X} proxyHits={}",
						ngxResult_, lastPathProxyHits_),
					ngxResult_);
				return false;
			}

			static_cast<ID3D12Device*>(device_)->Release();
			device_ = nullptr;
		}

		if (coreFile_) {
			if (!CloseHandle(static_cast<HANDLE>(coreFile_))) {
				SetFailureLocked(
					RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					std::format(
						"CloseHandle(parameter core file) failed with {}", GetLastError()));
				return false;
			}
			coreFile_ = nullptr;
		}

		if (coreModule_) {
			if (!FreeLibrary(static_cast<HMODULE>(coreModule_))) {
				SetFailureLocked(
					RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					std::format("FreeLibrary(parameter core) failed with {}", GetLastError()));
				return false;
			}
			coreModule_ = nullptr;
		}

		if (module_) {
			const auto runtime = static_cast<HMODULE>(module_);
			if (IsRuntimeCallerPathHookActive(runtime)) {
				SetFailureLocked(
					RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					"caller-path proxy restoration failed; runtime retained for safety");
				return false;
			}
			if (!FreeLibrary(runtime)) {
				SetFailureLocked(
					RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					std::format("FreeLibrary(runtime) failed with {}", GetLastError()));
				return false;
			}
		}
		module_ = nullptr;
		path_.clear();
		hash_.clear();
		version_.clear();
		corePath_.clear();
		coreHash_.clear();
		coreTrust_ = ParameterCoreTrust::Unknown;
		coreSource_ = ParameterCoreSource::None;
		detail_.clear();
		featureHandles_ = {};
		featureConfigurations_ = {};
		status_ = RuntimeStatus::NotProbed;
		trust_ = RuntimeTrust::Unknown;
		failureStage_ = RuntimeFailureStage::None;
		ngxResult_ = 0;
		applicationId_ = 0;
		apiVersion_ = 0;
		lastPathProxyHits_ = 0;
		successfulFrames_ = 0;
		lastPathProxyInstalled_ = false;
		probeLogEmitted_ = false;
		initializationLogEmitted_ = false;
		featureCreateLogEmitted_ = false;
		featureEvaluateLogEmitted_ = false;
		abandoned_ = false;
		return true;
	}

	void Runtime::AbandonLocked() noexcept
	{
		if (abandoned_)
			return;
		// Failed teardown retains NGX ownership intentionally rather than releasing objects that may still be in use.
		module_ = nullptr;
		coreModule_ = nullptr;
		coreFile_ = nullptr;
		device_ = nullptr;
		parameters_ = nullptr;
		featureHandles_ = {};
		featureConfigurations_ = {};
		failureStage_ = RuntimeFailureStage::UnsafeAbandon;
		status_ = RuntimeStatus::UnsafeAbandoned;
		abandoned_ = true;
		try {
			detail_ = "NGX objects intentionally retained after unsafe teardown";
			logger::critical("[DLSSNR] unsafe ownership abandoned: {}", detail_);
		} catch (...) {
		}
	}

	bool Runtime::Shutdown()
	{
		std::scoped_lock lock(mutex_);
		return ShutdownLocked();
	}

	void Runtime::AbandonUnsafe() noexcept
	{
		abandonRequested_.store(true, std::memory_order_release);
		try {
			std::scoped_lock lock(mutex_);
			AbandonLocked();
		} catch (...) {
		}
	}

	RuntimeStatus Runtime::Status() const
	{
		std::scoped_lock lock(mutex_);
		return status_;
	}

	RuntimeTrust Runtime::Trust() const
	{
		std::scoped_lock lock(mutex_);
		return trust_;
	}

	RuntimeFailureStage Runtime::FailureStage() const
	{
		std::scoped_lock lock(mutex_);
		return failureStage_;
	}

	std::filesystem::path Runtime::Path() const
	{
		std::scoped_lock lock(mutex_);
		return path_;
	}

	std::string Runtime::Hash() const
	{
		std::scoped_lock lock(mutex_);
		return hash_;
	}

	std::string Runtime::Version() const
	{
		std::scoped_lock lock(mutex_);
		return version_;
	}

	std::filesystem::path Runtime::ParameterCorePath() const
	{
		std::scoped_lock lock(mutex_);
		return corePath_;
	}

	std::string Runtime::ParameterCoreHash() const
	{
		std::scoped_lock lock(mutex_);
		return coreHash_;
	}

	ParameterCoreTrust Runtime::CoreTrust() const
	{
		std::scoped_lock lock(mutex_);
		return coreTrust_;
	}

	ParameterCoreSource Runtime::CoreSource() const
	{
		std::scoped_lock lock(mutex_);
		return coreSource_;
	}

	std::string Runtime::Detail() const
	{
		std::scoped_lock lock(mutex_);
		return detail_;
	}

	std::uint32_t Runtime::NgxResult() const
	{
		std::scoped_lock lock(mutex_);
		return ngxResult_;
	}

	std::uint64_t Runtime::SuccessfulFrames() const
	{
		std::scoped_lock lock(mutex_);
		return successfulFrames_;
	}

	std::uint32_t Runtime::LastPathProxyHits() const
	{
		std::scoped_lock lock(mutex_);
		return lastPathProxyHits_;
	}

	bool Runtime::LastPathProxyInstalled() const
	{
		std::scoped_lock lock(mutex_);
		return lastPathProxyInstalled_;
	}

	const char* ToString(RuntimeStatus a_status)
	{
		switch (a_status) {
		case RuntimeStatus::NotProbed:
			return "not-probed";
		case RuntimeStatus::NotFound:
			return "not-found";
		case RuntimeStatus::HashFailed:
			return "hash-failed";
		case RuntimeStatus::TrustRejected:
			return "trust-rejected";
		case RuntimeStatus::VersionRejected:
			return "version-rejected";
		case RuntimeStatus::LoadFailed:
			return "load-failed";
		case RuntimeStatus::MissingExport:
			return "missing-export";
		case RuntimeStatus::IdentityFailed:
			return "identity-failed";
		case RuntimeStatus::Ready:
			return "ready";
		case RuntimeStatus::InitializationFailed:
			return "initialization-failed";
		case RuntimeStatus::CoreUnavailable:
			return "core-unavailable";
		case RuntimeStatus::ParameterAllocationFailed:
			return "parameter-allocation-failed";
		case RuntimeStatus::Initialized:
			return "initialized";
		case RuntimeStatus::FeatureConfigurationChanged:
			return "feature-configuration-changed";
		case RuntimeStatus::FeatureCreateFailed:
			return "feature-create-failed";
		case RuntimeStatus::FeatureEvaluateFailed:
			return "feature-evaluate-failed";
		case RuntimeStatus::FeatureReleaseFailed:
			return "feature-release-failed";
		case RuntimeStatus::ShutdownFailed:
			return "shutdown-failed";
		case RuntimeStatus::UnsafeAbandoned:
			return "unsafe-abandoned";
		}
		return "unknown";
	}

	const char* ToString(RuntimeTrust a_trust)
	{
		switch (a_trust) {
		case RuntimeTrust::Unknown:
			return "unknown";
		case RuntimeTrust::SignedAllowlisted:
			return "signed-allowlisted";
		case RuntimeTrust::PatchedAllowlisted:
			return "patched-allowlisted";
		}
		return "unknown";
	}

	const char* ToString(RuntimeFailureStage a_stage)
	{
		switch (a_stage) {
		case RuntimeFailureStage::None:
			return "none";
		case RuntimeFailureStage::Discovery:
			return "discovery";
		case RuntimeFailureStage::Hash:
			return "hash";
		case RuntimeFailureStage::Trust:
			return "trust";
		case RuntimeFailureStage::Version:
			return "version";
		case RuntimeFailureStage::Load:
			return "load";
		case RuntimeFailureStage::Identity:
			return "identity";
		case RuntimeFailureStage::Initialization:
			return "initialization";
		case RuntimeFailureStage::ParameterCore:
			return "parameter-core";
		case RuntimeFailureStage::ParameterAllocation:
			return "parameter-allocation";
		case RuntimeFailureStage::FeatureConfiguration:
			return "feature-configuration";
		case RuntimeFailureStage::FeatureCreate:
			return "feature-create";
		case RuntimeFailureStage::FeatureEvaluate:
			return "feature-evaluate";
		case RuntimeFailureStage::FeatureRelease:
			return "feature-release";
		case RuntimeFailureStage::Shutdown:
			return "shutdown";
		case RuntimeFailureStage::UnsafeAbandon:
			return "unsafe-abandon";
		}
		return "unknown";
	}

	const char* ToString(ParameterCoreTrust a_trust)
	{
		switch (a_trust) {
		case ParameterCoreTrust::Unknown:
			return "unknown";
		case ParameterCoreTrust::RuntimeHashAllowlisted:
			return "runtime-hash-allowlisted";
		case ParameterCoreTrust::AuthenticodeVerified:
			return "authenticode-verified";
		}
		return "unknown";
	}

	const char* ToString(ParameterCoreSource a_source)
	{
		switch (a_source) {
		case ParameterCoreSource::None:
			return "none";
		case ParameterCoreSource::Runtime:
			return "runtime";
		case ParameterCoreSource::DriverStore:
			return "driver-store";
		}
		return "unknown";
	}
}
