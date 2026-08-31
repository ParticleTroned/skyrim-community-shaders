#include "Utils/ShaderCachePack.h"

#include "Utils/CryptoHash.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
	constexpr std::array<char, 8> kFileMagic{ 'C', 'S', 'X', 'S', 'P', 'K', '1', '\0' };
	constexpr std::array<char, 8> kRecordMagic{ 'C', 'S', 'X', 'R', 'E', 'C', '1', '\0' };
	constexpr std::array<char, 8> kCommitMagic{ 'C', 'S', 'X', 'C', 'M', 'T', '1', '\0' };
	constexpr std::uint32_t kFormatVersion = 1;
	constexpr std::uint64_t kMaximumRecordSize = 512ull * 1024ull * 1024ull;

#pragma pack(push, 1)
	struct FileHeader
	{
		char magic[8];
		std::uint32_t version;
		std::uint32_t lane;
		std::uint64_t generation;
		std::uint64_t reserved[3];
		std::byte hash[32];
	};

	struct RecordHeader
	{
		char magic[8];
		std::uint32_t version;
		std::uint32_t reserved;
		std::uint64_t sequence;
		std::uint32_t logicalSize;
		std::uint32_t exactSize;
		std::uint32_t metadataSize;
		std::uint32_t reserved2;
		std::uint64_t bytecodeSize;
		std::byte payloadHash[32];
	};

	struct CommitTrailer
	{
		char magic[8];
		std::uint64_t totalSize;
		std::byte payloadHash[32];
	};
#pragma pack(pop)

	static_assert(sizeof(FileHeader) == 80);
	static_assert(sizeof(RecordHeader) == 80);
	static_assert(sizeof(CommitTrailer) == 48);

	template <class T>
	bool ReadAt(std::ifstream& a_stream, std::uint64_t a_offset, T& a_output)
	{
		a_stream.seekg(static_cast<std::streamoff>(a_offset));
		a_stream.read(reinterpret_cast<char*>(&a_output), sizeof(T));
		return a_stream.good();
	}

	bool ReadBytes(std::ifstream& a_stream, std::uint64_t a_offset, void* a_output, std::size_t a_size)
	{
		a_stream.seekg(static_cast<std::streamoff>(a_offset));
		a_stream.read(static_cast<char*>(a_output), static_cast<std::streamsize>(a_size));
		return a_stream.good();
	}

	Util::CryptoHash::Sha256 HashPayload(
		std::string_view a_logical,
		std::string_view a_exact,
		std::string_view a_metadata,
		std::span<const std::byte> a_bytecode)
	{
		std::string payload;
		payload.reserve(a_logical.size() + a_exact.size() + a_metadata.size() + a_bytecode.size());
		payload.append(a_logical);
		payload.append(a_exact);
		payload.append(a_metadata);
		payload.append(reinterpret_cast<const char*>(a_bytecode.data()), a_bytecode.size());
		return Util::CryptoHash::Sha256Bytes(payload);
	}

	void SetError(std::string* a_error, std::string a_value)
	{
		if (a_error)
			*a_error = std::move(a_value);
	}

	bool DurableFlush(const std::filesystem::path& a_path)
	{
#ifdef _WIN32
		const HANDLE file = CreateFileW(
			a_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		const bool flushed = FlushFileBuffers(file) != FALSE;
		CloseHandle(file);
		return flushed;
#else
		(void)a_path;
		return true;
#endif
	}
}

namespace Util::ShaderCachePack
{
	Store::Store(std::filesystem::path a_pathA, std::filesystem::path a_pathB, Lane a_lane) :
		pathA(std::move(a_pathA)), pathB(std::move(a_pathB)), lane(a_lane)
	{}

