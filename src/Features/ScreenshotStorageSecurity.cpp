#include "Features/ScreenshotStorageSecurity.h"

#include <Windows.h>
#include <bcrypt.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <format>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CSX::ScreenshotStorage
{
#ifdef CSX_SCREENSHOT_STORAGE_TESTING
	namespace
	{
		std::atomic<DirectoryCreationTestHook> g_directoryCreationTestHook{ nullptr };
		std::atomic<DirectoryCreationTestHook> g_destinationOpeningTestHook{ nullptr };
	}

	void SetDirectoryCreationTestHook(DirectoryCreationTestHook a_hook) noexcept
	{
		g_directoryCreationTestHook.store(a_hook, std::memory_order_release);
	}

	void SetDestinationOpeningTestHook(DirectoryCreationTestHook a_hook) noexcept
	{
		g_destinationOpeningTestHook.store(a_hook, std::memory_order_release);
	}
#endif

	namespace
	{
		constexpr std::size_t kMaximumDestinationComponents = 256u;

		class ScopedHandle final
		{
		public:
			explicit ScopedHandle(HANDLE a_handle = INVALID_HANDLE_VALUE) : handle(a_handle) {}
			~ScopedHandle()
			{
				if (handle != INVALID_HANDLE_VALUE)
					CloseHandle(handle);
			}

			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;
			ScopedHandle(ScopedHandle&& a_other) noexcept : handle(a_other.Release()) {}

			HANDLE Get() const noexcept { return handle; }
			HANDLE Release() noexcept
			{
				const auto released = handle;
				handle = INVALID_HANDLE_VALUE;
				return released;
			}

		private:
			HANDLE handle;
		};

		std::string Identity(const BY_HANDLE_FILE_INFORMATION& a_information)
		{
			return std::format(
				"{:08x}:{:08x}:{:08x}", a_information.dwVolumeSerialNumber,
				a_information.nFileIndexHigh, a_information.nFileIndexLow);
		}

		BY_HANDLE_FILE_INFORMATION ReadIdentity(HANDLE a_handle, bool a_directory)
		{
			BY_HANDLE_FILE_INFORMATION information{};
			if (!GetFileInformationByHandle(a_handle, &information))
				throw std::runtime_error(std::format("file identity query failed with Win32 error {}", GetLastError()));
			const bool isDirectory = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			if (isDirectory != a_directory ||
				(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
				throw std::runtime_error(a_directory ?
											 "sequence storage is not a stable non-reparse directory" :
											 "committed artifact is not a stable non-reparse regular file");
			}
			return information;
		}

		std::filesystem::path NormalizeFinalPath(std::wstring a_path)
		{
			static constexpr std::wstring_view uncPrefix = L"\\\\?\\UNC\\";
			static constexpr std::wstring_view extendedPrefix = L"\\\\?\\";
			if (a_path.starts_with(uncPrefix))
				a_path.replace(0, uncPrefix.size(), L"\\\\");
			else if (a_path.starts_with(extendedPrefix))
				a_path.erase(0, extendedPrefix.size());
			return std::filesystem::path(a_path).lexically_normal();
		}

		std::filesystem::path FinalPath(HANDLE a_handle)
		{
			const DWORD required = GetFinalPathNameByHandleW(a_handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (required == 0)
				throw std::runtime_error(std::format("final path query failed with Win32 error {}", GetLastError()));
			std::wstring buffer(static_cast<std::size_t>(required) + 1, L'\0');
			const DWORD written = GetFinalPathNameByHandleW(
				a_handle, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (written == 0 || written >= buffer.size())
				throw std::runtime_error(std::format("final path query failed with Win32 error {}", GetLastError()));
			const auto terminator = std::ranges::find(buffer, L'\0');
			if (terminator == buffer.end())
				throw std::runtime_error("final path query returned an unterminated path");
			buffer.resize(static_cast<std::size_t>(std::distance(buffer.begin(), terminator)));
			return NormalizeFinalPath(std::move(buffer));
		}

		bool SamePath(const std::filesystem::path& a_left, const std::filesystem::path& a_right)
		{
			const auto left = a_left.lexically_normal().native();
			const auto right = a_right.lexically_normal().native();
			return _wcsicmp(left.c_str(), right.c_str()) == 0;
		}

		void RenameHandle(
			HANDLE a_handle,
			const std::filesystem::path& a_destination,
			bool a_replaceExisting)
		{
			const auto destination = a_destination.filename().native();
			if (destination.empty() || destination.find_first_of(L"\\/:") != std::wstring::npos)
				throw std::runtime_error("committed artifact rename requires a regular leaf name");
			using NtSetInformationFileFunction = NTSTATUS(NTAPI*)(
				HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
			const auto module = GetModuleHandleW(L"ntdll.dll");
			const auto setInformation = module ? reinterpret_cast<NtSetInformationFileFunction>(
													 GetProcAddress(module, "NtSetInformationFile")) :
			                                     nullptr;
			if (!setInformation)
				throw std::runtime_error("same-directory atomic artifact rename is unavailable");
			constexpr auto headerBytes = offsetof(FILE_RENAME_INFO, FileName);
			if (destination.size() > (MAXDWORD - headerBytes - sizeof(wchar_t)) / sizeof(wchar_t))
				throw std::runtime_error("committed artifact destination is too long");
			const auto nameBytes = destination.size() * sizeof(wchar_t);
			// A native leaf rename avoids reopening the write-protected parent.
			std::vector<std::byte> storage(std::max<std::size_t>(sizeof(FILE_RENAME_INFO), headerBytes + nameBytes + sizeof(wchar_t)));
			auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
			rename->ReplaceIfExists = a_replaceExisting ? TRUE : FALSE;
			rename->RootDirectory = nullptr;
			rename->FileNameLength = static_cast<DWORD>(nameBytes);
			std::memcpy(rename->FileName, destination.data(), nameBytes);
			IO_STATUS_BLOCK statusBlock{};
			const auto status = setInformation(
				a_handle, &statusBlock, rename, static_cast<ULONG>(storage.size()),
				static_cast<FILE_INFORMATION_CLASS>(10));
			if (status < 0) {
				throw std::runtime_error(std::format(
					"committed artifact rename failed with NTSTATUS {:#x}", static_cast<std::uint32_t>(status)));
			}
		}

		void DeleteHandle(HANDLE a_handle) noexcept
		{
			FILE_DISPOSITION_INFO disposition{ .DeleteFile = TRUE };
			SetFileInformationByHandle(
				a_handle, FileDispositionInfo, &disposition, sizeof(disposition));
		}

		HANDLE OpenDirectory(const std::filesystem::path& a_path, bool a_protectWrites = false)
		{
			// Attribute-only handles do not enforce the no-delete sharing lease.
			return CreateFileW(
				a_path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | (a_protectWrites ? 0u : FILE_SHARE_WRITE),
				nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		}

		HANDLE CreateDirectoryRelative(HANDLE a_parent, std::wstring_view a_name, bool a_exclusive = true)
		{
			if (a_name.empty() || a_name.size() > USHRT_MAX / sizeof(wchar_t))
				throw std::runtime_error("sequence directory name is too long");
			using NtCreateFileFunction = NTSTATUS(NTAPI*)(
				PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
				PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
			const auto module = GetModuleHandleW(L"ntdll.dll");
			const auto createFile = module ? reinterpret_cast<NtCreateFileFunction>(
												 GetProcAddress(module, "NtCreateFile")) :
			                                 nullptr;
			if (!createFile)
				throw std::runtime_error("atomic sequence directory creation is unavailable");
			UNICODE_STRING name{
				.Length = static_cast<USHORT>(a_name.size() * sizeof(wchar_t)),
				.MaximumLength = static_cast<USHORT>(a_name.size() * sizeof(wchar_t)),
				.Buffer = const_cast<PWSTR>(a_name.data()),
			};
			OBJECT_ATTRIBUTES attributes{};
			InitializeObjectAttributes(
				&attributes, &name, OBJ_CASE_INSENSITIVE, a_parent, nullptr);
			IO_STATUS_BLOCK statusBlock{};
			HANDLE directory = INVALID_HANDLE_VALUE;
			const auto status = createFile(
				&directory,
				FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
				&attributes,
				&statusBlock,
				nullptr,
				FILE_ATTRIBUTE_NORMAL,
				FILE_SHARE_READ,
				a_exclusive ? FILE_CREATE : FILE_OPEN_IF,
				FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
				nullptr,
				0);
			if (status < 0) {
				if (static_cast<std::uint32_t>(status) == 0xC0000035u)
					throw std::runtime_error("sequence directory already exists for this request identity");
				throw std::runtime_error(std::format(
					"atomic sequence directory creation failed with NTSTATUS {:#x}",
					static_cast<std::uint32_t>(status)));
			}
			return directory;
		}

		std::vector<ScopedHandle> OpenApprovedDestination(
			const std::filesystem::path& a_root, const std::filesystem::path& a_destination)
		{
			const auto root = std::filesystem::absolute(a_root).lexically_normal();
			const auto destination = std::filesystem::absolute(a_destination).lexically_normal();
			const auto relative = destination.lexically_relative(root);
			if (relative.empty() || relative.is_absolute())
				throw std::runtime_error("sequence destination is outside its approved root");
			std::vector<std::filesystem::path> components;
			for (const auto& component : relative) {
				if (component == ".")
					continue;
				if (component == ".." || component.native().find_first_of(L"\\/:") != std::wstring::npos || components.size() >= kMaximumDestinationComponents)
					throw std::runtime_error("sequence destination has unsafe or excessive directory components");
				components.push_back(component);
			}
			std::vector<ScopedHandle> chain;
			chain.reserve(components.size() + 1u);
			chain.emplace_back(OpenDirectory(root, true));
			if (chain.back().Get() == INVALID_HANDLE_VALUE)
				throw std::runtime_error(std::format("approved capture root could not be locked (Win32 error {})", GetLastError()));
			ReadIdentity(chain.back().Get(), true);
			if (!SamePath(FinalPath(chain.back().Get()), root))
				throw std::runtime_error("approved capture root changed before handle acquisition");
			auto expected = root;
			for (const auto& component : components) {
				chain.emplace_back(CreateDirectoryRelative(chain.back().Get(), component.native(), false));
				ReadIdentity(chain.back().Get(), true);
				expected /= component;
				if (!SamePath(FinalPath(chain.back().Get()), expected))
					throw std::runtime_error("sequence destination changed during approved traversal");
			}
			return chain;
		}

		std::string HashHandle(HANDLE a_handle)
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			if (const auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0); status < 0)
				throw std::runtime_error(std::format("BCryptOpenAlgorithmProvider failed ({:#x})", static_cast<std::uint32_t>(status)));
			DWORD objectBytes = 0;
			DWORD copiedBytes = 0;
			const auto propertyStatus = BCryptGetProperty(
				algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
				sizeof(objectBytes), &copiedBytes, 0);
			if (propertyStatus < 0) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
				throw std::runtime_error(std::format("BCryptGetProperty failed ({:#x})", static_cast<std::uint32_t>(propertyStatus)));
			}
			std::vector<UCHAR> hashObject(objectBytes);
			BCRYPT_HASH_HANDLE hash = nullptr;
			const auto createStatus = BCryptCreateHash(
				algorithm, &hash, hashObject.data(), static_cast<ULONG>(hashObject.size()), nullptr, 0, 0);
			if (createStatus < 0) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
				throw std::runtime_error(std::format("BCryptCreateHash failed ({:#x})", static_cast<std::uint32_t>(createStatus)));
			}

			LARGE_INTEGER beginning{};
			if (!SetFilePointerEx(a_handle, beginning, nullptr, FILE_BEGIN)) {
				BCryptDestroyHash(hash);
				BCryptCloseAlgorithmProvider(algorithm, 0);
				throw std::runtime_error(std::format("committed artifact seek failed with Win32 error {}", GetLastError()));
			}
			std::vector<UCHAR> buffer(1024 * 1024);
			NTSTATUS hashStatus = 0;
			while (hashStatus >= 0) {
				DWORD bytesRead = 0;
				if (!ReadFile(a_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
					hashStatus = static_cast<NTSTATUS>(0xC0000185L);  // STATUS_IO_DEVICE_ERROR
					break;
				}
				if (bytesRead == 0)
					break;
				hashStatus = BCryptHashData(hash, buffer.data(), bytesRead, 0);
			}
			std::array<UCHAR, 32> digest{};
			const auto finishStatus = hashStatus < 0 ? hashStatus :
			                                           BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			if (finishStatus < 0)
				throw std::runtime_error(std::format("SHA-256 hashing failed ({:#x})", static_cast<std::uint32_t>(finishStatus)));

			std::ostringstream result;
			result << std::hex << std::setfill('0');
			for (const auto value : digest)
				result << std::setw(2) << static_cast<unsigned int>(value);
			return result.str();
		}
	}

	CommittedFile::CommittedFile(void* a_handle, std::filesystem::path a_path) :
		handle(a_handle), path(std::move(a_path))
	{}

	CommittedFile CommittedFile::Open(const std::filesystem::path& a_path)
	{
		ScopedHandle file(CreateFileW(
			a_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (file.Get() == INVALID_HANDLE_VALUE)
			throw std::runtime_error(std::format("could not lock committed artifact for verification (Win32 error {})", GetLastError()));
		ReadIdentity(file.Get(), false);
		const auto openedPath = FinalPath(file.Get());
		return CommittedFile(file.Release(), openedPath);
	}

	CommittedArtifact CommittedFile::WriteAtomically(
		const std::filesystem::path& a_temporaryPath,
		const std::filesystem::path& a_destination,
		const void* a_data,
		std::size_t a_size,
		bool a_replaceExisting)
	{
		if (a_size != 0 && !a_data)
			throw std::runtime_error("committed artifact data is unavailable");
		const auto temporary = std::filesystem::absolute(a_temporaryPath).lexically_normal();
		const auto destination = std::filesystem::absolute(a_destination).lexically_normal();
		if (!SamePath(temporary.parent_path(), destination.parent_path()))
			throw std::runtime_error("committed artifact temporary file is outside its destination directory");

		ScopedHandle file(CreateFileW(
			temporary.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ,
			nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr));
		if (file.Get() == INVALID_HANDLE_VALUE)
			throw std::runtime_error(std::format(
				"committed artifact temporary file creation failed with Win32 error {}", GetLastError()));

		try {
			const auto identity = Identity(ReadIdentity(file.Get(), false));
			const auto* bytes = static_cast<const std::byte*>(a_data);
			std::size_t offset = 0;
			while (offset < a_size) {
				const auto remaining = std::min<std::size_t>(a_size - offset, MAXDWORD);
				DWORD written = 0;
				if (!WriteFile(file.Get(), bytes + offset, static_cast<DWORD>(remaining), &written, nullptr) || written == 0)
					throw std::runtime_error(std::format(
						"committed artifact write failed with Win32 error {}", GetLastError()));
				offset += written;
			}
			if (!FlushFileBuffers(file.Get()))
				throw std::runtime_error(std::format(
					"committed artifact flush failed with Win32 error {}", GetLastError()));

			RenameHandle(file.Get(), destination, a_replaceExisting);
			const auto publishedPath = FinalPath(file.Get());
			if (!SamePath(publishedPath, destination))
				throw std::runtime_error("committed artifact producer handle did not resolve to its destination path");
			ScopedHandle published(CreateFileW(
				destination.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (published.Get() == INVALID_HANDLE_VALUE)
				throw std::runtime_error(std::format("committed artifact publication verification open failed with Win32 error {}", GetLastError()));
			if (Identity(ReadIdentity(file.Get(), false)) != identity ||
				Identity(ReadIdentity(published.Get(), false)) != identity)
				throw std::runtime_error("committed artifact file identity changed during publication");
			const auto verifiedPath = FinalPath(published.Get());
			if (!SamePath(verifiedPath, publishedPath))
				throw std::runtime_error("committed artifact path changed during publication");
			CommittedFile committed(file.Release(), publishedPath);
			return committed.Describe();
		} catch (...) {
			DeleteHandle(file.Get());
			throw;
		}
	}

	CommittedFile::CommittedFile(CommittedFile&& a_other) noexcept :
		handle(std::exchange(a_other.handle, nullptr)), path(std::move(a_other.path))
	{}

	CommittedFile& CommittedFile::operator=(CommittedFile&& a_other) noexcept
	{
		if (this != &a_other) {
			Release();
			handle = std::exchange(a_other.handle, nullptr);
			path = std::move(a_other.path);
		}
		return *this;
	}

	CommittedFile::~CommittedFile()
	{
		Release();
	}

	CommittedArtifact CommittedFile::Describe() const
	{
		if (!handle)
			throw std::runtime_error("committed artifact handle is unavailable");
		const auto nativeHandle = static_cast<HANDLE>(handle);
		const auto before = ReadIdentity(nativeHandle, false);
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(nativeHandle, &size) || size.QuadPart < 0)
			throw std::runtime_error(std::format("committed artifact size query failed with Win32 error {}", GetLastError()));
		const auto identitySize =
			(static_cast<std::uint64_t>(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
		if (identitySize != static_cast<std::uint64_t>(size.QuadPart))
			throw std::runtime_error("committed artifact size metadata disagrees on the locked handle");
		const auto digest = HashHandle(nativeHandle);
		const auto after = ReadIdentity(nativeHandle, false);
		if (Identity(before) != Identity(after) ||
			before.nFileSizeHigh != after.nFileSizeHigh || before.nFileSizeLow != after.nFileSizeLow ||
			CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) != 0) {
			throw std::runtime_error("committed artifact changed while its integrity metadata was computed");
		}
		return { .bytes = static_cast<std::uint64_t>(size.QuadPart), .sha256 = digest };
	}

	void CommittedFile::Release() noexcept
	{
		if (handle)
			CloseHandle(static_cast<HANDLE>(handle));
		handle = nullptr;
	}

	DirectoryLease::DirectoryLease(
		void* a_destinationHandle,
		void* a_directoryHandle,
		std::filesystem::path a_destination,
		std::filesystem::path a_path,
		std::string a_destinationIdentity,
		std::string a_directoryIdentity) :
		destinationHandle(a_destinationHandle),
		directoryHandle(a_directoryHandle),
		destination(std::move(a_destination)),
		path(std::move(a_path)),
		destinationIdentity(std::move(a_destinationIdentity)),
		directoryIdentity(std::move(a_directoryIdentity))
	{}

	std::shared_ptr<DirectoryLease> DirectoryLease::CreateExclusive(
		const std::filesystem::path& a_destination,
		std::string_view a_requestId,
		const std::filesystem::path& a_approvedRoot)
	{
		if (a_requestId.empty() || !std::ranges::all_of(a_requestId, [](const unsigned char value) {
				return std::isalnum(value) != 0 || value == '-';
			})) {
			throw std::runtime_error("sequence request identity is not a safe directory suffix");
		}
		std::vector<ScopedHandle> ancestors;
		if (a_approvedRoot.empty()) {
			std::filesystem::create_directories(a_destination);
		}
#ifdef CSX_SCREENSHOT_STORAGE_TESTING
		if (const auto hook = g_destinationOpeningTestHook.load(std::memory_order_acquire))
			hook(a_destination);
#endif
		if (!a_approvedRoot.empty())
			ancestors = OpenApprovedDestination(a_approvedRoot, a_destination);
		ScopedHandle destinationHandle(ancestors.empty() ? OpenDirectory(a_destination) : ancestors.back().Release());
		if (!ancestors.empty())
			ancestors.pop_back();
		if (destinationHandle.Get() == INVALID_HANDLE_VALUE)
			throw std::runtime_error(std::format("sequence destination could not be locked (Win32 error {})", GetLastError()));
		const auto destinationInformation = ReadIdentity(destinationHandle.Get(), true);
		const auto destination = FinalPath(destinationHandle.Get());

		const auto leaf = L"CS_sequence_" + std::wstring(a_requestId.begin(), a_requestId.end());
		const auto directory = destination / leaf;
		ScopedHandle directoryHandle(CreateDirectoryRelative(destinationHandle.Get(), leaf));
		try {
#ifdef CSX_SCREENSHOT_STORAGE_TESTING
			if (const auto hook = g_directoryCreationTestHook.load(std::memory_order_acquire))
				hook(directory);
#endif
			const auto directoryInformation = ReadIdentity(directoryHandle.Get(), true);
			const auto openedDirectory = FinalPath(directoryHandle.Get());
			if (!SamePath(openedDirectory, directory))
				throw std::runtime_error("sequence directory identity did not match its created path");
			auto lease = std::shared_ptr<DirectoryLease>(new DirectoryLease(
				nullptr, nullptr, destination, openedDirectory,
				Identity(destinationInformation), Identity(directoryInformation)));
			lease->protectedAncestors.resize(ancestors.size(), nullptr);
			lease->destinationHandle = destinationHandle.Release();
			lease->directoryHandle = directoryHandle.Release();
			for (std::size_t index = 0u; index < ancestors.size(); ++index)
				lease->protectedAncestors[index] = ancestors[index].Release();
			return lease;
		} catch (...) {
			DeleteHandle(directoryHandle.Get());
			throw;
		}
	}

	DirectoryLease::~DirectoryLease()
	{
		if (directoryHandle)
			CloseHandle(static_cast<HANDLE>(directoryHandle));
		if (destinationHandle)
			CloseHandle(static_cast<HANDLE>(destinationHandle));
		for (auto* ancestor : protectedAncestors)
			if (ancestor)
				CloseHandle(static_cast<HANDLE>(ancestor));
	}

	void DirectoryLease::Verify() const
	{
		if (!destinationHandle || !directoryHandle)
			throw std::runtime_error("sequence directory ownership is unavailable");
		const auto destinationInformation = ReadIdentity(static_cast<HANDLE>(destinationHandle), true);
		const auto directoryInformation = ReadIdentity(static_cast<HANDLE>(directoryHandle), true);
		if (Identity(destinationInformation) != destinationIdentity ||
			Identity(directoryInformation) != directoryIdentity ||
			!SamePath(FinalPath(static_cast<HANDLE>(destinationHandle)), destination) ||
			!SamePath(FinalPath(static_cast<HANDLE>(directoryHandle)), path)) {
			throw std::runtime_error("sequence directory identity changed while capture was active");
		}
	}

	void DirectoryLease::VerifyDirectChild(const std::filesystem::path& a_path) const
	{
		Verify();
		const auto absolute = std::filesystem::absolute(a_path).lexically_normal();
		if (!SamePath(absolute.parent_path(), path))
			throw std::runtime_error("sequence output is not a direct child of its owned directory");
	}
}
