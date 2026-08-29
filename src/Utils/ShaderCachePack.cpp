#include "Utils/ShaderCachePack.h"

#include "Utils/CryptoHash.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>

#ifdef _WIN32
#	include <Windows.h>
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
		std::byte packSetId[16];
		std::uint64_t reserved;
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

	struct RecordLayout
	{
		std::uint64_t payloadSize = 0;
		std::uint64_t totalSize = 0;
		std::uint64_t logicalOffset = 0;
		std::uint64_t exactOffset = 0;
		std::uint64_t metadataOffset = 0;
		std::uint64_t bytecodeOffset = 0;
	};

	void SetError(std::string* a_error, std::string a_value);

	bool CheckedAdd(std::uint64_t& a_value, std::uint64_t a_addend)
	{
		if (a_addend > (std::numeric_limits<std::uint64_t>::max)() - a_value)
			return false;
		a_value += a_addend;
		return true;
	}

	bool BuildRecordLayout(
		const RecordHeader& a_header,
		std::uint64_t a_recordOffset,
		std::uint64_t a_remainingBytes,
		RecordLayout& a_layout,
		std::string* a_error)
	{
		if (a_header.reserved != 0 || a_header.reserved2 != 0 || a_header.sequence == 0 ||
			a_header.sequence == (std::numeric_limits<std::uint64_t>::max)() ||
			a_header.logicalSize == 0 || a_header.exactSize == 0 || a_header.bytecodeSize == 0) {
			SetError(a_error, "shader pack record contains invalid reserved, sequence, or required-size fields");
			return false;
		}

		std::uint64_t payloadSize = 0;
		if (!CheckedAdd(payloadSize, a_header.logicalSize) ||
			!CheckedAdd(payloadSize, a_header.exactSize) ||
			!CheckedAdd(payloadSize, a_header.metadataSize) ||
			!CheckedAdd(payloadSize, a_header.bytecodeSize) ||
			payloadSize > kMaximumRecordSize ||
			payloadSize > (std::numeric_limits<std::size_t>::max)()) {
			SetError(a_error, "shader pack record payload exceeds checked format limits");
			return false;
		}

		std::uint64_t totalSize = sizeof(RecordHeader);
		if (!CheckedAdd(totalSize, payloadSize) ||
			!CheckedAdd(totalSize, sizeof(CommitTrailer)) ||
			totalSize > a_remainingBytes) {
			SetError(a_error, "shader pack record extends beyond the committed file boundary");
			return false;
		}

		std::uint64_t logicalOffset = a_recordOffset;
		if (!CheckedAdd(logicalOffset, sizeof(RecordHeader))) {
			SetError(a_error, "shader pack record offset exceeds checked format limits");
			return false;
		}
		a_layout.payloadSize = payloadSize;
		a_layout.totalSize = totalSize;
		a_layout.logicalOffset = logicalOffset;
		a_layout.exactOffset = logicalOffset;
		a_layout.metadataOffset = logicalOffset;
		a_layout.bytecodeOffset = logicalOffset;
		if (!CheckedAdd(a_layout.exactOffset, a_header.logicalSize) ||
			!CheckedAdd(a_layout.metadataOffset, a_header.logicalSize) ||
			!CheckedAdd(a_layout.metadataOffset, a_header.exactSize) ||
			!CheckedAdd(a_layout.bytecodeOffset, a_header.logicalSize) ||
			!CheckedAdd(a_layout.bytecodeOffset, a_header.exactSize) ||
			!CheckedAdd(a_layout.bytecodeOffset, a_header.metadataSize)) {
			SetError(a_error, "shader pack payload offsets exceed checked format limits");
			return false;
		}
		return true;
	}

	template <class T>
	bool ReadAt(std::ifstream& a_stream, std::uint64_t a_offset, T& a_output)
	{
		if (a_offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) - sizeof(T))
			return false;
		a_stream.seekg(static_cast<std::streamoff>(a_offset));
		a_stream.read(reinterpret_cast<char*>(&a_output), sizeof(T));
		return a_stream.good();
	}

	bool ReadBytes(std::ifstream& a_stream, std::uint64_t a_offset, void* a_output, std::size_t a_size)
	{
		if (a_size > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()) ||
			a_offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) - a_size)
			return false;
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

	bool HasPackSetId(const Util::ShaderCachePack::PackSetId& a_packSetId)
	{
		return std::ranges::any_of(a_packSetId, [](std::byte a_value) { return a_value != std::byte{}; });
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
	Store::Store(
		std::filesystem::path a_pathA,
		std::filesystem::path a_pathB,
		Lane a_lane,
		PackSetId a_packSetId) :
		pathA(std::move(a_pathA)), pathB(std::move(a_pathB)), lane(a_lane), packSetId(a_packSetId)
	{}

	Store::~Store()
	{
		ReleaseWriterLease();
	}

	bool Store::AcquireWriterLease(std::string* a_error)
	{
#ifdef _WIN32
		if (!HasPackSetId(packSetId) || leaseOwned)
			return true;
		if (leaseHandle) {
			SetError(a_error, "managed shader pack writer lease is held by another process");
			return false;
		}

		auto normalizedPath = [](const std::filesystem::path& a_path) {
			auto value = a_path.lexically_normal().string();
			std::ranges::transform(value, value.begin(), [](unsigned char a_character) {
				return static_cast<char>(std::tolower(a_character));
			});
			return value;
		};
		std::string identity = normalizedPath(pathA) + '|' + normalizedPath(pathB) + '|' +
		                       std::to_string(static_cast<std::uint32_t>(lane));
		const auto digest = CryptoHash::Sha256Hex(identity);
		const auto mutexName = L"Local\\CSX.ShaderCachePack." + std::wstring(digest.begin(), digest.end());
		leaseHandle = CreateMutexW(nullptr, FALSE, mutexName.c_str());
		if (!leaseHandle) {
			SetError(a_error, "failed to create managed shader pack writer lease");
			return false;
		}
		const DWORD result = WaitForSingleObject(static_cast<HANDLE>(leaseHandle), 0);
		if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
			SetError(a_error, "managed shader pack writer lease is held by another process");
			CloseHandle(static_cast<HANDLE>(leaseHandle));
			leaseHandle = nullptr;
			return false;
		}
		leaseOwned = true;
#else
		(void)a_error;
#endif
		return true;
	}

	void Store::ReleaseWriterLease() noexcept
	{
#ifdef _WIN32
		if (leaseOwned && leaseHandle)
			ReleaseMutex(static_cast<HANDLE>(leaseHandle));
		leaseOwned = false;
		if (leaseHandle)
			CloseHandle(static_cast<HANDLE>(leaseHandle));
		leaseHandle = nullptr;
#endif
	}

	bool Store::Scan(const std::filesystem::path& a_path, ScannedFile& a_output, std::string* a_error) const
	{
		// a_path may refer to a_output.path (InitializeEmpty does exactly that).
		// Preserve the value before clearing the output object.
		const auto stablePath = a_path;
		a_output = {};
		a_output.path = stablePath;
		std::error_code error;
		a_output.exists = std::filesystem::exists(stablePath, error);
		if (error) {
			a_output.diagnostic = "failed to inspect pack path: " + error.message();
			SetError(a_error, a_output.diagnostic);
			return false;
		}
		if (!a_output.exists) {
			a_output.diagnostic = "pack file is absent";
			return true;
		}
		a_output.fileSize = std::filesystem::file_size(stablePath, error);
		if (error) {
			a_output.diagnostic = "failed to read pack size: " + error.message();
			SetError(a_error, a_output.diagnostic);
			return false;
		}
		if (a_output.fileSize == 0) {
			a_output.diagnostic = "pack file is empty and requires initialization";
			return true;
		}
		if (a_output.fileSize < sizeof(FileHeader)) {
			a_output.diagnostic = "pack file is shorter than its header";
			return true;
		}

		std::ifstream stream(stablePath, std::ios::binary);
		FileHeader file{};
		if (!ReadAt(stream, 0, file)) {
			a_output.diagnostic = "failed to read pack file header";
			SetError(a_error, a_output.diagnostic);
			return false;
		}
		const auto expectedHeaderHash = CryptoHash::Sha256Bytes(std::span<const std::byte>{
			reinterpret_cast<const std::byte*>(&file), offsetof(FileHeader, hash) });
		if (std::memcmp(file.magic, kFileMagic.data(), kFileMagic.size()) != 0 ||
			file.version != kFormatVersion || file.lane != static_cast<std::uint32_t>(lane) ||
			file.reserved != 0 ||
			(HasPackSetId(packSetId) && std::memcmp(file.packSetId, packSetId.data(), packSetId.size()) != 0) ||
			std::memcmp(file.hash, expectedHeaderHash.data(), expectedHeaderHash.size()) != 0) {
			a_output.diagnostic = "pack file header, lane, set identity, or header hash is invalid";
			return true;
		}

		a_output.valid = true;
		a_output.generation = file.generation;
		a_output.validSize = sizeof(FileHeader);
		std::uint64_t offset = sizeof(FileHeader);
		while (offset <= a_output.fileSize &&
			   a_output.fileSize - offset >= sizeof(RecordHeader) + sizeof(CommitTrailer)) {
			RecordHeader header{};
			if (!ReadAt(stream, offset, header)) {
				a_output.diagnostic = "failed to read shader pack record header";
				break;
			}
			if (std::memcmp(header.magic, kRecordMagic.data(), kRecordMagic.size()) != 0 || header.version != kFormatVersion) {
				a_output.diagnostic = "shader pack record magic or version is invalid";
				break;
			}
			RecordLayout layout;
			std::string layoutError;
			if (!BuildRecordLayout(header, offset, a_output.fileSize - offset, layout, &layoutError)) {
				a_output.diagnostic = std::move(layoutError);
				break;
			}
			std::vector<std::byte> payload(static_cast<std::size_t>(layout.payloadSize));
			if (!ReadBytes(stream, offset + sizeof(RecordHeader), payload.data(), payload.size()))
				break;
			CommitTrailer trailer{};
			if (!ReadAt(stream, offset + sizeof(RecordHeader) + layout.payloadSize, trailer))
				break;
			const auto hash = CryptoHash::Sha256Bytes(payload);
			if (std::memcmp(trailer.magic, kCommitMagic.data(), kCommitMagic.size()) != 0 || trailer.totalSize != layout.totalSize ||
				std::memcmp(header.payloadHash, hash.data(), hash.size()) != 0 ||
				std::memcmp(trailer.payloadHash, hash.data(), hash.size()) != 0)
				break;

			const auto* chars = reinterpret_cast<const char*>(payload.data());
			RecordLocation location{
				.path = stablePath,
				.offset = offset,
				.totalSize = layout.totalSize,
				.sequence = header.sequence,
				.generation = file.generation,
				.logicalKey = std::string(chars, header.logicalSize),
				.exactKey = std::string(chars + header.logicalSize, header.exactSize),
				.metadata = std::string(chars + header.logicalSize + header.exactSize, header.metadataSize),
				.bytecodeOffset = layout.bytecodeOffset,
				.bytecodeSize = header.bytecodeSize,
			};
			a_output.records.push_back(std::move(location));
			if (header.sequence == (std::numeric_limits<std::uint64_t>::max)()) {
				a_output.diagnostic = "shader pack record sequence is exhausted";
				break;
			}
			a_output.nextSequence = (std::max)(a_output.nextSequence, header.sequence + 1);
			offset += layout.totalSize;
			a_output.validSize = offset;
		}
		if (a_output.validSize != a_output.fileSize && a_output.diagnostic.empty())
			a_output.diagnostic = "pack contains an incomplete or corrupt tail after its committed prefix";
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
		std::memcpy(header.packSetId, packSetId.data(), packSetId.size());
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
		if (!AcquireWriterLease(a_error)) {
			opened = false;
			return false;
		}
		ScannedFile a;
		ScannedFile b;
		std::string aError;
		std::string bError;
		const bool scannedA = Scan(pathA, a, &aError);
		const bool scannedB = Scan(pathB, b, &bError);
		if (!scannedA || !scannedB) {
			SetError(a_error, std::format("failed to scan managed pack files (A='{}', B='{}')", aError, bError));
			opened = false;
			return false;
		}
		if (!a.valid && !b.valid) {
			ScannedFile* empty = a.exists && a.fileSize == 0 ? &a : (b.exists && b.fileSize == 0 ? &b : nullptr);
			if (!empty || !InitializeEmpty(*empty, 1, a_error)) {
				if (!empty)
					SetError(a_error, std::format("both managed pack generations are invalid (A='{}', B='{}')", a.diagnostic, b.diagnostic));
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
		const bool fallbackContinuesActive = fallback.valid && fallback.generation != 0 &&
		                                     fallback.generation < (std::numeric_limits<std::uint64_t>::max)() &&
		                                     fallback.generation + 1 == active.generation;
		for (const auto* file : { &fallback, &active }) {
			stats.totalBytes += file->fileSize;
			stats.corruptTailBytes += file->fileSize - file->validSize;
			if (file == &fallback && !fallbackContinuesActive)
				continue;
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
		if (!stream) {
			SetError(a_error, "failed to reopen committed shader pack generation");
			return std::nullopt;
		}
		std::error_code sizeError;
		const auto fileSize = std::filesystem::file_size(a_location.path, sizeError);
		if (sizeError || a_location.offset > fileSize) {
			SetError(a_error, "committed shader pack generation changed after indexing");
			return std::nullopt;
		}
		FileHeader fileHeader{};
		if (!ReadAt(stream, 0, fileHeader)) {
			SetError(a_error, "failed to reread committed shader pack header");
			return std::nullopt;
		}
		const auto fileHeaderHash = CryptoHash::Sha256Bytes(std::span<const std::byte>{
			reinterpret_cast<const std::byte*>(&fileHeader), offsetof(FileHeader, hash) });
		if (std::memcmp(fileHeader.magic, kFileMagic.data(), kFileMagic.size()) != 0 ||
			fileHeader.version != kFormatVersion ||
			fileHeader.lane != static_cast<std::uint32_t>(lane) ||
			fileHeader.generation != a_location.generation ||
			fileHeader.reserved != 0 ||
			(HasPackSetId(packSetId) &&
				std::memcmp(fileHeader.packSetId, packSetId.data(), packSetId.size()) != 0) ||
			std::memcmp(fileHeader.hash, fileHeaderHash.data(), fileHeaderHash.size()) != 0) {
			SetError(a_error, "committed shader pack generation changed after indexing");
			return std::nullopt;
		}

		RecordHeader header{};
		if (!ReadAt(stream, a_location.offset, header) ||
			std::memcmp(header.magic, kRecordMagic.data(), kRecordMagic.size()) != 0 ||
			header.version != kFormatVersion || header.sequence != a_location.sequence) {
			SetError(a_error, "committed shader pack record header changed after indexing");
			return std::nullopt;
		}
		RecordLayout layout;
		if (!BuildRecordLayout(header, a_location.offset, fileSize - a_location.offset, layout, a_error) ||
			layout.totalSize != a_location.totalSize || layout.bytecodeOffset != a_location.bytecodeOffset ||
			header.bytecodeSize != a_location.bytecodeSize) {
			SetError(a_error, "committed shader pack record layout changed after indexing");
			return std::nullopt;
		}
		std::vector<std::byte> payload(static_cast<std::size_t>(layout.payloadSize));
		if (!ReadBytes(stream, layout.logicalOffset, payload.data(), payload.size())) {
			SetError(a_error, "failed to reread committed shader pack payload");
			return std::nullopt;
		}
		CommitTrailer trailer{};
		if (!ReadAt(stream, layout.logicalOffset + layout.payloadSize, trailer)) {
			SetError(a_error, "failed to reread committed shader pack trailer");
			return std::nullopt;
		}
		const auto hash = CryptoHash::Sha256Bytes(payload);
		if (std::memcmp(trailer.magic, kCommitMagic.data(), kCommitMagic.size()) != 0 ||
			trailer.totalSize != layout.totalSize ||
			std::memcmp(header.payloadHash, hash.data(), hash.size()) != 0 ||
			std::memcmp(trailer.payloadHash, hash.data(), hash.size()) != 0) {
			SetError(a_error, "committed shader pack payload failed read-time integrity validation");
			return std::nullopt;
		}

		const auto* chars = reinterpret_cast<const char*>(payload.data());
		Entry result{
			std::string(chars, header.logicalSize),
			std::string(chars + header.logicalSize, header.exactSize),
			std::string(chars + header.logicalSize + header.exactSize, header.metadataSize),
			{}
		};
		if (result.logicalKey != a_location.logicalKey || result.exactKey != a_location.exactKey ||
			result.metadata != a_location.metadata) {
			SetError(a_error, "committed shader pack identity changed after indexing");
			return std::nullopt;
		}
		const auto bytecodeBegin = payload.begin() + static_cast<std::ptrdiff_t>(
														 header.logicalSize + header.exactSize + header.metadataSize);
		result.bytecode.assign(bytecodeBegin, payload.end());
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
		std::ifstream identityStream(a_file.path, std::ios::binary);
		FileHeader currentHeader{};
		if (!ReadAt(identityStream, 0, currentHeader)) {
			SetError(a_error, "failed to verify managed pack generation before append");
			return false;
		}
		const auto currentHeaderHash = CryptoHash::Sha256Bytes(std::span<const std::byte>{
			reinterpret_cast<const std::byte*>(&currentHeader), offsetof(FileHeader, hash) });
		if (std::memcmp(currentHeader.magic, kFileMagic.data(), kFileMagic.size()) != 0 ||
			currentHeader.version != kFormatVersion ||
			currentHeader.lane != static_cast<std::uint32_t>(lane) ||
			currentHeader.generation != a_file.generation ||
			currentHeader.reserved != 0 ||
			(HasPackSetId(packSetId) && std::memcmp(currentHeader.packSetId, packSetId.data(), packSetId.size()) != 0) ||
			std::memcmp(currentHeader.hash, currentHeaderHash.data(), currentHeaderHash.size()) != 0) {
			SetError(a_error, "managed pack generation changed before append");
			return false;
		}
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
		RecordHeader header{};
		std::memcpy(header.magic, kRecordMagic.data(), kRecordMagic.size());
		header.version = kFormatVersion;
		header.sequence = a_sequence;
		header.logicalSize = static_cast<std::uint32_t>(a_entry.logicalKey.size());
		header.exactSize = static_cast<std::uint32_t>(a_entry.exactKey.size());
		header.metadataSize = static_cast<std::uint32_t>(a_entry.metadata.size());
		header.bytecodeSize = a_entry.bytecode.size();
		RecordLayout layout;
		if (!BuildRecordLayout(
				header,
				0,
				(std::numeric_limits<std::uint64_t>::max)(),
				layout,
				a_error)) {
			return false;
		}
		const auto hash = HashPayload(a_entry.logicalKey, a_entry.exactKey, a_entry.metadata, a_entry.bytecode);
		std::memcpy(header.payloadHash, hash.data(), hash.size());
		CommitTrailer trailer{};
		std::memcpy(trailer.magic, kCommitMagic.data(), kCommitMagic.size());
		trailer.totalSize = layout.totalSize;
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
			RecordHeader layoutHeader{};
			layoutHeader.sequence = sequence;
			layoutHeader.logicalSize = static_cast<std::uint32_t>(a_entry.logicalKey.size());
			layoutHeader.exactSize = static_cast<std::uint32_t>(a_entry.exactKey.size());
			layoutHeader.metadataSize = static_cast<std::uint32_t>(a_entry.metadata.size());
			layoutHeader.bytecodeSize = a_entry.bytecode.size();
			RecordLayout layout;
			if (!BuildRecordLayout(
					layoutHeader,
					offset,
					(std::numeric_limits<std::uint64_t>::max)() - offset,
					layout,
					a_error)) {
				opened = false;
				return false;
			}
			RecordLocation location{
				.path = active.path,
				.offset = offset,
				.totalSize = layout.totalSize,
				.sequence = sequence,
				.generation = active.generation,
				.logicalKey = a_entry.logicalKey,
				.exactKey = a_entry.exactKey,
				.metadata = a_entry.metadata,
				.bytecodeOffset = layout.bytecodeOffset,
				.bytecodeSize = a_entry.bytecode.size(),
			};
			active.records.push_back(location);
			active.validSize += layout.totalSize;
			active.fileSize = active.validSize;
			++active.nextSequence;

			stats.totalBytes = stats.totalBytes >= removedTailBytes ? stats.totalBytes - removedTailBytes + layout.totalSize : layout.totalSize;
			stats.corruptTailBytes = stats.corruptTailBytes >= removedTailBytes ? stats.corruptTailBytes - removedTailBytes : 0;
			stats.committedBytes += layout.totalSize;
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

			if (active.generation == (std::numeric_limits<std::uint64_t>::max)()) {
				SetError(a_error, "shader pack generation is exhausted");
				return false;
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
			if (active.generation > (std::numeric_limits<std::uint64_t>::max)() - 2) {
				SetError(a_error, "shader pack generation is exhausted");
				return false;
			}

			// Write and verify a reset barrier into the inactive generation first.
			// The +2 gap makes RebuildIndexes ignore the previous generation even if
			// cleanup of that old file is interrupted.
			ScannedFile resetTarget = fallback;
			if (!InitializeEmpty(resetTarget, active.generation + 2, a_error))
				return false;
			if (!OpenLocked(a_error))
				return false;

			// The reset is already durable and authoritative. Clearing the superseded
			// file is cleanup only; a failure must not invalidate the new empty store.
			ScannedFile oldGeneration = fallback;
			std::string cleanupError;
			if (!InitializeEmpty(oldGeneration, active.generation - 1, &cleanupError)) {
				SetError(a_error, "managed pack reset committed; superseded generation cleanup failed: " + cleanupError);
				return true;
			}
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
