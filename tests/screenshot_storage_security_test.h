// The fixtures share the production translation unit to inspect private path normalization.
#include "Features/ScreenshotStorageSecurity.cpp"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <stdexcept>

namespace
{
	std::filesystem::path g_creationReplacement;
	std::filesystem::path g_creationDisplaced;
	std::atomic_bool g_creationHookCalled{ false };
	std::mutex g_preparationMutex;
	std::condition_variable g_preparationCondition;
	bool g_preparationEntered = false;
	bool g_preparationReleased = false;
	std::filesystem::path g_destinationParent;
	std::filesystem::path g_destinationDisplaced;
	std::filesystem::path g_destinationOutside;
	bool g_destinationHookCalled = false;

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

	void AttemptCreationSubstitution(const std::filesystem::path& a_created)
	{
		g_creationHookCalled.store(true, std::memory_order_release);
		if (MoveFileExW(a_created.c_str(), g_creationDisplaced.c_str(), 0) ||
			!IsLeaseConflict(GetLastError())) {
			throw std::runtime_error("atomically created sequence directory could be displaced before adoption");
		}
		if (MoveFileExW(
				g_creationReplacement.c_str(), a_created.c_str(), MOVEFILE_REPLACE_EXISTING) ||
			!IsLeaseConflict(GetLastError())) {
			throw std::runtime_error("replacement directory could be installed before lease adoption");
		}
	}

	void BlockStoragePreparation(const std::filesystem::path&)
	{
		std::unique_lock lock(g_preparationMutex);
		g_preparationEntered = true;
		g_preparationCondition.notify_all();
		g_preparationCondition.wait(lock, [] { return g_preparationReleased; });
	}

	void SubstituteDestinationParent(const std::filesystem::path&)
	{
		g_destinationHookCalled = true;
		if (!MoveFileExW(g_destinationParent.c_str(), g_destinationDisplaced.c_str(), 0))
			throw std::runtime_error("could not displace the destination parent race fixture");
		const auto command = std::format(L"cmd.exe /d /c mklink /J \"{}\" \"{}\" >nul",
			g_destinationParent.native(), g_destinationOutside.native());
		if (_wsystem(command.c_str()) != 0)
			throw std::runtime_error("could not install the destination parent substitution fixture");
	}
}