	bool Store::Scan(const std::filesystem::path& a_path, ScannedFile& a_output, std::string* a_error) const
	{
		(void)a_error;
		// a_path may refer to a_output.path (InitializeEmpty does exactly that).
		// Preserve the value before clearing the output object.
		const auto stablePath = a_path;
		a_output = {};
		a_output.path = stablePath;
		std::error_code error;
		a_output.exists = std::filesystem::exists(stablePath, error);
		if (error || !a_output.exists)
			return true;
		a_output.fileSize = std::filesystem::file_size(stablePath, error);
		if (error || a_output.fileSize == 0)
			return true;
		if (a_output.fileSize < sizeof(FileHeader))
			return true;

		std::ifstream stream(stablePath, std::ios::binary);
		FileHeader file{};
		if (!ReadAt(stream, 0, file))
			return true;
		const auto expectedHeaderHash = CryptoHash::Sha256Bytes(std::span<const std::byte>{
			reinterpret_cast<const std::byte*>(&file), offsetof(FileHeader, hash) });
		if (std::memcmp(file.magic, kFileMagic.data(), kFileMagic.size()) != 0 ||
			file.version != kFormatVersion || file.lane != static_cast<std::uint32_t>(lane) ||
			std::memcmp(file.hash, expectedHeaderHash.data(), expectedHeaderHash.size()) != 0)
			return true;

		a_output.valid = true;
		a_output.generation = file.generation;
		a_output.validSize = sizeof(FileHeader);
		std::uint64_t offset = sizeof(FileHeader);
		while (offset + sizeof(RecordHeader) + sizeof(CommitTrailer) <= a_output.fileSize) {
			RecordHeader header{};
			if (!ReadAt(stream, offset, header) || std::memcmp(header.magic, kRecordMagic.data(), kRecordMagic.size()) != 0 || header.version != kFormatVersion)
				break;
			const std::uint64_t payloadSize = static_cast<std::uint64_t>(header.logicalSize) + header.exactSize + header.metadataSize + header.bytecodeSize;
			const std::uint64_t totalSize = sizeof(RecordHeader) + payloadSize + sizeof(CommitTrailer);
			if (payloadSize > kMaximumRecordSize || totalSize > a_output.fileSize - offset)
				break;
			std::vector<std::byte> payload(static_cast<std::size_t>(payloadSize));
			if (!ReadBytes(stream, offset + sizeof(RecordHeader), payload.data(), payload.size()))
				break;
			CommitTrailer trailer{};
			if (!ReadAt(stream, offset + sizeof(RecordHeader) + payloadSize, trailer))
				break;
			const auto hash = CryptoHash::Sha256Bytes(payload);
			if (std::memcmp(trailer.magic, kCommitMagic.data(), kCommitMagic.size()) != 0 || trailer.totalSize != totalSize ||
				std::memcmp(header.payloadHash, hash.data(), hash.size()) != 0 ||
				std::memcmp(trailer.payloadHash, hash.data(), hash.size()) != 0)
				break;

			const auto* chars = reinterpret_cast<const char*>(payload.data());
			RecordLocation location{
				.path = stablePath,
				.offset = offset,
				.totalSize = totalSize,
				.sequence = header.sequence,
				.generation = file.generation,
				.logicalKey = std::string(chars, header.logicalSize),
				.exactKey = std::string(chars + header.logicalSize, header.exactSize),
				.metadata = std::string(chars + header.logicalSize + header.exactSize, header.metadataSize),
				.bytecodeOffset = offset + sizeof(RecordHeader) + header.logicalSize + header.exactSize + header.metadataSize,
				.bytecodeSize = header.bytecodeSize,
			};
			a_output.records.push_back(std::move(location));
			a_output.nextSequence = (std::max)(a_output.nextSequence, header.sequence + 1);
			offset += totalSize;
			a_output.validSize = offset;
		}
		return true;
	}

	bool Store::InitializeEmpty(ScannedFile& a_file, std::uint64_t a_generation, std::string* a_error) const
	{
		std::error_code existenceError;
		if (!a_file.exists || !std::filesystem::is_regular_file(a_file.path, existenceError) || existenceError) {
			SetError(a_error, "pack file is absent; runtime will not create files outside the shipped managed cache mod");
			return false;
		}
		FileHeader header{};
		std::memcpy(header.magic, kFileMagic.data(), kFileMagic.size());
		header.version = kFormatVersion;
		header.lane = static_cast<std::uint32_t>(lane);
		header.generation = a_generation;
		const auto hash = CryptoHash::Sha256Bytes(std::span<const std::byte>{
			reinterpret_cast<const std::byte*>(&header), offsetof(FileHeader, hash) });
		std::memcpy(header.hash, hash.data(), hash.size());
		{
			std::ofstream stream(a_file.path, std::ios::binary | std::ios::trunc);
			stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
			stream.flush();
			if (!stream.good()) {
				SetError(a_error, "failed to initialize existing shader pack file");
				return false;
			}
		}
		if (!DurableFlush(a_file.path)) {
			SetError(a_error, "failed to durably flush initialized shader pack file");
			return false;
		}
		return Scan(a_file.path, a_file, a_error);
	}

