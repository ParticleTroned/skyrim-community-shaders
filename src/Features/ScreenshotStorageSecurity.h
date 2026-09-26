#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace CSX::ScreenshotStorage
{
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
		static std::shared_ptr<DirectoryLease> CreateExclusive(
			const std::filesystem::path& a_destination,
			std::string_view a_requestId);

		~DirectoryLease();

		DirectoryLease(const DirectoryLease&) = delete;
		DirectoryLease& operator=(const DirectoryLease&) = delete;

		const std::filesystem::path& Path() const noexcept { return path; }
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
	};
}