inline void RunScreenshotStorageSecurityTests()
{
	using CSX::ScreenshotStorage::CommittedFile;
	using CSX::ScreenshotStorage::DirectoryLease;
	using CSX::ScreenshotStorage::NormalizeFinalPath;
	using CSX::ScreenshotStorage::SetDestinationOpeningTestHook;
	using CSX::ScreenshotStorage::SetDirectoryCreationTestHook;
	if (NormalizeFinalPath(LR"(\\?\UNC\server\share\folder)") !=
			std::filesystem::path(LR"(\\server\share\folder)") ||
		NormalizeFinalPath(LR"(\\?\C:\folder)") !=
			std::filesystem::path(LR"(C:\folder)"))
		throw std::runtime_error("extended Windows paths were not normalized");

	const auto root = std::filesystem::temp_directory_path() /
	                  std::format("csx-screenshot-storage-{}-{}", GetCurrentProcessId(), GetTickCount64());
	std::filesystem::create_directories(root);
	try {
		const auto approved = root / "approved";
		std::filesystem::create_directories(approved);
		const auto nested = approved / "stable" / "parent";
		auto restricted = DirectoryLease::CreateExclusive(nested, "restricted", approved);
		if (restricted->Destination() != nested || restricted->Path().parent_path() != nested)
			throw std::runtime_error("restricted destination receipt did not bind the actual retained parent");
		for (const auto& protectedPath : { approved, nested.parent_path(), nested }) {
			if (MoveFileExW(protectedPath.c_str(), (root / "displaced-protected").c_str(), 0) ||
				!IsLeaseConflict(GetLastError()))
				throw std::runtime_error("approved ancestry could be displaced during publication");
			const HANDLE writer = CreateFileW(protectedPath.c_str(), GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (writer != INVALID_HANDLE_VALUE) {
				CloseHandle(writer);
				throw std::runtime_error("approved ancestry allowed reparse mutation during publication");
			}
		}
		restricted->Verify();
		const auto restrictedArtifact = restricted->Path() / "restricted.bmp";
		const auto restrictedCommit = CommittedFile::WriteAtomically(
			restrictedArtifact.native() + L".tmp", restrictedArtifact, "abc", 3, false);
		if (restrictedCommit.bytes != 3 ||
			restrictedCommit.sha256 != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
			throw std::runtime_error("restricted ancestry prevented atomic artifact publication");
		restricted->Verify();
		restricted.reset();
		g_destinationParent = approved / "substituted";
		g_destinationDisplaced = approved / "original-parent";
		g_destinationOutside = root / "outside";
		std::filesystem::create_directories(g_destinationParent);
		std::filesystem::create_directories(g_destinationOutside);
		const auto outsideSentinel = g_destinationOutside / "sentinel.txt";
		{
			std::ofstream sentinel(outsideSentinel, std::ios::binary);
			sentinel << "outside-owned";
		}
		g_destinationHookCalled = false;
		SetDestinationOpeningTestHook(&SubstituteDestinationParent);
		ExpectFailure([&] {
			DirectoryLease::CreateExclusive(g_destinationParent / "inner", "parent-race", approved);
		},
			"a substituted intermediate parent escaped its approved root");
		SetDestinationOpeningTestHook(nullptr);
		if (!g_destinationHookCalled || !std::filesystem::exists(g_destinationDisplaced) ||
			std::filesystem::exists(g_destinationOutside / "inner") ||
			std::filesystem::exists(g_destinationOutside / "CS_sequence_parent-race"))
			throw std::runtime_error("parent substitution created storage outside the approved root");
		std::ifstream sentinel(outsideSentinel, std::ios::binary);
		const std::string sentinelBytes((std::istreambuf_iterator<char>(sentinel)), {});
		if (sentinelBytes != "outside-owned")
			throw std::runtime_error("parent substitution changed outside-owned contents");
		sentinel.close();
		if (!RemoveDirectoryW(g_destinationParent.c_str()))
			throw std::runtime_error("could not retire the parent substitution junction fixture");
		ExpectFailure([&] {
			DirectoryLease::CreateExclusive(g_destinationOutside, "outside-rejected", approved);
		},
			"an explicitly outside destination passed restricted containment");
		if (std::filesystem::exists(g_destinationOutside / "CS_sequence_outside-rejected"))
			throw std::runtime_error("restricted containment failure created a sequence");
		auto absolute = DirectoryLease::CreateExclusive(g_destinationOutside, "absolute-permitted");
		absolute->Verify();
		absolute.reset();
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
		const auto atomicPath = directory->Path() / "atomic.bmp";
		const auto atomicTemporary = directory->Path() / "atomic.bmp.tmp";
		const std::string atomicBytes = "producer-owned";
		const auto atomicDescription = CommittedFile::WriteAtomically(
			atomicTemporary, atomicPath, atomicBytes.data(), atomicBytes.size(), false);
		if (atomicDescription.bytes != atomicBytes.size() ||
			atomicDescription.sha256 != "3d829da6e6186451accbb64d80461f66f178170700528ed530119316c840871e") {
			throw std::runtime_error("producer-owned atomic commit returned incorrect integrity metadata");
		}
		if (std::filesystem::exists(atomicTemporary))
			throw std::runtime_error("producer-owned atomic commit left its temporary path behind");
		for (int index = 0; index < 64; ++index) {
			const auto repeatedPath = directory->Path() /
			                          (std::wstring(index % 24, L'x') + std::format(L"-publication-{}-\u6c34.png", index));
			const auto repeated = CommittedFile::WriteAtomically(
				repeatedPath.native() + L".tmp", repeatedPath,
				atomicBytes.data(), atomicBytes.size(), false);
			const auto reopened = CommittedFile::Open(repeatedPath).Describe();
			if (repeated.bytes != atomicDescription.bytes || repeated.sha256 != atomicDescription.sha256 ||
				reopened.bytes != repeated.bytes || reopened.sha256 != repeated.sha256)
				throw std::runtime_error("repeated Unicode publication did not preserve the requested destination and bytes");
		}
		{
			std::ofstream occupied(atomicTemporary, std::ios::binary);
			occupied << "attacker";
		}
		ExpectFailure(
			[&] { CommittedFile::WriteAtomically(
					  atomicTemporary, directory->Path() / "blocked.bmp",
					  atomicBytes.data(), atomicBytes.size(), false); },
			"a pre-replaced producer temporary file was accepted");
		std::filesystem::remove(atomicTemporary);
		ExpectFailure(
			[&] { CommittedFile::WriteAtomically(
					  directory->Path() / "collision.tmp", atomicPath,
					  atomicBytes.data(), atomicBytes.size(), false); },
			"an existing final artifact was replaced without authority");
		if (std::filesystem::exists(directory->Path() / "collision.tmp"))
			throw std::runtime_error("failed no-replace commit left its temporary path behind");
		const auto replaced = CommittedFile::WriteAtomically(
			atomicTemporary, atomicPath, "abc", 3, true);
		const auto reopenedReplacement = CommittedFile::Open(atomicPath).Describe();
		if (replaced.bytes != 3 ||
			replaced.sha256 != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ||
			reopenedReplacement.bytes != replaced.bytes || reopenedReplacement.sha256 != replaced.sha256 ||
			std::filesystem::exists(atomicTemporary))
			throw std::runtime_error("authorized atomic replacement did not preserve the new payload");

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
		g_creationReplacement = root / "attacker-replacement";
		g_creationDisplaced = root / "attacker-displaced";
		std::filesystem::create_directories(g_creationReplacement);
		const auto replacementSentinel = g_creationReplacement / "sentinel.txt";
		{
			std::ofstream sentinel(replacementSentinel, std::ios::binary);
			sentinel << "attacker-owned";
		}
		g_creationHookCalled.store(false, std::memory_order_release);
		SetDirectoryCreationTestHook(&AttemptCreationSubstitution);
		auto atomicDirectory = DirectoryLease::CreateExclusive(root, "atomic-creation");
		SetDirectoryCreationTestHook(nullptr);
		if (!g_creationHookCalled.load(std::memory_order_acquire) ||
			!std::filesystem::exists(replacementSentinel) ||
			std::filesystem::exists(atomicDirectory->Path() / "sentinel.txt") ||
			std::filesystem::exists(g_creationDisplaced)) {
			throw std::runtime_error("directory creation custody adopted or changed attacker-owned contents");
		}
		atomicDirectory.reset();
		g_preparationEntered = false;
		g_preparationReleased = false;
		SetDirectoryCreationTestHook(&BlockStoragePreparation);
		auto preparation = std::async(std::launch::async, [&] {
			return DirectoryLease::CreateExclusive(root, "asynchronous-preparation");
		});
		{
			std::unique_lock lock(g_preparationMutex);
			if (!g_preparationCondition.wait_for(lock, std::chrono::seconds(2), [] {
					return g_preparationEntered;
				})) {
				g_preparationReleased = true;
				lock.unlock();
				g_preparationCondition.notify_all();
				preparation.wait();
				throw std::runtime_error("storage preparation did not reach its controlled boundary");
			}
		}
		std::atomic_uint32_t callerProgress{ 0 };
		++callerProgress;
		if (preparation.wait_for(std::chrono::milliseconds(0)) != std::future_status::timeout ||
			callerProgress.load() != 1) {
			throw std::runtime_error("a blocked storage boundary also blocked caller progress");
		}
		{
			std::lock_guard lock(g_preparationMutex);
			g_preparationReleased = true;
		}
		g_preparationCondition.notify_all();
		auto asynchronouslyPrepared = preparation.get();
		SetDirectoryCreationTestHook(nullptr);
		asynchronouslyPrepared->Verify();
		asynchronouslyPrepared.reset();
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
		SetDirectoryCreationTestHook(nullptr);
		SetDestinationOpeningTestHook(nullptr);
		{
			std::lock_guard lock(g_preparationMutex);
			g_preparationReleased = true;
		}
		g_preparationCondition.notify_all();
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
		throw;
	}
}
