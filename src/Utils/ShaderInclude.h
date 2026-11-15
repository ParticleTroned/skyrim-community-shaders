#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <d3dcompiler.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Util::ShaderInclude
{
	struct File
	{
		std::unique_ptr<char[]> data;
		UINT size = 0;
	};

	struct ReadError
	{
		const char* operation = "";
		DWORD code = ERROR_SUCCESS;
		uint64_t expectedBytes = 0;
		uint64_t readBytes = 0;
	};

	/** Bounded include bytes retained only during blocking startup compilation. */
	class StartupCache
	{
	public:
		static constexpr size_t kMaximumBytes = 16 * 1024 * 1024;
		static constexpr size_t kMaximumEntries = 1024;
		explicit StartupCache(size_t bytes = kMaximumBytes, size_t entries = kMaximumEntries);
		/** Permanently ends reuse and releases retained bytes, including during active reads. */
		void Disable();
		struct Usage
		{
			size_t bytes;
			size_t entries;
		};
		/** Returns retained storage accounting, including path and entry payloads. */
		Usage GetUsage() const;

	private:
		friend bool Read(const std::filesystem::path&, File&, ReadError&, StartupCache*, bool*) noexcept;
		struct FileStamp
		{
			uint64_t size;
			uint64_t lastWriteTime;
			uint64_t changeTime;
			uint64_t volume;
			uint64_t fileId;
			bool operator==(const FileStamp&) const = default;
		};
		struct Snapshot
		{
			FileStamp stamp;
			std::shared_ptr<const char[]> bytes;
			size_t charge;
		};
		const size_t maximumBytes;
		const size_t maximumEntries;
		std::atomic<bool> enabled = true;
		mutable std::mutex mutex;
		std::unordered_map<std::filesystem::path, Snapshot> snapshots;
		size_t retainedBytes = 0;
	};

	/** Reads complete UINT-sized bytes; optional startup reuse never bypasses file access checks. */
	bool Read(const std::filesystem::path& path, File& contents, ReadError& error,
		StartupCache* cache = nullptr, bool* reused = nullptr) noexcept;
	/** Emits bounded, deduplicated diagnostics without throwing into the compiler. */
	void Report(const std::filesystem::path& source, const std::filesystem::path& attemptedPath, const ReadError& error) noexcept;
}

namespace Util
{
	/** Resolves runtime includes from the shader root, independently of the including file. */
	struct CustomInclude : public ID3DInclude
	{
		explicit CustomInclude(const std::filesystem::path& root = L"Data\\Shaders", const std::filesystem::path& source = {}) :
			includeRoot(root), sourcePath(source) {}

		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR filename, LPCVOID parent, LPCVOID* data, UINT* size) noexcept override;
		HRESULT Close(LPCVOID data) noexcept override;

	private:
		const std::filesystem::path includeRoot;
		const std::filesystem::path sourcePath;
	};
}
