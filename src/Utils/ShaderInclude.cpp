#include "ShaderInclude.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <spdlog/spdlog.h>
#include <system_error>
#include <unordered_set>
#include <winrt/base.h>

namespace Util::ShaderInclude
{
	StartupCache::StartupCache(size_t bytes, size_t entries) :
		maximumBytes((std::min)(bytes, kMaximumBytes)), maximumEntries((std::min)(entries, kMaximumEntries)),
		enabled(bytes != 0 && entries != 0) {}

	void StartupCache::Disable()
	{
		std::lock_guard lock(mutex);
		enabled.store(false, std::memory_order_release);
		decltype(snapshots){}.swap(snapshots);
		retainedBytes = 0;
	}

	StartupCache::Usage StartupCache::GetUsage() const
	{
		std::lock_guard lock(mutex);
		return { retainedBytes, snapshots.size() };
	}

	bool Read(const std::filesystem::path& path, File& contents, ReadError& error,
		StartupCache* cache, bool* reused) noexcept
	{
		contents = {};
		error = {};
		if (reused)
			*reused = false;
		if (cache && !cache->enabled.load(std::memory_order_acquire))
			cache = nullptr;
		// A reuse candidate must exclude concurrent writers and replacement until copied.
		constexpr DWORD sharedAccess = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
		winrt::file_handle handle{ CreateFileW(path.c_str(), GENERIC_READ,
			cache ? FILE_SHARE_READ : sharedAccess, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
		if (!handle && cache) {
			cache = nullptr;
			handle.attach(CreateFileW(path.c_str(), GENERIC_READ, sharedAccess, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		}
		if (!handle) {
			error = { "open", GetLastError() };
			return false;
		}

		LARGE_INTEGER size{};
		BY_HANDLE_FILE_INFORMATION identity{};
		if (cache && GetFileInformationByHandle(handle.get(), &identity)) {
			size.QuadPart = (static_cast<uint64_t>(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow;
		} else if (!GetFileSizeEx(handle.get(), &size)) {
			error = { "size", GetLastError() };
			return false;
		} else {
			cache = nullptr;
		}
		if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > (std::numeric_limits<UINT>::max)()) {
			error = { "size", ERROR_FILE_TOO_LARGE, static_cast<uint64_t>(size.QuadPart) };
			return false;
		}
		StartupCache::FileStamp stamp{};
		if (cache) {
			FILE_BASIC_INFO basic{};
			if ((identity.nFileIndexHigh == 0 && identity.nFileIndexLow == 0) ||
				(identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
				!GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &basic, sizeof(basic)) ||
				basic.ChangeTime.QuadPart == 0) {
				cache = nullptr;
			} else {
				stamp = { static_cast<uint64_t>(size.QuadPart), static_cast<uint64_t>(basic.LastWriteTime.QuadPart),
					static_cast<uint64_t>(basic.ChangeTime.QuadPart), identity.dwVolumeSerialNumber,
					(static_cast<uint64_t>(identity.nFileIndexHigh) << 32) | identity.nFileIndexLow };
			}
		}

		File result;
		result.size = static_cast<UINT>(size.QuadPart);
		try {
			result.data = std::make_unique_for_overwrite<char[]>(std::max<size_t>(result.size, 1));
		} catch (const std::bad_alloc&) {
			error = { "allocate", ERROR_NOT_ENOUGH_MEMORY, result.size };
			return false;
		}
		if (cache) {
			std::shared_ptr<const char[]> snapshot;
			{
				std::lock_guard lock(cache->mutex);
				if (auto found = cache->snapshots.find(path); found != cache->snapshots.end()) {
					if (found->second.stamp == stamp) {
						snapshot = found->second.bytes;
					} else {
						cache->retainedBytes -= found->second.charge;
						cache->snapshots.erase(found);
					}
				}
			}
			if (snapshot) {
				// Readers own their snapshot while copying, so unrelated includes do not serialize.
				std::memcpy(result.data.get(), snapshot.get(), result.size);
				contents = std::move(result);
				if (reused)
					*reused = true;
				return true;
			}
		}

		UINT totalRead = 0;
		while (totalRead < result.size) {
			DWORD bytesRead = 0;
			if (!ReadFile(handle.get(), result.data.get() + totalRead, result.size - totalRead, &bytesRead, nullptr)) {
				error = { "read", GetLastError(), result.size, totalRead };
				return false;
			}
			if (bytesRead == 0) {
				error = { "read", ERROR_HANDLE_EOF, result.size, totalRead };
				return false;
			}
			totalRead += bytesRead;
		}
		if (cache) {
			try {
				std::lock_guard lock(cache->mutex);
				const size_t charge = std::max<size_t>(result.size, 1) + sizeof(decltype(cache->snapshots)::value_type) +
				                      (path.native().size() + 1) * sizeof(std::filesystem::path::value_type);
				if (cache->enabled.load(std::memory_order_acquire) &&
					!cache->snapshots.contains(path) && cache->snapshots.size() < cache->maximumEntries &&
					charge <= cache->maximumBytes - cache->retainedBytes) {
					auto copy = std::make_shared_for_overwrite<char[]>(std::max<size_t>(result.size, 1));
					std::memcpy(copy.get(), result.data.get(), result.size);
					cache->snapshots.emplace(path, StartupCache::Snapshot{ stamp, std::move(copy), charge });
					cache->retainedBytes += charge;
				}
			} catch (const std::bad_alloc&) {
				cache->Disable();
				OutputDebugStringA("[ShaderInclude] Startup reuse disabled after allocation failure.\n");
			}
		}
		contents = std::move(result);
		return true;
	}

	void Report(const std::filesystem::path& source, const std::filesystem::path& attemptedPath, const ReadError& error) noexcept
	{
		try {
			constexpr size_t kMaxReports = 128;
			static std::mutex mutex;
			static std::unordered_set<std::string> reported;
			const auto key = std::format("{}|{}|{}|{}", source.string(), attemptedPath.string(), error.operation, error.code);
			{
				std::lock_guard lock(mutex);
				if (reported.size() >= kMaxReports || !reported.insert(key).second)
					return;
			}
			spdlog::error("[ShaderInclude] source='{}' path='{}' operation={} win32_error={} ({}) expected_bytes={} read_bytes={}",
				source.string(), attemptedPath.string(), error.operation, error.code,
				std::system_category().message(static_cast<int>(error.code)), error.expectedBytes, error.readBytes);
		} catch (...) {
			OutputDebugStringA("[ShaderInclude] Unable to format include failure diagnostics.\n");
		}
	}
}

namespace Util
{
	HRESULT CustomInclude::Open([[maybe_unused]] D3D_INCLUDE_TYPE type, LPCSTR filename,
		[[maybe_unused]] LPCVOID parent, LPCVOID* data, UINT* size) noexcept
	{
		if (data)
			*data = nullptr;
		if (size)
			*size = 0;
		if (!data || !size || !filename)
			return E_INVALIDARG;
		std::filesystem::path path;
		try {
			path = includeRoot / filename;
			ShaderInclude::File contents;
			ShaderInclude::ReadError error;
			if (!ShaderInclude::Read(path, contents, error)) {
				ShaderInclude::Report(sourcePath, path, error);
				return HRESULT_FROM_WIN32(error.code);
			}
			*size = contents.size;
			*data = contents.data.release();
			return S_OK;
		} catch (const std::bad_alloc&) {
			ShaderInclude::Report(sourcePath, path, { "prepare_include", ERROR_NOT_ENOUGH_MEMORY });
			return E_OUTOFMEMORY;
		} catch (...) {
			ShaderInclude::Report(sourcePath, path, { "prepare_include", ERROR_UNHANDLED_EXCEPTION });
			return E_FAIL;
		}
	}

	HRESULT CustomInclude::Close(LPCVOID data) noexcept
	{
		delete[] static_cast<const char*>(data);
		return S_OK;
	}
}