	bool Store::Open(std::string* a_error)
	{
		try {
			std::unique_lock lock(mutex);
			return OpenLocked(a_error);
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			opened = false;
			return false;
		} catch (...) {
			SetError(a_error, "unknown shader pack open failure");
			opened = false;
			return false;
		}
	}

	bool Store::OpenLocked(std::string* a_error)
	{
		ScannedFile a;
		ScannedFile b;
		Scan(pathA, a, a_error);
		Scan(pathB, b, a_error);
		if (!a.valid && !b.valid) {
			ScannedFile* empty = a.exists && a.fileSize == 0 ? &a : (b.exists && b.fileSize == 0 ? &b : nullptr);
			if (!empty || !InitializeEmpty(*empty, 1, a_error)) {
				opened = false;
				return false;
			}
		}
		if (b.valid && (!a.valid || b.generation > a.generation)) {
			active = std::move(b);
			fallback = std::move(a);
		} else {
			active = std::move(a);
			fallback = std::move(b);
		}
		opened = active.valid;
		RebuildIndexes();
		return opened;
	}

	void Store::RebuildIndexes()
	{
		exactIndex.clear();
		liveByLogical.clear();
		activeLiveByLogical.clear();
		stats = { .available = opened, .activeGeneration = active.generation };
		for (const auto* file : { &fallback, &active }) {
			stats.totalBytes += file->fileSize;
			stats.corruptTailBytes += file->fileSize - file->validSize;
			for (const auto& record : file->records) {
				exactIndex.insert_or_assign(record.exactKey, record);
				const auto found = liveByLogical.find(record.logicalKey);
				if (found == liveByLogical.end() || std::pair{ record.generation, record.sequence } >= std::pair{ found->second.generation, found->second.sequence })
					liveByLogical.insert_or_assign(record.logicalKey, record);
				if (file == &active) {
					stats.committedBytes += record.totalSize;
					++stats.recordCount;
					activeLiveByLogical.insert_or_assign(record.logicalKey, record);
				}
			}
		}
		for (const auto& [_, record] : activeLiveByLogical)
			stats.liveBytes += record.totalSize;
		stats.liveRecordCount = activeLiveByLogical.size();
		stats.supersededBytes = stats.committedBytes > stats.liveBytes ? stats.committedBytes - stats.liveBytes : 0;
	}

	std::optional<Entry> Store::Read(const RecordLocation& a_location, std::string* a_error) const
	{
		std::ifstream stream(a_location.path, std::ios::binary);
		Entry result{ a_location.logicalKey, a_location.exactKey, a_location.metadata, {} };
		result.bytecode.resize(static_cast<std::size_t>(a_location.bytecodeSize));
		if (!ReadBytes(stream, a_location.bytecodeOffset, result.bytecode.data(), result.bytecode.size())) {
			SetError(a_error, "failed to read committed shader pack record");
			return std::nullopt;
		}
		return result;
	}

	std::optional<Entry> Store::Find(std::string_view a_exactKey, std::string* a_error) const
	{
		try {
			std::shared_lock lock(mutex);
			if (!opened)
				return std::nullopt;
			const auto found = exactIndex.find(std::string(a_exactKey));
			return found == exactIndex.end() ? std::nullopt : Read(found->second, a_error);
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			return std::nullopt;
		} catch (...) {
			SetError(a_error, "unknown shader pack read failure");
			return std::nullopt;
		}
	}

