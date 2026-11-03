#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace CSX::ScreenshotStorage
{
#ifdef CSX_SCREENSHOT_STORAGE_TESTING
	using DirectoryCreationTestHook = void (*)(const std::filesystem::path&);
	void SetDirectoryCreationTestHook(DirectoryCreationTestHook a_hook) noexcept;
	void SetDestinationOpeningTestHook(DirectoryCreationTestHook a_hook) noexcept;
#endif

	struct CommittedArtifact
	{
		std::uint64_t bytes = 0;
		std::string sha256;
	};

	/** Holds a committed regular file stable while its size and digest are read. */
	class CommittedFile final
	{
	public:
		static CommittedFile Open(const std::filesystem::path& a_path);
		static CommittedArtifact WriteAtomically(
			const std::filesystem::path& a_temporaryPath,
			const std::filesystem::path& a_destination,
			const void* a_data,
			std::size_t a_size,
			bool a_replaceExisting);

		CommittedFile(CommittedFile&& a_other) noexcept;
		CommittedFile& operator=(CommittedFile&& a_other) noexcept;
		~CommittedFile();

		CommittedFile(const CommittedFile&) = delete;
		CommittedFile& operator=(const CommittedFile&) = delete;

		CommittedArtifact Describe() const;

	private:
		explicit CommittedFile(void* a_handle, std::filesystem::path a_path);
		void Release() noexcept;

		void* handle = nullptr;
		std::filesystem::path path;
	};

	/**
	 * Owns an exclusively-created sequence directory and prevents its path from
	 * being renamed or replaced until all frame and manifest writes finish.
	 */
	class DirectoryLease final
	{
	public:
		/** Restricted destinations traverse from a retained root without reparse points. */
		static std::shared_ptr<DirectoryLease> CreateExclusive(
			const std::filesystem::path& a_destination,
			std::string_view a_requestId,
			const std::filesystem::path& a_approvedRoot = {});

		~DirectoryLease();

		DirectoryLease(const DirectoryLease&) = delete;
		DirectoryLease& operator=(const DirectoryLease&) = delete;

		const std::filesystem::path& Path() const noexcept { return path; }
		/** Return the parent path obtained from the retained directory handle. */
		const std::filesystem::path& Destination() const noexcept { return destination; }
		void Verify() const;
		void VerifyDirectChild(const std::filesystem::path& a_path) const;

	private:
		DirectoryLease(
			void* a_destinationHandle,
			void* a_directoryHandle,
			std::filesystem::path a_destination,
			std::filesystem::path a_path,
			std::string a_destinationIdentity,
			std::string a_directoryIdentity);

		void* destinationHandle = nullptr;
		void* directoryHandle = nullptr;
		std::filesystem::path destination;
		std::filesystem::path path;
		std::string destinationIdentity;
		std::string directoryIdentity;
		std::vector<void*> protectedAncestors;
	};
}
