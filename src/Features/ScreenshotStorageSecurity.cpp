#include "Features/ScreenshotStorageSecurity.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <format>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CSX::ScreenshotStorage
{
	namespace
	{
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

		std::filesystem::path FinalPath(HANDLE a_handle)
		{
			const DWORD required = GetFinalPathNameByHandleW(a_handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (required == 0)
				throw std::runtime_error(std::format("final path query failed with Win32 error {}", GetLastError()));
			std::wstring buffer(required, L'\0');
			const DWORD written = GetFinalPathNameByHandleW(
				a_handle, buffer.data(), required, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (written == 0 || written >= required)
				throw std::runtime_error(std::format("final path query failed with Win32 error {}", GetLastError()));
			buffer.resize(written);
			static constexpr std::wstring_view extendedPrefix = L"\\\\?\\";
			if (buffer.starts_with(extendedPrefix))
				buffer.erase(0, extendedPrefix.size());
			return std::filesystem::path(buffer).lexically_normal();
		}

		bool SamePath(const std::filesystem::path& a_left, const std::filesystem::path& a_right)
		{
			const auto left = a_left.lexically_normal().native();
			const auto right = a_right.lexically_normal().native();
			return _wcsicmp(left.c_str(), right.c_str()) == 0;
		}

		HANDLE OpenDirectory(const std::filesystem::path& a_path)
		{
			return CreateFileW(
				a_path.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
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
		const ScopedHandle file(CreateFileW(
			a_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (file.Get() == INVALID_HANDLE_VALUE)
			throw std::runtime_error(std::format("could not lock committed artifact for verification (Win32 error {})", GetLastError()));
		ReadIdentity(file.Get(), false);
		std::error_code canonicalError;
		const auto expected = std::filesystem::weakly_canonical(a_path, canonicalError);
		if (canonicalError || !SamePath(FinalPath(file.Get()), expected))
			throw std::runtime_error("committed artifact path changed while it was opened");
		return CommittedFile(file.Release(), expected);
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
		std::string_view a_requestId)
	{
		if (a_requestId.empty() || !std::ranges::all_of(a_requestId, [](const unsigned char value) {
				return std::isalnum(value) != 0 || value == '-';
			})) {
			throw std::runtime_error("sequence request identity is not a safe directory suffix");
		}
		std::filesystem::create_directories(a_destination);
		std::error_code canonicalError;
		const auto destination = std::filesystem::weakly_canonical(a_destination, canonicalError);
		if (canonicalError || !destination.is_absolute())
			throw std::runtime_error("sequence destination could not be resolved to an absolute directory");

		ScopedHandle destinationHandle(OpenDirectory(destination));
		if (destinationHandle.Get() == INVALID_HANDLE_VALUE)
			throw std::runtime_error(std::format("sequence destination could not be locked (Win32 error {})", GetLastError()));
		const auto destinationInformation = ReadIdentity(destinationHandle.Get(), true);
		if (!SamePath(FinalPath(destinationHandle.Get()), destination))
			throw std::runtime_error("sequence destination changed while it was opened");

		const auto directory = destination / ("CS_sequence_" + std::string(a_requestId));
		if (!CreateDirectoryW(directory.c_str(), nullptr)) {
			const auto error = GetLastError();
			if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
				throw std::runtime_error("sequence directory already exists for this request identity");
			throw std::runtime_error(std::format("sequence directory creation failed with Win32 error {}", error));
		}

		ScopedHandle directoryHandle(OpenDirectory(directory));
		if (directoryHandle.Get() == INVALID_HANDLE_VALUE) {
			const auto error = GetLastError();
			RemoveDirectoryW(directory.c_str());
			throw std::runtime_error(std::format("sequence directory could not be locked (Win32 error {})", error));
		}
		try {
			const auto directoryInformation = ReadIdentity(directoryHandle.Get(), true);
			if (!SamePath(FinalPath(directoryHandle.Get()), directory))
				throw std::runtime_error("sequence directory changed between creation and ownership");
			return std::shared_ptr<DirectoryLease>(new DirectoryLease(
				destinationHandle.Release(), directoryHandle.Release(), destination, directory,
				Identity(destinationInformation), Identity(directoryInformation)));
		} catch (...) {
			CloseHandle(directoryHandle.Release());
			RemoveDirectoryW(directory.c_str());
			throw;
		}
	}

	DirectoryLease::~DirectoryLease()
	{
		if (directoryHandle)
			CloseHandle(static_cast<HANDLE>(directoryHandle));
		if (destinationHandle)
			CloseHandle(static_cast<HANDLE>(destinationHandle));
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