	bool Store::AppendLocked(
		ScannedFile& a_file,
		const Entry& a_entry,
		std::uint64_t a_sequence,
		bool a_checkpoint,
		std::string* a_error) const
	{
		if (a_entry.logicalKey.empty() || a_entry.exactKey.empty() || a_entry.bytecode.empty()) {
			SetError(a_error, "shader pack records require logical key, exact key, and bytecode");
			return false;
		}
		if (a_entry.logicalKey.size() > (std::numeric_limits<std::uint32_t>::max)() ||
			a_entry.exactKey.size() > (std::numeric_limits<std::uint32_t>::max)() ||
			a_entry.metadata.size() > (std::numeric_limits<std::uint32_t>::max)()) {
			SetError(a_error, "shader pack record metadata exceeds format limits");
			return false;
		}
		const auto hash = HashPayload(a_entry.logicalKey, a_entry.exactKey, a_entry.metadata, a_entry.bytecode);
		RecordHeader header{};
		std::memcpy(header.magic, kRecordMagic.data(), kRecordMagic.size());
		header.version = kFormatVersion;
		header.sequence = a_sequence;
		header.logicalSize = static_cast<std::uint32_t>(a_entry.logicalKey.size());
		header.exactSize = static_cast<std::uint32_t>(a_entry.exactKey.size());
		header.metadataSize = static_cast<std::uint32_t>(a_entry.metadata.size());
		header.bytecodeSize = a_entry.bytecode.size();
		std::memcpy(header.payloadHash, hash.data(), hash.size());
		const std::uint64_t totalSize = sizeof(header) + a_entry.logicalKey.size() + a_entry.exactKey.size() + a_entry.metadata.size() + a_entry.bytecode.size() + sizeof(CommitTrailer);
		CommitTrailer trailer{};
		std::memcpy(trailer.magic, kCommitMagic.data(), kCommitMagic.size());
		trailer.totalSize = totalSize;
		std::memcpy(trailer.payloadHash, hash.data(), hash.size());

		std::filesystem::resize_file(a_file.path, a_file.validSize);
		{
			std::ofstream stream(a_file.path, std::ios::binary | std::ios::app);
			stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
			stream.write(a_entry.logicalKey.data(), a_entry.logicalKey.size());
			stream.write(a_entry.exactKey.data(), a_entry.exactKey.size());
			stream.write(a_entry.metadata.data(), a_entry.metadata.size());
			stream.write(reinterpret_cast<const char*>(a_entry.bytecode.data()), static_cast<std::streamsize>(a_entry.bytecode.size()));
			stream.write(reinterpret_cast<const char*>(&trailer), sizeof(trailer));
			stream.flush();
			if (!stream.good()) {
				SetError(a_error, "failed to append committed shader pack record");
				return false;
			}
		}
		if (a_checkpoint && !DurableFlush(a_file.path)) {
			SetError(a_error, "failed to durably flush committed shader pack record");
			return false;
		}
		return true;
	}

	bool Store::Append(const Entry& a_entry, std::string* a_error)
	{
		try {
			std::unique_lock lock(mutex);
			if (!opened && !OpenLocked(a_error))
				return false;
			const auto offset = active.validSize;
			const auto sequence = active.nextSequence;
			const auto removedTailBytes = active.fileSize - active.validSize;
			if (!AppendLocked(active, a_entry, sequence, false, a_error))
				return false;
			const std::uint64_t totalSize = sizeof(RecordHeader) + a_entry.logicalKey.size() + a_entry.exactKey.size() +
				a_entry.metadata.size() + a_entry.bytecode.size() + sizeof(CommitTrailer);
			RecordLocation location{
			.path = active.path,
			.offset = offset,
			.totalSize = totalSize,
			.sequence = sequence,
			.generation = active.generation,
			.logicalKey = a_entry.logicalKey,
			.exactKey = a_entry.exactKey,
			.metadata = a_entry.metadata,
			.bytecodeOffset = offset + sizeof(RecordHeader) + a_entry.logicalKey.size() + a_entry.exactKey.size() + a_entry.metadata.size(),
			.bytecodeSize = a_entry.bytecode.size(),
			};
			active.records.push_back(location);
			active.validSize += totalSize;
			active.fileSize = active.validSize;
			++active.nextSequence;

			stats.totalBytes = stats.totalBytes >= removedTailBytes ? stats.totalBytes - removedTailBytes + totalSize : totalSize;
			stats.corruptTailBytes = stats.corruptTailBytes >= removedTailBytes ? stats.corruptTailBytes - removedTailBytes : 0;
			stats.committedBytes += totalSize;
			++stats.recordCount;
			if (const auto previous = activeLiveByLogical.find(location.logicalKey); previous != activeLiveByLogical.end())
				stats.liveBytes -= previous->second.totalSize;
			activeLiveByLogical.insert_or_assign(location.logicalKey, location);
			stats.liveBytes += location.totalSize;
			stats.liveRecordCount = activeLiveByLogical.size();
			stats.supersededBytes = stats.committedBytes > stats.liveBytes ? stats.committedBytes - stats.liveBytes : 0;
			exactIndex.insert_or_assign(location.exactKey, location);
			const auto live = liveByLogical.find(location.logicalKey);
			if (live == liveByLogical.end() || std::pair{ location.generation, location.sequence } >=
				std::pair{ live->second.generation, live->second.sequence })
				liveByLogical.insert_or_assign(location.logicalKey, location);
			return true;
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			return false;
		} catch (...) {
			SetError(a_error, "unknown shader pack append failure");
			return false;
		}
	}

