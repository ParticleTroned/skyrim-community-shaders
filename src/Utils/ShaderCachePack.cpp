#include "Utils/ShaderCachePack.h"

#include "Utils/CryptoHash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

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

	std::mutex g_writerLeaseRegistryMutex;
	std::unordered_set<std::string> g_writerLeaseRegistry;

#ifdef CSX_SHADER_CACHE_PACK_TESTING
	std::atomic<std::uint32_t> g_testFailurePoints{ 0 };

	bool ConsumeTestFailurePoint(Util::ShaderCachePack::TestFailurePoint a_failurePoint)
	{
		const auto mask = static_cast<std::uint32_t>(a_failurePoint);
		return (g_testFailurePoints.fetch_and(~mask) & mask) != 0;
	}
#endif

#ifdef _WIN32
	bool AcquireStablePathGuards(
		std::filesystem::path& a_path,
		std::vector<void*>& a_handles,
		std::string* a_error)
	{
		std::error_code absoluteError;
		const auto absolutePath = std::filesystem::absolute(a_path, absoluteError).lexically_normal();
		if (absoluteError || !absolutePath.has_root_path() || !absolutePath.has_parent_path()) {
			SetError(a_error, std::format(
								  "failed to resolve absolute managed shader pack path '{}' ({})",
								  a_path.string(),
								  absoluteError ? absoluteError.message() : "path has no stable parent"));
			return false;
		}

		auto current = absolutePath.root_path();
		for (const auto& component : absolutePath.parent_path().relative_path()) {
			current /= component;
			const HANDLE directory = CreateFileW(
				current.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (directory == INVALID_HANDLE_VALUE) {
				SetError(a_error, std::format(
									  "failed to guard managed shader pack parent '{}' (Windows error {})",
									  current.string(), GetLastError()));
				return false;
			}
			BY_HANDLE_FILE_INFORMATION information{};
			if (!GetFileInformationByHandle(directory, &information) ||
				(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
				(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
				const auto identityError = GetLastError();
				CloseHandle(directory);
				SetError(a_error, std::format(
									  "managed shader pack parent '{}' is not a stable non-reparse directory (Windows error {})",
									  current.string(), identityError));
				return false;
			}
			try {
				a_handles.push_back(directory);
			} catch (...) {
				CloseHandle(directory);
				throw;
			}
		}
		a_path = absolutePath;
		return true;
	}

	bool AcquireFileIdentityGuard(
		const std::filesystem::path& a_path,
		void*& a_handle,
		std::string& a_identity,
		std::string* a_error)
	{
		const HANDLE file = CreateFileW(
			a_path.c_str(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			SetError(a_error, std::format(
								  "failed to acquire physical identity for managed shader pack '{}' (Windows error {})",
								  a_path.string(),
								  GetLastError()));
			return false;
		}

		BY_HANDLE_FILE_INFORMATION information{};
		if (!GetFileInformationByHandle(file, &information) ||
			(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
			const auto identityError = GetLastError();
			CloseHandle(file);
			SetError(a_error, std::format(
								  "managed shader pack '{}' is not a stable regular file identity (Windows error {})",
								  a_path.string(),
								  identityError));
			return false;
		}

		a_handle = file;
		a_identity = std::format(
			"file-id:{:08x}:{:08x}{:08x}",
			information.dwVolumeSerialNumber,
			information.nFileIndexHigh,
			information.nFileIndexLow);
		return true;
	}
#else
	std::string CanonicalLeasePath(const std::filesystem::path& a_path)
	{
		std::error_code error;
		auto canonical = std::filesystem::weakly_canonical(a_path, error);
		if (error) {
			error.clear();
			canonical = std::filesystem::absolute(a_path, error);
			if (error)
				canonical = a_path.lexically_normal();
		}
		return canonical.lexically_normal().generic_string();
	}
#endif

	std::optional<std::uint64_t> ReadUnsigned(const nlohmann::json& a_value)
	{
		if (a_value.is_number_unsigned())
			return a_value.get<std::uint64_t>();
		if (a_value.is_number_integer()) {
			const auto signedValue = a_value.get<std::int64_t>();
			if (signedValue >= 0)
				return static_cast<std::uint64_t>(signedValue);
		}
		return std::nullopt;
	}

	std::optional<Util::ShaderCachePack::PackSetId> ParsePackSetIdText(std::string_view a_value)
	{
		if (a_value.size() != 32)
			return std::nullopt;
		Util::ShaderCachePack::PackSetId result{};
		for (std::size_t index = 0; index < result.size(); ++index) {
			auto nibble = [](char a_character) -> std::optional<std::uint8_t> {
				if (a_character >= '0' && a_character <= '9')
					return static_cast<std::uint8_t>(a_character - '0');
				if (a_character >= 'a' && a_character <= 'f')
					return static_cast<std::uint8_t>(a_character - 'a' + 10);
				return std::nullopt;
			};
			const auto high = nibble(a_value[index * 2]);
			const auto low = nibble(a_value[index * 2 + 1]);
			if (!high || !low)
				return std::nullopt;
			result[index] = static_cast<std::byte>((*high << 4) | *low);
		}
		if (!Util::ShaderCachePack::IsValidPackSetId(result))
			return std::nullopt;
		return result;
	}

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
#ifdef CSX_SHADER_CACHE_PACK_TESTING
	void SetTestFailurePoints(std::uint32_t a_failurePoints)
	{
		g_testFailurePoints.store(a_failurePoints);
	}
#endif

	bool IsValidPackSetId(const PackSetId& a_packSetId)
	{
		return std::ranges::any_of(a_packSetId, [](std::byte a_value) { return a_value != std::byte{}; });
	}

	std::optional<ManifestContract> ParseManifestContract(
		const nlohmann::json& a_manifest,
		std::string_view a_expectedRuntime,
		std::string* a_error)
	{
		auto reject = [&](std::string a_reason) -> std::optional<ManifestContract> {
			SetError(a_error, std::move(a_reason));
			return std::nullopt;
		};
		try {
			const auto schemaVersionValue = a_manifest.find("schemaVersion");
			const auto formatVersionValue = a_manifest.find("formatVersion");
			const auto seedShaderCacheABI = a_manifest.find("shaderCacheABI");
			const auto schemaVersion = schemaVersionValue == a_manifest.end() ? std::nullopt : ReadUnsigned(*schemaVersionValue);
			const auto formatVersion = formatVersionValue == a_manifest.end() ? std::nullopt : ReadUnsigned(*formatVersionValue);
			if (!a_manifest.is_object() ||
				a_manifest.value("schema", std::string{}) != "csx.shader-cache.pack-manifest" ||
				!schemaVersion || *schemaVersion != 2 ||
				!formatVersion || *formatVersion != kFormatVersion ||
				a_manifest.value("fileStateSemantics", std::string{}) != "installation-baseline-v1" ||
				a_manifest.value("hashAlgorithm", std::string{}) != "sha256" ||
				a_manifest.value("runtime", std::string{}) != a_expectedRuntime ||
				seedShaderCacheABI == a_manifest.end() || !seedShaderCacheABI->is_string() ||
				seedShaderCacheABI->get_ref<const std::string&>().empty()) {
				return reject("managed shader pack manifest metadata does not match this runtime or format");
			}

			const auto packSetValue = a_manifest.find("packSetId");
			if (packSetValue == a_manifest.end() || !packSetValue->is_string())
				return reject("managed shader pack manifest has no valid nonzero pack-set identity");
			const auto packSetId = ParsePackSetIdText(packSetValue->get_ref<const std::string&>());
			if (!packSetId)
				return reject("managed shader pack manifest has no valid nonzero pack-set identity");

			const auto optimizedValue = a_manifest.find("optimizedRecordCount");
			const auto developerValue = a_manifest.find("developerRecordCount");
			if (optimizedValue == a_manifest.end() || developerValue == a_manifest.end())
				return reject("managed shader pack manifest is missing aggregate record counts");
			const auto optimizedRecordCount = ReadUnsigned(*optimizedValue);
			const auto developerRecordCount = ReadUnsigned(*developerValue);
			if (!optimizedRecordCount || !developerRecordCount)
				return reject("managed shader pack manifest record counts must be unsigned integers");

			const auto variants = a_manifest.find("compatibilityVariants");
			if (variants == a_manifest.end() || !variants->is_array() || variants->empty())
				return reject("managed shader pack manifest has no compatibility variants");
			std::unordered_set<std::string> uniqueVariants;
			bool hasDefault = false;
			for (const auto& value : *variants) {
				if (!value.is_string())
					return reject("managed shader pack compatibility variants must be nonempty strings");
				const auto& variant = value.get_ref<const std::string&>();
				if (variant.empty() || !uniqueVariants.insert(variant).second)
					return reject("managed shader pack compatibility variants must be nonempty and unique");
				hasDefault = hasDefault || variant == "default";
			}
			if (!hasDefault)
				return reject("managed shader pack compatibility variants must include default");

			const auto files = a_manifest.find("files");
			if (files == a_manifest.end() || !files->is_object() || files->size() != 4)
				return reject("managed shader pack manifest must describe exactly four fixed pack files");

			struct ExpectedFile
			{
				std::string_view name;
				Lane lane;
				std::uint64_t* laneTotal;
				std::size_t contractIndex;
			};
			std::uint64_t optimizedFileTotal = 0;
			std::uint64_t developerFileTotal = 0;
			std::array<ExpectedFile, 4> expected{ {
				{ "Optimized.A.csxpack", Lane::Optimized, &optimizedFileTotal, 0 },
				{ "Optimized.B.csxpack", Lane::Optimized, &optimizedFileTotal, 1 },
				{ "Developer.A.csxpack", Lane::Developer, &developerFileTotal, 2 },
				{ "Developer.B.csxpack", Lane::Developer, &developerFileTotal, 3 },
			} };
			std::array<ManifestContract::FileBaseline, 4> fileBaselines{};
			for (auto& expectedFile : expected) {
				const auto file = files->find(expectedFile.name);
				if (file == files->end() || !file->is_object() || file->size() != 3)
					return reject("managed shader pack manifest has an invalid fixed-file entry");
				const auto laneValue = file->find("lane");
				const auto generationValue = file->find("generation");
				const auto recordCountValue = file->find("recordCount");
				if (laneValue == file->end() || generationValue == file->end() || recordCountValue == file->end())
					return reject("managed shader pack manifest file entry is incomplete");
				const auto lane = ReadUnsigned(*laneValue);
				const auto generation = ReadUnsigned(*generationValue);
				const auto recordCount = ReadUnsigned(*recordCountValue);
				if (!lane || !generation || !recordCount ||
					*lane != static_cast<std::uint32_t>(expectedFile.lane) ||
					*recordCount > (std::numeric_limits<std::uint64_t>::max)() - *expectedFile.laneTotal) {
					return reject("managed shader pack manifest file entry has invalid lane, generation, or record count");
				}
				fileBaselines[expectedFile.contractIndex] = {
					.lane = expectedFile.lane,
					.generation = *generation,
					.recordCount = *recordCount,
				};
				*expectedFile.laneTotal += *recordCount;
			}
			auto adjacentGenerations = [](std::uint64_t a_first, std::uint64_t a_second) {
				return a_first > a_second ? a_first - a_second == 1 : a_second - a_first == 1;
			};
			if (!adjacentGenerations(fileBaselines[0].generation, fileBaselines[1].generation) ||
				!adjacentGenerations(fileBaselines[2].generation, fileBaselines[3].generation)) {
				return reject("managed shader pack manifest has ambiguous A/B generations");
			}
			if (optimizedFileTotal != *optimizedRecordCount || developerFileTotal != *developerRecordCount)
				return reject("managed shader pack manifest aggregate record counts disagree with its file entries");

			if (a_error)
				a_error->clear();
			return ManifestContract{
				.packSetId = *packSetId,
				.optimizedRecordCount = *optimizedRecordCount,
				.developerRecordCount = *developerRecordCount,
				.files = fileBaselines,
			};
		} catch (const std::exception& e) {
			return reject(std::string("managed shader pack manifest validation failed: ") + e.what());
		} catch (...) {
			return reject("managed shader pack manifest validation failed");
		}
	}

	bool ValidateManifestFileStates(
		const ManifestContract& a_contract,
		const std::array<PackFileState, 4>& a_files,
		std::string* a_error)
	{
		for (std::size_t index = 0; index < a_files.size(); ++index) {
			const auto& baseline = a_contract.files[index];
			const auto& actual = a_files[index];
			if (!actual.valid || actual.lane != baseline.lane || actual.packSetId != a_contract.packSetId) {
				SetError(a_error, std::format("managed shader pack file {} has invalid identity, lane, or contents", index));
				return false;
			}
			if (actual.generation < baseline.generation ||
				(actual.generation == baseline.generation && actual.recordCount < baseline.recordCount)) {
				SetError(a_error, std::format("managed shader pack file {} regresses below its manifest installation baseline", index));
				return false;
			}
		}
		if (a_files[0].generation == a_files[1].generation ||
			a_files[2].generation == a_files[3].generation) {
			SetError(a_error, "managed shader pack A/B generations are equal and therefore ambiguous");
			return false;
		}
		if (a_error)
			a_error->clear();
		return true;
	}

	bool ValidateDistinctFileIdentities(
		const std::array<std::string, 4>& a_identities,
		std::string* a_error)
	{
		std::unordered_set<std::string> unique;
		for (const auto& identity : a_identities) {
			if (identity.empty() || !unique.insert(identity).second) {
				SetError(a_error, "managed shader pack fixed members must resolve to four distinct stable file identities");
				return false;
			}
		}
		if (a_error)
			a_error->clear();
		return true;
	}

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
		if (leaseOwned)
			return true;
		if (!IsValidPackSetId(packSetId)) {
			SetError(a_error, "managed shader pack requires a nonzero pack-set identity");
			return false;
		}

#ifdef _WIN32
		if (!AcquireStablePathGuards(pathA, pathGuardHandles, a_error) ||
			!AcquireStablePathGuards(pathB, pathGuardHandles, a_error) ||
			!AcquireFileIdentityGuard(pathA, fileIdentityHandles[0], fileIdentityKeys[0], a_error) ||
			!AcquireFileIdentityGuard(pathB, fileIdentityHandles[1], fileIdentityKeys[1], a_error)) {
			ReleaseWriterLease();
			return false;
		}
#else
		fileIdentityKeys = { CanonicalLeasePath(pathA), CanonicalLeasePath(pathB) };
#endif
		if (fileIdentityKeys[0].empty() || fileIdentityKeys[1].empty() ||
			fileIdentityKeys[0] == fileIdentityKeys[1]) {
			SetError(a_error, "managed shader pack A/B members must be distinct stable file identities");
			ReleaseWriterLease();
			return false;
		}
		bool registryConflict = false;
		{
			std::lock_guard registryLock(g_writerLeaseRegistryMutex);
			registryConflict = std::ranges::any_of(fileIdentityKeys, [](const auto& a_identity) {
				return g_writerLeaseRegistry.contains(a_identity);
			});
			if (!registryConflict) {
				for (std::size_t index = 0; index < fileIdentityKeys.size(); ++index) {
					g_writerLeaseRegistry.insert(fileIdentityKeys[index]);
					processRegistryOwned[index] = true;
				}
			}
		}
		if (registryConflict) {
			SetError(a_error, "managed shader pack member writer lease is already held in this process");
			ReleaseWriterLease();
			return false;
		}
#ifdef CSX_SHADER_CACHE_PACK_TESTING
		if (ConsumeTestFailurePoint(TestFailurePoint::AfterRegistryInsert))
			throw std::runtime_error("injected shader pack failure after writer-registry insertion");
#endif

#ifdef _WIN32
		auto sortedIdentities = fileIdentityKeys;
		std::ranges::sort(sortedIdentities);
		// Each physical member needs its own lease because different A/B pairs
		// can share a file. Kernel handles release ownership on process exit.
		for (std::size_t index = 0; index < sortedIdentities.size(); ++index) {
			const auto digest = CryptoHash::Sha256Hex(sortedIdentities[index]);
			const auto leaseName = std::wstring(L"\\\\.\\pipe\\CSX.ShaderCachePack.") +
			                       std::wstring(digest.begin(), digest.end());
			SetLastError(ERROR_SUCCESS);
			leaseHandles[index] = CreateNamedPipeW(
				leaseName.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				1, 1, 1, 0, nullptr);
			const auto leaseError = GetLastError();
			if (leaseHandles[index] == INVALID_HANDLE_VALUE) {
				leaseHandles[index] = nullptr;
				SetError(a_error, std::format(
									  "managed shader pack member writer lease is held or unavailable (Windows error {})",
									  leaseError));
				ReleaseWriterLease();
				return false;
			}
		}
#endif
		leaseOwned = true;
		return true;
	}

	std::array<PackFileState, 2> Store::GetFileStates() const
	{
		std::shared_lock lock(mutex);
		auto state = [&](const ScannedFile& a_file) {
			return PackFileState{
				.packSetId = packSetId,
				.lane = lane,
				.valid = a_file.valid,
				.generation = a_file.generation,
				.recordCount = a_file.records.size(),
			};
		};
		const auto& fileA = active.path == pathA ? active : fallback;
		const auto& fileB = active.path == pathB ? active : fallback;
		return { state(fileA), state(fileB) };
	}

	std::array<std::string, 2> Store::GetFileIdentityKeys() const
	{
		std::shared_lock lock(mutex);
		return fileIdentityKeys;
	}

	void Store::ReleaseWriterLease() noexcept
	{
#ifdef _WIN32
		for (auto& handle : leaseHandles) {
			if (handle)
				CloseHandle(static_cast<HANDLE>(handle));
			handle = nullptr;
		}
		for (auto& handle : fileIdentityHandles) {
			if (handle)
				CloseHandle(static_cast<HANDLE>(handle));
			handle = nullptr;
		}
		for (auto& handle : pathGuardHandles) {
			if (handle)
				CloseHandle(static_cast<HANDLE>(handle));
		}
		pathGuardHandles.clear();
#endif
		if (std::ranges::any_of(processRegistryOwned, [](bool a_owned) { return a_owned; })) {
			std::lock_guard registryLock(g_writerLeaseRegistryMutex);
			for (std::size_t index = 0; index < processRegistryOwned.size(); ++index) {
				if (processRegistryOwned[index])
					g_writerLeaseRegistry.erase(fileIdentityKeys[index]);
			}
		}
		leaseOwned = false;
		processRegistryOwned = {};
		fileIdentityKeys = {};
	}

	void Store::InvalidateStateLocked() noexcept
	{
		opened = false;
		active = {};
		fallback = {};
		exactIndex.clear();
		recordsByLogical.clear();
		liveByLogical.clear();
		activeLiveByLogical.clear();
		stats = {};
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
			std::memcmp(file.packSetId, packSetId.data(), packSetId.size()) != 0 ||
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
			if (header.sequence < a_output.nextSequence) {
				a_output.diagnostic = "shader pack record sequence does not increase within its generation";
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
			a_output.nextSequence = header.sequence + 1;
			offset += layout.totalSize;
			a_output.validSize = offset;
		}
		if (a_output.validSize != a_output.fileSize && a_output.diagnostic.empty())
			a_output.diagnostic = "pack contains an incomplete or corrupt tail after its committed prefix";
		return true;
	}

	bool Store::InitializeEmpty(
		ScannedFile& a_file,
		std::uint64_t a_generation,
		std::string* a_error,
		InitializeProgress* a_progress,
		[[maybe_unused]] InitializePurpose a_purpose) const
	{
		if (a_progress)
			*a_progress = InitializeProgress::Unchanged;
		std::error_code existenceError;
		if (!a_file.exists || !std::filesystem::is_regular_file(a_file.path, existenceError) || existenceError) {
			SetError(a_error, "pack file is absent; runtime will not create files outside the shipped managed cache mod");
			return false;
		}
#ifdef CSX_SHADER_CACHE_PACK_TESTING
		const auto beforeMutationFailure = a_purpose == InitializePurpose::ResetCleanup ?
		                                       TestFailurePoint::BeforeResetCleanupMutation :
		                                       TestFailurePoint::BeforeInitializeMutation;
		if (ConsumeTestFailurePoint(beforeMutationFailure)) {
			SetError(a_error, "injected failure before shader pack initialization mutation");
			return false;
		}
#endif
		FileHeader header{};
		std::memcpy(header.magic, kFileMagic.data(), kFileMagic.size());
		header.version = kFormatVersion;
		header.lane = static_cast<std::uint32_t>(lane);
		header.generation = a_generation;
		std::memcpy(header.packSetId, packSetId.data(), packSetId.size());
		const auto hash = CryptoHash::Sha256Bytes(std::span<const std::byte>{
			reinterpret_cast<const std::byte*>(&header), offsetof(FileHeader, hash) });
		std::memcpy(header.hash, hash.data(), hash.size());
		if (a_progress)
			*a_progress = InitializeProgress::MutationStarted;
		{
			std::ofstream stream(a_file.path, std::ios::binary | std::ios::trunc);
#ifdef CSX_SHADER_CACHE_PACK_TESTING
			const auto afterTruncateFailure = a_purpose == InitializePurpose::ResetCleanup ?
			                                      TestFailurePoint::AfterResetCleanupTruncate :
			                                      TestFailurePoint::AfterInitializeTruncate;
			const auto throwAfterTruncate = a_purpose == InitializePurpose::ResetCleanup ?
			                                    TestFailurePoint::ThrowAfterResetCleanupTruncate :
			                                    TestFailurePoint::ThrowAfterInitializeTruncate;
			if (ConsumeTestFailurePoint(afterTruncateFailure)) {
				SetError(a_error, "injected failure after shader pack initialization truncate");
				return false;
			}
			if (ConsumeTestFailurePoint(throwAfterTruncate))
				throw std::runtime_error("injected exception after shader pack initialization truncate");
#endif
			stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
			stream.flush();
			if (!stream.good()) {
				SetError(a_error, "failed to initialize existing shader pack file");
				return false;
			}
#ifdef CSX_SHADER_CACHE_PACK_TESTING
			const auto afterWriteFailure = a_purpose == InitializePurpose::ResetCleanup ?
			                                   TestFailurePoint::AfterResetCleanupWrite :
			                                   TestFailurePoint::AfterInitializeWrite;
			const auto throwAfterWrite = a_purpose == InitializePurpose::ResetCleanup ?
			                                 TestFailurePoint::ThrowAfterResetCleanupWrite :
			                                 TestFailurePoint::ThrowAfterInitializeWrite;
			if (ConsumeTestFailurePoint(afterWriteFailure)) {
				SetError(a_error, "injected failure after shader pack initialization write");
				return false;
			}
			if (ConsumeTestFailurePoint(throwAfterWrite))
				throw std::runtime_error("injected exception after shader pack initialization write");
#endif
		}
		if (!DurableFlush(a_file.path)) {
			SetError(a_error, "failed to durably flush initialized shader pack file");
			return false;
		}
		if (a_progress)
			*a_progress = InitializeProgress::Durable;
#ifdef CSX_SHADER_CACHE_PACK_TESTING
		const auto afterDurableFlushFailure = a_purpose == InitializePurpose::ResetCleanup ?
		                                          TestFailurePoint::AfterResetCleanupDurableFlush :
		                                          TestFailurePoint::AfterInitializeDurableFlush;
		const auto throwAfterDurableFlush = a_purpose == InitializePurpose::ResetCleanup ?
		                                        TestFailurePoint::ThrowAfterResetCleanupDurableFlush :
		                                        TestFailurePoint::ThrowAfterInitializeDurableFlush;
		if (ConsumeTestFailurePoint(afterDurableFlushFailure)) {
			SetError(a_error, "injected failure after durable shader pack initialization flush");
			return false;
		}
		if (ConsumeTestFailurePoint(throwAfterDurableFlush))
			throw std::runtime_error("injected exception after durable shader pack initialization flush");
		if (a_purpose == InitializePurpose::ResetCleanup &&
			ConsumeTestFailurePoint(TestFailurePoint::ThrowBeforeResetCleanupVerification))
			throw std::runtime_error("injected exception before shader pack reset cleanup verification");
#endif
		if (!Scan(a_file.path, a_file, a_error))
			return false;
		if (a_progress)
			*a_progress = InitializeProgress::Verified;
		return true;
	}

	bool Store::Open(std::string* a_error)
	{
		return OpenWithInitialization(false, a_error);
	}

	bool Store::InitializeEmptyFilesAndOpen(std::string* a_error)
	{
		return OpenWithInitialization(true, a_error);
	}

	bool Store::OpenWithInitialization(bool a_allowEmptyInitialization, std::string* a_error)
	{
		std::unique_lock lock(mutex, std::defer_lock);
		try {
			lock.lock();
			const bool result = OpenLocked(a_allowEmptyInitialization, a_error);
			if (!result)
				ReleaseWriterLease();
			return result;
		} catch (const std::exception& e) {
			if (lock.owns_lock()) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, e.what());
			} catch (...) {
			}
			return false;
		} catch (...) {
			if (lock.owns_lock()) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, "unknown shader pack admission failure");
			} catch (...) {
			}
			return false;
		}
	}

	void Store::Close()
	{
		std::unique_lock lock(mutex);
		InvalidateStateLocked();
		ReleaseWriterLease();
	}

	bool Store::OpenLocked(bool a_allowEmptyInitialization, std::string* a_error)
	{
		if (a_error)
			a_error->clear();
		// Admission publishes one coherent snapshot or no readable state.
		InvalidateStateLocked();
		if (!IsValidPackSetId(packSetId)) {
			SetError(a_error, "managed shader pack requires a nonzero pack-set identity");
			return false;
		}
		if (!AcquireWriterLease(a_error)) {
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
			return false;
		}
		if (!a.exists || !b.exists) {
			SetError(a_error, std::format(
								  "both fixed A/B files are required (A='{}', B='{}')",
								  a.diagnostic,
								  b.diagnostic));
			return false;
		}
		struct BootstrapRollbackResult
		{
			std::array<bool, 2> restored{};
			bool recoveryException = false;

			[[nodiscard]] bool AllRestored() const noexcept { return restored[0] && restored[1]; }
		};
		auto restoreEmptyPair = [&]() noexcept {
			BootstrapRollbackResult result;
			const std::array<const std::filesystem::path*, 2> paths{ &pathA, &pathB };
			for (std::size_t index = 0; index < paths.size(); ++index) {
				const auto& path = *paths[index];
				try {
#ifdef CSX_SHADER_CACHE_PACK_TESTING
					try {
						if (index == 0 && ConsumeTestFailurePoint(TestFailurePoint::ThrowBeforeFirstBootstrapRollback))
							throw std::runtime_error("injected exception before first shader pack bootstrap rollback member");
						if (index == 1 && ConsumeTestFailurePoint(TestFailurePoint::ThrowBetweenBootstrapRollbackMembers))
							throw std::runtime_error("injected exception between shader pack bootstrap rollback members");
					} catch (...) {
						// Recovery bookkeeping and diagnostics must not prevent either
						// physical member from being restored and verified.
						result.recoveryException = true;
					}
					if (ConsumeTestFailurePoint(TestFailurePoint::DuringBootstrapRollback))
						continue;
#endif
					std::ofstream stream(path, std::ios::binary | std::ios::trunc);
					if (!stream)
						continue;
					stream.close();
					std::error_code sizeError;
					const auto restoredSize = std::filesystem::file_size(path, sizeError);
					if (!stream.good() || sizeError || restoredSize != 0 || !DurableFlush(path))
						continue;
					result.restored[index] = true;
				} catch (...) {
					result.recoveryException = true;
				}
			}
			return result;
		};
		auto reportBootstrapFailure = [&](std::string_view a_initializationError, const BootstrapRollbackResult& a_rollback) noexcept {
			try {
#ifdef CSX_SHADER_CACHE_PACK_TESTING
				if (ConsumeTestFailurePoint(TestFailurePoint::ThrowDuringBootstrapRollbackDiagnostic))
					throw std::runtime_error("injected exception during shader pack bootstrap rollback diagnostic");
#endif
				std::string rollbackDetail;
				if (!a_rollback.restored[0])
					rollbackDetail = "member A was not restored";
				if (!a_rollback.restored[1]) {
					if (!rollbackDetail.empty())
						rollbackDetail += "; ";
					rollbackDetail += "member B was not restored";
				}
				if (a_rollback.recoveryException) {
					if (!rollbackDetail.empty())
						rollbackDetail += "; ";
					rollbackDetail += "a rollback operation raised an exception";
				}
				const auto separator = a_initializationError.empty() ? "" : "; ";
				SetError(a_error, a_rollback.AllRestored() ?
									  std::format("{}{}bootstrap rollback restored both members to zero bytes{}",
										  a_initializationError,
										  separator,
										  a_rollback.recoveryException ? " despite a recovered rollback exception" : "") :
									  std::format("{}{}bootstrap rollback failed or is indeterminate: {}",
										  a_initializationError,
										  separator,
										  rollbackDetail));
			} catch (...) {
				try {
					SetError(a_error, a_rollback.AllRestored() ?
										  "shader pack bootstrap failed; rollback restored both members to zero bytes; detailed diagnostic unavailable" :
										  "shader pack bootstrap failed; rollback failed or is indeterminate; detailed diagnostic unavailable");
				} catch (...) {
					// Physical recovery has already attempted and verified both
					// members. An allocation failure may prevent textual reporting.
				}
			}
		};
		bool bootstrapRollbackArmed = false;
		auto failBootstrap = [&](std::string_view a_failure) noexcept {
			InvalidateStateLocked();
			const auto rollback = restoreEmptyPair();
			bootstrapRollbackArmed = false;
			reportBootstrapFailure(a_failure, rollback);
			return false;
		};
		if (!a.valid || !b.valid) {
			const bool emptyPair = a.fileSize == 0 && b.fileSize == 0;
			if (!a_allowEmptyInitialization || !emptyPair) {
				SetError(a_error, std::format(
									  "managed pack admission is read-only and requires two valid prebuilt files (A='{}', B='{}')",
									  a.diagnostic,
									  b.diagnostic));
				return false;
			}

			bootstrapRollbackArmed = true;
			try {
				bool initialized = false;
				std::string initializationError;
				try {
					const bool initializedA = InitializeEmpty(a, 1, a_error);
#ifdef CSX_SHADER_CACHE_PACK_TESTING
					if (initializedA && ConsumeTestFailurePoint(TestFailurePoint::AfterFirstBootstrapInitialization))
						throw std::runtime_error("injected failure after first shader pack bootstrap initialization");
					const bool initializeB = !ConsumeTestFailurePoint(TestFailurePoint::BeforeSecondBootstrapInitialization);
					if (!initializeB)
						SetError(a_error, "injected failure before second shader pack bootstrap initialization");
#else
					constexpr bool initializeB = true;
#endif
					initialized = initializedA && initializeB && InitializeEmpty(b, 0, a_error);
					if (!initialized)
						initializationError = a_error ? *a_error : std::string{};
				} catch (const std::exception& e) {
					initializationError = std::format("shader pack bootstrap initialization raised an exception: {}", e.what());
				} catch (...) {
					initializationError = "shader pack bootstrap initialization raised an unknown exception";
				}
				if (!initialized) {
					return failBootstrap(initializationError);
				}
			} catch (const std::exception& e) {
				return failBootstrap(e.what());
			} catch (...) {
				return failBootstrap("shader pack bootstrap transaction raised an unknown exception");
			}
		}
		std::string degraded;
		if (!a.diagnostic.empty())
			degraded = "A: " + a.diagnostic;
		if (!b.diagnostic.empty()) {
			if (!degraded.empty())
				degraded += "; ";
			degraded += "B: " + b.diagnostic;
		}
		if (a.valid && b.valid && a.generation == b.generation) {
			SetError(a_error, "managed shader pack A/B generations are equal and therefore ambiguous");
			return false;
		}
		try {
#ifdef CSX_SHADER_CACHE_PACK_TESTING
			if (ConsumeTestFailurePoint(TestFailurePoint::BeforeStoreAdmissionCommit)) {
				if (bootstrapRollbackArmed)
					return failBootstrap("injected failure before shader pack admission commit");
				SetError(a_error, "injected failure before shader pack admission commit");
				return false;
			}
#endif
			if (b.valid && (!a.valid || b.generation > a.generation)) {
				active = std::move(b);
				fallback = std::move(a);
			} else {
				active = std::move(a);
				fallback = std::move(b);
			}
			if (fallback.valid && active.generation - fallback.generation > 1) {
				if (!degraded.empty())
					degraded += "; ";
				degraded += std::format(
					"A/B generation gap is {} (authoritative generation {}, superseded generation {}); prior reset cleanup may be incomplete",
					active.generation - fallback.generation,
					active.generation,
					fallback.generation);
			}
			opened = active.valid && fallback.valid;
#ifdef CSX_SHADER_CACHE_PACK_TESTING
			if (ConsumeTestFailurePoint(TestFailurePoint::DuringStoreAdmissionCommit))
				throw std::runtime_error("injected failure during shader pack admission commit");
			if (ConsumeTestFailurePoint(TestFailurePoint::DuringStoreIndexPublication))
				throw std::runtime_error("injected failure during shader pack index publication");
#endif
			RebuildIndexes();
			bootstrapRollbackArmed = false;
		} catch (const std::exception& e) {
			InvalidateStateLocked();
			if (bootstrapRollbackArmed)
				return failBootstrap(e.what());
			throw;
		} catch (...) {
			InvalidateStateLocked();
			if (bootstrapRollbackArmed)
				return failBootstrap("shader pack admission raised an unknown exception");
			throw;
		}
		if (!degraded.empty())
			SetError(a_error, std::move(degraded));
		return opened;
	}

	void Store::RebuildIndexes()
	{
		exactIndex.clear();
		recordsByLogical.clear();
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
		for (const auto& [_, record] : exactIndex)
			recordsByLogical[record.logicalKey].push_back(record);
		for (auto& [_, records] : recordsByLogical) {
			std::ranges::sort(records, [](const RecordLocation& a_left, const RecordLocation& a_right) {
				return std::pair{ a_left.generation, a_left.sequence } >
				       std::pair{ a_right.generation, a_right.sequence };
			});
		}
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
			std::memcmp(fileHeader.packSetId, packSetId.data(), packSetId.size()) != 0 ||
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

	std::optional<Entry> Store::FindCompatible(
		std::string_view a_logicalKey,
		const std::function<bool(std::string_view)>& a_acceptMetadata,
		std::string* a_error) const
	{
		try {
			std::shared_lock lock(mutex);
			if (!opened)
				return std::nullopt;
			const auto found = recordsByLogical.find(std::string(a_logicalKey));
			if (found == recordsByLogical.end())
				return std::nullopt;
			for (const auto& record : found->second) {
				if (a_acceptMetadata(record.metadata))
					return Read(record, a_error);
			}
			return std::nullopt;
		} catch (const std::exception& e) {
			SetError(a_error, e.what());
			return std::nullopt;
		} catch (...) {
			SetError(a_error, "unknown compatible shader pack read failure");
			return std::nullopt;
		}
	}

	bool Store::AppendLocked(
		ScannedFile& a_file,
		const Entry& a_entry,
		std::uint64_t a_sequence,
		bool a_checkpoint,
		std::string* a_error,
		bool* a_mutationStarted) const
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
			std::memcmp(currentHeader.packSetId, packSetId.data(), packSetId.size()) != 0 ||
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
				a_file.validSize,
				(std::numeric_limits<std::uint64_t>::max)() - a_file.validSize,
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

		if (a_mutationStarted)
			*a_mutationStarted = true;
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
#ifdef CSX_SHADER_CACHE_PACK_TESTING
		if (ConsumeTestFailurePoint(TestFailurePoint::AfterAppendWrite)) {
			SetError(a_error, "injected failure after shader pack append write");
			return false;
		}
#endif
		return true;
	}

	bool Store::Append(const Entry& a_entry, std::string* a_error)
	{
		bool admissionPending = false;
		bool mutationStarted = false;
		std::unique_lock lock(mutex, std::defer_lock);
		try {
			lock.lock();
			admissionPending = !opened;
			if (admissionPending) {
				if (!OpenLocked(false, a_error)) {
					ReleaseWriterLease();
					return false;
				}
				admissionPending = false;
			}
			const auto offset = active.validSize;
			const auto sequence = active.nextSequence;
			const auto removedTailBytes = active.fileSize - active.validSize;
			if (!AppendLocked(active, a_entry, sequence, false, a_error, &mutationStarted)) {
				if (mutationStarted) {
					InvalidateStateLocked();
					ReleaseWriterLease();
				}
				return false;
			}
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
				InvalidateStateLocked();
				ReleaseWriterLease();
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
#ifdef CSX_SHADER_CACHE_PACK_TESTING
			if (ConsumeTestFailurePoint(TestFailurePoint::DuringAppendIndexPublication))
				throw std::runtime_error("injected exception during shader pack append index publication");
#endif
			exactIndex.insert_or_assign(location.exactKey, location);
			auto& compatibleRecords = recordsByLogical[location.logicalKey];
			std::erase_if(compatibleRecords, [&](const RecordLocation& a_record) {
				return a_record.exactKey == location.exactKey;
			});
			compatibleRecords.insert(compatibleRecords.begin(), location);
			const auto live = liveByLogical.find(location.logicalKey);
			if (live == liveByLogical.end() || std::pair{ location.generation, location.sequence } >=
												   std::pair{ live->second.generation, live->second.sequence })
				liveByLogical.insert_or_assign(location.logicalKey, location);
			return true;
		} catch (const std::exception& e) {
			if (admissionPending || mutationStarted) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, e.what());
			} catch (...) {
			}
			return false;
		} catch (...) {
			if (admissionPending || mutationStarted) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, "unknown shader pack append failure");
			} catch (...) {
			}
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
		InitializeProgress compactionProgress = InitializeProgress::Unchanged;
		std::unique_lock lock(mutex, std::defer_lock);
		try {
			lock.lock();
			auto failAfterMutation = [&](std::string_view a_fallbackError = {}) noexcept {
				InvalidateStateLocked();
				ReleaseWriterLease();
				if (!a_fallbackError.empty()) {
					try {
						SetError(a_error, std::string(a_fallbackError));
					} catch (...) {
					}
				}
				return false;
			};
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
			if (!InitializeEmpty(target, active.generation + 1, a_error, &compactionProgress))
				return compactionProgress == InitializeProgress::Unchanged ? false : failAfterMutation();
			std::uint64_t sequence = 1;
			for (const auto& entry : live) {
				if (!AppendLocked(target, entry, sequence++, false, a_error))
					return failAfterMutation();
#ifdef CSX_SHADER_CACHE_PACK_TESTING
				if (ConsumeTestFailurePoint(TestFailurePoint::DuringCompactionCopy)) {
					return failAfterMutation("injected failure during shader pack compaction copy");
				}
#endif
				target.validSize = std::filesystem::file_size(target.path);
			}
			if (!DurableFlush(target.path)) {
				return failAfterMutation("failed to durably checkpoint compacted shader pack");
			}
			if (!OpenLocked(false, a_error)) {
				ReleaseWriterLease();
				return false;
			}
			return true;
		} catch (const std::exception& e) {
			if (compactionProgress != InitializeProgress::Unchanged) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, e.what());
			} catch (...) {
			}
			return false;
		} catch (...) {
			if (compactionProgress != InitializeProgress::Unchanged) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, "unknown shader pack compaction failure");
			} catch (...) {
			}
			return false;
		}
	}

	ResetDisposition Store::Reset(std::string* a_error)
	{
		bool barrierCommitted = false;
		bool admissionPending = false;
		InitializeProgress resetProgress = InitializeProgress::Unchanged;
		std::unique_lock lock(mutex, std::defer_lock);
		try {
			lock.lock();
			admissionPending = !opened;
			if (admissionPending) {
				if (!OpenLocked(false, a_error)) {
					ReleaseWriterLease();
					return ResetDisposition::FailedBeforeCommit;
				}
				admissionPending = false;
			}
			if (!active.exists || !fallback.exists) {
				SetError(a_error, "both fixed A/B files are required to reset a managed shader pack");
				return ResetDisposition::FailedBeforeCommit;
			}
			if (active.generation > (std::numeric_limits<std::uint64_t>::max)() - 2) {
				SetError(a_error, "shader pack generation is exhausted");
				return ResetDisposition::FailedBeforeCommit;
			}

			// Write and verify a reset barrier into the inactive generation first.
			// The +2 gap makes RebuildIndexes ignore the previous generation even if
			// cleanup of that old file is interrupted.
			ScannedFile resetTarget = fallback;
			if (!InitializeEmpty(resetTarget, active.generation + 2, a_error, &resetProgress)) {
				if (resetProgress == InitializeProgress::Unchanged)
					return ResetDisposition::FailedBeforeCommit;

				const bool durableBarrier = resetProgress == InitializeProgress::Durable;
				std::string initializationError;
				if (a_error)
					initializationError.swap(*a_error);
				InvalidateStateLocked();
				ReleaseWriterLease();
				try {
					SetError(a_error, std::format(
										  "managed pack reset {} but could not establish a verified authoritative generation{}{}",
										  durableBarrier ? "barrier committed" : "mutation is commit-uncertain",
										  initializationError.empty() ? "" : ": ",
										  initializationError));
				} catch (...) {
					try {
						SetError(a_error, durableBarrier ?
											  "managed pack reset barrier committed but verification failed; detailed diagnostic unavailable" :
											  "managed pack reset mutation is commit-uncertain; detailed diagnostic unavailable");
					} catch (...) {
					}
				}
				return ResetDisposition::CommittedDegraded;
			}
			barrierCommitted = true;
			if (!OpenLocked(false, a_error)) {
				ReleaseWriterLease();
				if (a_error && a_error->empty())
					*a_error = "managed pack reset committed but the authoritative empty generation could not be reopened";
				else if (a_error)
					*a_error = "managed pack reset committed but reopen failed: " + *a_error;
				return ResetDisposition::CommittedDegraded;
			}

			// The reset is already durable and authoritative. Clearing the superseded
			// file is cleanup only; a failure must not invalidate the new empty store.
			ScannedFile oldGeneration = fallback;
			std::string cleanupError;
			auto retainAuthoritativeReset = [&]() noexcept {
				opened = active.valid;
				fallback.valid = false;
				fallback.generation = 0;
				fallback.fileSize = 0;
				fallback.validSize = 0;
				fallback.nextSequence = 1;
				fallback.diagnostic.clear();
				fallback.records.clear();
				exactIndex.clear();
				recordsByLogical.clear();
				liveByLogical.clear();
				activeLiveByLogical.clear();
				stats = {
					.available = opened,
					.activeGeneration = active.generation,
					.totalBytes = active.fileSize,
					.corruptTailBytes = active.fileSize - active.validSize,
				};
			};
			auto reportCleanupFailure = [&](std::string_view a_detail) noexcept {
				retainAuthoritativeReset();
				try {
#ifdef CSX_SHADER_CACHE_PACK_TESTING
					if (ConsumeTestFailurePoint(TestFailurePoint::ThrowDuringResetCleanupDiagnostic))
						throw std::runtime_error("injected exception during shader pack reset cleanup diagnostic");
#endif
					SetError(a_error, std::format(
										  "managed pack reset committed; superseded generation cleanup failed{}{}",
										  a_detail.empty() ? "" : ": ",
										  a_detail));
				} catch (...) {
					try {
						SetError(a_error, "managed pack reset committed; superseded generation cleanup failed; detailed diagnostic unavailable");
					} catch (...) {
					}
				}
				return ResetDisposition::CommittedDegraded;
			};
			InitializeProgress cleanupProgress = InitializeProgress::Unchanged;
			try {
				if (!InitializeEmpty(
						oldGeneration,
						active.generation - 1,
						&cleanupError,
						&cleanupProgress,
						InitializePurpose::ResetCleanup))
					return reportCleanupFailure(cleanupError);
			} catch (const std::exception& e) {
				return reportCleanupFailure(e.what());
			} catch (...) {
				return reportCleanupFailure("unknown cleanup exception");
			}
			bool finalReopened = false;
#ifdef CSX_SHADER_CACHE_PACK_TESTING
			if (ConsumeTestFailurePoint(TestFailurePoint::BeforeFinalResetReopen)) {
				InvalidateStateLocked();
				SetError(a_error, "injected failure before final shader pack reset reopen");
			} else
#endif
			{
				finalReopened = OpenLocked(false, a_error);
			}
			if (!finalReopened) {
				ReleaseWriterLease();
				if (a_error && a_error->empty())
					*a_error = "managed pack reset committed but final reopen failed";
				else if (a_error)
					*a_error = "managed pack reset committed but final reopen failed: " + *a_error;
				return ResetDisposition::CommittedDegraded;
			}
			return ResetDisposition::Complete;
		} catch (const std::exception& e) {
			const bool changedOrCommitted = barrierCommitted || resetProgress != InitializeProgress::Unchanged;
			if (lock.owns_lock()) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error, std::format(
									  "managed pack reset {} after an exception: {}",
									  barrierCommitted || resetProgress == InitializeProgress::Durable ?
										  "barrier committed" :
									  changedOrCommitted ? "mutation is commit-uncertain" :
														   "failed before mutation",
									  e.what()));
			} catch (...) {
			}
			return changedOrCommitted ? ResetDisposition::CommittedDegraded : ResetDisposition::FailedBeforeCommit;
		} catch (...) {
			const bool changedOrCommitted = barrierCommitted || resetProgress != InitializeProgress::Unchanged;
			if (lock.owns_lock()) {
				InvalidateStateLocked();
				ReleaseWriterLease();
			}
			try {
				SetError(a_error,
					barrierCommitted || resetProgress == InitializeProgress::Durable ?
						"managed pack reset barrier committed before an unknown exception" :
					changedOrCommitted ?
						"managed pack reset mutation is commit-uncertain after an unknown exception" :
						"unknown shader pack reset failure before mutation");
			} catch (...) {
			}
			return changedOrCommitted ? ResetDisposition::CommittedDegraded : ResetDisposition::FailedBeforeCommit;
		}
	}
}
