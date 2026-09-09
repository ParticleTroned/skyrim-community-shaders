#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <ranges>
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

	enum class ResetDisposition
	{
		Complete,
		CommittedDegraded,
		FailedBeforeCommit
	};

	enum class LayoutState
	{
		Absent,
		PartialOrInvalid,
		Complete
	};

	constexpr LayoutState ClassifyLayoutMembers(const std::array<bool, 5>& a_present)
	{
		const auto presentCount = std::ranges::count(a_present, true);
		if (presentCount == 0)
			return LayoutState::Absent;
		if (static_cast<std::size_t>(presentCount) == a_present.size())
			return LayoutState::Complete;
		return LayoutState::PartialOrInvalid;
	}

	constexpr LayoutState ClassifyValidatedLayout(
		LayoutState a_memberState,
		bool a_optimizedValid,
		bool a_developerValid)
	{
		if (a_memberState == LayoutState::Absent)
			return LayoutState::Absent;
		return a_memberState == LayoutState::Complete && a_optimizedValid && a_developerValid ?
		           LayoutState::Complete :
		           LayoutState::PartialOrInvalid;
	}

	struct ManifestContract
	{
		struct FileBaseline
		{
			Lane lane = Lane::Optimized;
			std::uint64_t generation = 0;
			std::uint64_t recordCount = 0;
		};

		PackSetId packSetId{};
		std::uint64_t optimizedRecordCount = 0;
		std::uint64_t developerRecordCount = 0;
		std::array<FileBaseline, 4> files{};
	};

	struct PackFileState
	{
		PackSetId packSetId{};
		Lane lane = Lane::Optimized;
		bool valid = false;
		std::uint64_t generation = 0;
		std::uint64_t recordCount = 0;
	};

	bool IsValidPackSetId(const PackSetId& a_packSetId);
	std::optional<ManifestContract> ParseManifestContract(
		const nlohmann::json& a_manifest,
		std::string_view a_expectedRuntime,
		std::string* a_error = nullptr);
	bool ValidateManifestFileStates(
		const ManifestContract& a_contract,
		const std::array<PackFileState, 4>& a_files,
		std::string* a_error = nullptr);
	bool ValidateDistinctFileIdentities(
		const std::array<std::string, 4>& a_identities,
		std::string* a_error = nullptr);

#ifdef CSX_SHADER_CACHE_PACK_TESTING
	enum class TestFailurePoint : std::uint32_t
	{
		None = 0,
		AfterRegistryInsert = 1u << 0,
		BeforeSecondBootstrapInitialization = 1u << 1,
		DuringBootstrapRollback = 1u << 2,
		AfterFirstBootstrapInitialization = 1u << 3,
		BeforeStoreAdmissionCommit = 1u << 4,
		DuringStoreAdmissionCommit = 1u << 5,
		BeforeFinalResetReopen = 1u << 6,
		BeforeInitializeMutation = 1u << 7,
		AfterInitializeTruncate = 1u << 8,
		ThrowAfterInitializeTruncate = 1u << 9,
		AfterInitializeWrite = 1u << 10,
		ThrowAfterInitializeWrite = 1u << 11,
		AfterInitializeDurableFlush = 1u << 12,
		ThrowAfterInitializeDurableFlush = 1u << 13,
		ThrowBeforeFirstBootstrapRollback = 1u << 14,
		ThrowBetweenBootstrapRollbackMembers = 1u << 15,
		ThrowDuringBootstrapRollbackDiagnostic = 1u << 16,
		DuringStoreIndexPublication = 1u << 17,
		DuringCompactionCopy = 1u << 18,
		BeforeResetCleanupMutation = 1u << 19,
		AfterResetCleanupTruncate = 1u << 20,
		ThrowAfterResetCleanupTruncate = 1u << 21,
		AfterResetCleanupWrite = 1u << 22,
		ThrowAfterResetCleanupWrite = 1u << 23,
		AfterResetCleanupDurableFlush = 1u << 24,
		ThrowAfterResetCleanupDurableFlush = 1u << 25,
		ThrowBeforeResetCleanupVerification = 1u << 26,
		ThrowDuringResetCleanupDiagnostic = 1u << 27
	};
	void SetTestFailurePoints(std::uint32_t a_failurePoints);