	bool Store::Checkpoint(std::string* a_error)
	{
		try {
			std::shared_lock lock(mutex);
			if (!opened) {
				SetError(a_error, "shader pack is not open");
				return false;
			}
			if (!DurableFlush(active.path)) {
				SetError(a_error, "failed to durably checkpoint shader pack records");
				return false;
			}
			return true;
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			return false;
		} catch (...) {
			SetError(a_error, "unknown shader pack checkpoint failure");
			return false;
		}
	}

	Stats Store::GetStats() const
	{
		std::shared_lock lock(mutex);
		return stats;
	}

	bool Store::ShouldCompact(double a_minimumFragmentation, std::uint64_t a_minimumSupersededBytes) const
	{
		std::shared_lock lock(mutex);
		return opened && fallback.exists && stats.supersededBytes >= a_minimumSupersededBytes && stats.Fragmentation() >= a_minimumFragmentation;
	}

	bool Store::Compact(std::string* a_error)
	{
		try {
		std::unique_lock lock(mutex);
		if (!opened || !fallback.exists) {
			SetError(a_error, "both fixed A/B files are required for compaction");
			return false;
		}
		std::vector<Entry> live;
		live.reserve(liveByLogical.size());
		std::vector<RecordLocation> ordered;
		ordered.reserve(liveByLogical.size());
		for (const auto& [_, record] : liveByLogical)
			ordered.push_back(record);
		std::ranges::sort(ordered, {}, &RecordLocation::logicalKey);
		for (const auto& record : ordered) {
			auto entry = Read(record, a_error);
			if (!entry)
				return false;
			live.push_back(std::move(*entry));
		}

		ScannedFile target = fallback;
		if (!InitializeEmpty(target, active.generation + 1, a_error))
			return false;
		std::uint64_t sequence = 1;
		for (const auto& entry : live) {
			if (!AppendLocked(target, entry, sequence++, false, a_error))
				return false;
			target.validSize = std::filesystem::file_size(target.path);
		}
		if (!DurableFlush(target.path)) {
			SetError(a_error, "failed to durably checkpoint compacted shader pack");
			return false;
		}
		return OpenLocked(a_error);
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			return false;
		} catch (...) {
			SetError(a_error, "unknown shader pack compaction failure");
			return false;
		}
	}

	bool Store::Reset(std::string* a_error)
	{
		try {
		std::unique_lock lock(mutex);
		if (!opened && !OpenLocked(a_error))
			return false;
		if (!active.exists || !fallback.exists) {
			SetError(a_error, "both fixed A/B files are required to reset a managed shader pack");
			return false;
		}
		const auto nextGeneration = active.generation + 1;
		ScannedFile first = active;
		ScannedFile second = fallback;
		if (!InitializeEmpty(first, nextGeneration, a_error) || !InitializeEmpty(second, 0, a_error))
			return false;
		return OpenLocked(a_error);
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			return false;
		} catch (...) {
			SetError(a_error, "unknown shader pack reset failure");
			return false;
		}
	}
}
