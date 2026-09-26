#include "Features/ScreenshotStorageSecurity.h"

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

namespace
{
	bool IsLeaseConflict(DWORD a_error)
	{
		return a_error == ERROR_SHARING_VIOLATION || a_error == ERROR_ACCESS_DENIED;
	}

	void ExpectFailure(auto&& a_operation, const char* a_message)
	{
		try {
			a_operation();
		} catch (const std::exception&) {
			return;
		}
		throw std::runtime_error(a_message);
	}
}

inline void RunScreenshotStorageSecurityTests()
{
	using CSX::ScreenshotStorage::CommittedFile;
	using CSX::ScreenshotStorage::DirectoryLease;

	const auto root = std::filesystem::temp_directory_path() /
	                  std::format("csx-screenshot-storage-{}-{}", GetCurrentProcessId(), GetTickCount64());
	std::filesystem::create_directories(root);
	try {
		const std::string requestId = "12345678-1234-1234-1234-123456789abc";
		auto directory = DirectoryLease::CreateExclusive(root, requestId);
		if (directory->Path().filename() != "CS_sequence_12345678-1234-1234-1234-123456789abc")
			throw std::runtime_error("sequence directory did not preserve the full request identity");
		ExpectFailure(
			[&] { DirectoryLease::CreateExclusive(root, requestId); },
			"a forced sequence directory collision was accepted");

		const auto renamed = root / "renamed-sequence";
		if (MoveFileExW(directory->Path().c_str(), renamed.c_str(), 0) || !IsLeaseConflict(GetLastError()))
			throw std::runtime_error("active sequence directory could be renamed between frame writes");
		directory->VerifyDirectChild(directory->Path() / "frame_000001.bmp");
		if (MoveFileExW(directory->Path().c_str(), renamed.c_str(), 0) || !IsLeaseConflict(GetLastError()))
			throw std::runtime_error("active sequence directory could be renamed before final manifest commit");

		const auto artifactPath = directory->Path() / "frame_000001.bmp";
		{
			{
				std::ofstream artifact(artifactPath, std::ios::binary);
				artifact << "abc";
			}
			auto committed = CommittedFile::Open(artifactPath);
			const HANDLE writer = CreateFileW(
				artifactPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (writer != INVALID_HANDLE_VALUE) {
				CloseHandle(writer);
				throw std::runtime_error("committed artifact remained writable during integrity verification");
			}
			const auto replacement = directory->Path() / "replacement.bmp";
			{
				std::ofstream replacementStream(replacement, std::ios::binary);
				replacementStream << "replacement";
			}
			if (MoveFileExW(replacement.c_str(), artifactPath.c_str(), MOVEFILE_REPLACE_EXISTING) ||
				!IsLeaseConflict(GetLastError())) {
				throw std::runtime_error("committed artifact could be replaced during integrity verification");
			}
			const auto description = committed.Describe();
			if (description.bytes != 3 ||
				description.sha256 != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
				throw std::runtime_error("same-handle artifact size or digest is incorrect");
			}
			auto moved = std::move(committed);
			ExpectFailure(
				[&] { committed.Describe(); },
				"an unavailable committed-file handle published integrity metadata");
			ExpectFailure(
				[&] { CommittedFile::Open(directory->Path()); },
				"a directory was accepted as a committed regular artifact");
		}
		directory.reset();

		const auto junctionTarget = root / "junction-target";
		const auto junctionPath = root / "CS_sequence_junction-request";
		std::filesystem::create_directories(junctionTarget);
		const auto command = std::format(
			L"cmd.exe /d /c mklink /J \"{}\" \"{}\" >nul",
			junctionPath.native(), junctionTarget.native());
		if (_wsystem(command.c_str()) != 0)
			throw std::runtime_error("could not create the pre-admission junction test fixture");
		ExpectFailure(
			[&] { DirectoryLease::CreateExclusive(root, "junction-request"); },
			"a pre-created sequence junction was accepted");
		RemoveDirectoryW(junctionPath.c_str());

		std::filesystem::remove_all(root);
	} catch (...) {
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
		throw;
	}
}