#endif

	// Any installed managed-pack member suppresses loose persistence. A complete
	// set serves compatible records; a partial set fails closed to source so a
	// damaged installation cannot silently regenerate thousands of loose files.
	constexpr bool ShouldReadLooseBlob(bool a_diskCacheEnabled, bool a_managedPackPresent)
	{
		return a_diskCacheEnabled && !a_managedPackPresent;
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
	 * runtime never creates or initializes a new MO2/VFS file. The isolated
	 * bootstrap entry point may initialize an existing empty pair. A committed
	 * record is visible only when its trailer and payload hash validate. Invalid
	 * tail bytes are ignored and truncated before the next append.
	 */
	class Store
	{
	public:
		Store(
			std::filesystem::path a_pathA,
			std::filesystem::path a_pathB,
			Lane a_lane,
			PackSetId a_packSetId);
		~Store();

		Store(const Store&) = delete;
		Store& operator=(const Store&) = delete;

		bool Open(std::string* a_error = nullptr);
		/**
		 * Explicitly bootstrap an isolated pair whose existing members are both
		 * zero-byte files. Runtime managed-layout admission must use Open(), which
		 * is strictly non-mutating.
		 */
		bool InitializeEmptyFilesAndOpen(std::string* a_error = nullptr);
		void Close();
		std::array<PackFileState, 2> GetFileStates() const;
		std::array<std::string, 2> GetFileIdentityKeys() const;
		std::optional<Entry> Find(std::string_view a_exactKey, std::string* a_error = nullptr) const;
		/** Returns the newest retained record whose metadata satisfies the caller's compatibility contract. */
		std::optional<Entry> FindCompatible(
			std::string_view a_logicalKey,
			const std::function<bool(std::string_view)>& a_acceptMetadata,
			std::string* a_error = nullptr) const;
		bool Append(const Entry& a_entry, std::string* a_error = nullptr);
		/** Durably commits all records appended since the previous checkpoint. */
		bool Checkpoint(std::string* a_error = nullptr);
		Stats GetStats() const;
		bool ShouldCompact(double a_minimumFragmentation = 0.30, std::uint64_t a_minimumSupersededBytes = 32ull * 1024ull * 1024ull) const;
		bool Compact(std::string* a_error = nullptr);
		ResetDisposition Reset(std::string* a_error = nullptr);

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
		std::string leaseKey;
		std::array<std::string, 2> fileIdentityKeys{};
		bool leaseOwned = false;
		bool processRegistryOwned = false;
#ifdef _WIN32
		void* leaseHandle = nullptr;
		std::array<void*, 2> fileIdentityHandles{};
		std::vector<void*> pathGuardHandles;
#endif
		ScannedFile active;
		ScannedFile fallback;
		std::unordered_map<std::string, RecordLocation> exactIndex;
		std::unordered_map<std::string, std::vector<RecordLocation>> recordsByLogical;
		std::unordered_map<std::string, RecordLocation> liveByLogical;
		std::unordered_map<std::string, RecordLocation> activeLiveByLogical;
		Stats stats;
		enum class InitializeProgress
		{
			Unchanged,
			MutationStarted,
			Durable,
			Verified
		};
		enum class InitializePurpose
		{
			General,
			ResetCleanup
		};

		bool OpenLocked(bool a_allowEmptyInitialization, std::string* a_error);
		bool AcquireWriterLease(std::string* a_error);
		void ReleaseWriterLease() noexcept;
		void InvalidateStateLocked() noexcept;
		bool Scan(const std::filesystem::path& a_path, ScannedFile& a_output, std::string* a_error) const;
		bool InitializeEmpty(
			ScannedFile& a_file,
			std::uint64_t a_generation,
			std::string* a_error,
			InitializeProgress* a_progress = nullptr,
			InitializePurpose a_purpose = InitializePurpose::General) const;
		bool AppendLocked(ScannedFile& a_file, const Entry& a_entry, std::uint64_t a_sequence, bool a_checkpoint, std::string* a_error) const;
		std::optional<Entry> Read(const RecordLocation& a_location, std::string* a_error) const;
		void RebuildIndexes();
	};
}
