#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Util::ShaderCachePack
{
	using PackSetId = std::array<std::byte, 16>;

	enum class Lane : std::uint32_t
	{
		Optimized = 1,
		Developer = 2
	};

	// A complete, readable managed pack set is authoritative. A miss in that
	// set means the exact shader contract must be compiled; consulting a legacy
	// loose blob would bypass pack identity and compatibility validation.
	constexpr bool ShouldReadLooseBlob(bool a_diskCacheEnabled, bool a_managedPackAvailable)
	{
		return a_diskCacheEnabled && !a_managedPackAvailable;
	}

	struct Entry
	{
		std::string logicalKey;
		std::string exactKey;
		std::string metadata;
		std::vector<std::byte> bytecode;
	};

	struct Stats
	{
		bool available = false;
		std::uint64_t activeGeneration = 0;
		std::uint64_t totalBytes = 0;
		std::uint64_t committedBytes = 0;
		std::uint64_t liveBytes = 0;
		std::uint64_t supersededBytes = 0;
		std::uint64_t corruptTailBytes = 0;
		std::uint64_t recordCount = 0;
		std::uint64_t liveRecordCount = 0;

		double Fragmentation() const
		{
			return committedBytes == 0 ? 0.0 : static_cast<double>(supersededBytes) / static_cast<double>(committedBytes);
		}
	};

	/**
	 * Two-generation append-only shader pack. Both paths must already exist;
	 * runtime never creates a new MO2/VFS file. Empty shipped files may be
	 * initialized in place. A committed record is visible only when its trailer
	 * and payload hash validate. Invalid tail bytes are ignored and truncated
	 * before the next append.
	 */
	class Store
	{
	public:
		Store(
			std::filesystem::path a_pathA,
			std::filesystem::path a_pathB,
			Lane a_lane,
			PackSetId a_packSetId = {});
		~Store();

		Store(const Store&) = delete;
		Store& operator=(const Store&) = delete;

		bool Open(std::string* a_error = nullptr);
		std::optional<Entry> Find(std::string_view a_exactKey, std::string* a_error = nullptr) const;
		bool Append(const Entry& a_entry, std::string* a_error = nullptr);
		/** Durably commits all records appended since the previous checkpoint. */
		bool Checkpoint(std::string* a_error = nullptr);
		Stats GetStats() const;
		bool ShouldCompact(double a_minimumFragmentation = 0.30, std::uint64_t a_minimumSupersededBytes = 32ull * 1024ull * 1024ull) const;
		bool Compact(std::string* a_error = nullptr);
		bool Reset(std::string* a_error = nullptr);

	private:
		struct RecordLocation
		{
			std::filesystem::path path;
			std::uint64_t offset = 0;
			std::uint64_t totalSize = 0;
			std::uint64_t sequence = 0;
			std::uint64_t generation = 0;
			std::string logicalKey;
			std::string exactKey;
			std::string metadata;
			std::uint64_t bytecodeOffset = 0;
			std::uint64_t bytecodeSize = 0;
		};

		struct ScannedFile
		{
			std::filesystem::path path;
			bool exists = false;
			bool valid = false;
			std::uint64_t generation = 0;
			std::uint64_t fileSize = 0;
			std::uint64_t validSize = 0;
			std::uint64_t nextSequence = 1;
			std::string diagnostic;
			std::vector<RecordLocation> records;
		};

		mutable std::shared_mutex mutex;
		std::filesystem::path pathA;
		std::filesystem::path pathB;
		Lane lane;
		PackSetId packSetId{};
		bool opened = false;
#ifdef _WIN32
		void* leaseHandle = nullptr;
		bool leaseOwned = false;
#endif
		ScannedFile active;
		ScannedFile fallback;
		std::unordered_map<std::string, RecordLocation> exactIndex;
		std::unordered_map<std::string, RecordLocation> liveByLogical;
		std::unordered_map<std::string, RecordLocation> activeLiveByLogical;
		Stats stats;

		bool OpenLocked(std::string* a_error);
		bool AcquireWriterLease(std::string* a_error);
		void ReleaseWriterLease() noexcept;
		bool Scan(const std::filesystem::path& a_path, ScannedFile& a_output, std::string* a_error) const;
		bool InitializeEmpty(ScannedFile& a_file, std::uint64_t a_generation, std::string* a_error) const;
		bool AppendLocked(ScannedFile& a_file, const Entry& a_entry, std::uint64_t a_sequence, bool a_checkpoint, std::string* a_error) const;
		std::optional<Entry> Read(const RecordLocation& a_location, std::string* a_error) const;
		void RebuildIndexes();
	};
}
