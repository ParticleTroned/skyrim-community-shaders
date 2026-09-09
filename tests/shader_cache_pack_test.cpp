#include "Utils/ShaderCachePack.h"

#include "Utils/CryptoHash.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>

#ifdef _WIN32
#	include <Windows.h>
#endif

using namespace Util::ShaderCachePack;

namespace
{
	PackSetId TestPackSetId()
	{
		PackSetId result{};
		result.front() = std::byte{ 0x53 };
		return result;
	}

	PackSetId ParsePackSetId(std::string_view a_value)
	{
		assert(a_value.size() == 32);
		PackSetId result{};
		for (std::size_t index = 0; index < result.size(); ++index) {
			auto nibble = [](char a_character) -> std::uint8_t {
				if (a_character >= '0' && a_character <= '9')
					return static_cast<std::uint8_t>(a_character - '0');
				assert(a_character >= 'a' && a_character <= 'f');
				return static_cast<std::uint8_t>(a_character - 'a' + 10);
			};
			result[index] = static_cast<std::byte>((nibble(a_value[index * 2]) << 4) | nibble(a_value[index * 2 + 1]));
		}
		return result;
	}

	nlohmann::json MakePackManifest()
	{
		return {
			{ "schema", "csx.shader-cache.pack-manifest" },
			{ "schemaVersion", 2 },
			{ "formatVersion", 1 },
			{ "fileStateSemantics", "installation-baseline-v1" },
			{ "hashAlgorithm", "sha256" },
			{ "packSetId", "53000000000000000000000000000000" },
			{ "runtime", "VR" },
			{ "shaderCacheABI", "test-abi" },
			{ "optimizedRecordCount", 3 },
			{ "developerRecordCount", 0 },
			{ "compatibilityVariants", nlohmann::json::array({ "default" }) },
			{ "files",
				{
					{ "Optimized.A.csxpack", { { "lane", 1 }, { "generation", 1 }, { "recordCount", 3 } } },
					{ "Optimized.B.csxpack", { { "lane", 1 }, { "generation", 0 }, { "recordCount", 0 } } },
					{ "Developer.A.csxpack", { { "lane", 2 }, { "generation", 1 }, { "recordCount", 0 } } },
					{ "Developer.B.csxpack", { { "lane", 2 }, { "generation", 0 }, { "recordCount", 0 } } },
				} },
		};
	}

	bool ValidateOptimizedStore(
		const Store& a_store,
		const ManifestContract& a_contract,
		std::string* a_error)
	{
		const auto optimized = a_store.GetFileStates();
		std::array<PackFileState, 4> states{
			optimized[0],
			optimized[1],
			PackFileState{ a_contract.packSetId, Lane::Developer, true, a_contract.files[2].generation, a_contract.files[2].recordCount },
			PackFileState{ a_contract.packSetId, Lane::Developer, true, a_contract.files[3].generation, a_contract.files[3].recordCount },
		};
		return ValidateManifestFileStates(a_contract, states, a_error);
	}

#ifdef _WIN32
	std::optional<std::wstring> ReadEnvironmentVariable(const wchar_t* a_name)
	{
		const DWORD required = GetEnvironmentVariableW(a_name, nullptr, 0);
		if (required == 0)
			return std::nullopt;
		std::wstring value(required, L'\0');
		const DWORD written = GetEnvironmentVariableW(a_name, value.data(), required);
		assert(written + 1 == required);
		value.resize(written);
		return value;
	}

	void RestoreEnvironmentVariable(const wchar_t* a_name, const std::optional<std::wstring>& a_value)
	{
		assert(SetEnvironmentVariableW(a_name, a_value ? a_value->c_str() : nullptr));
	}

	int RunLeaseProbe(
		const std::filesystem::path& a_executable,
		const std::filesystem::path& a_first,
		const std::filesystem::path& a_second,
		bool a_expectOpen,
		const std::filesystem::path& a_childTemp = {})
	{
		auto quote = [](const std::filesystem::path& a_value) {
			return L"\"" + a_value.wstring() + L"\"";
		};
		std::wstring command =
			quote(a_executable) + L" --lease-probe " + quote(a_first) + L" " + quote(a_second) +
			(a_expectOpen ? L" open" : L" locked");
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const auto originalTemp = ReadEnvironmentVariable(L"TEMP");
		const auto originalTmp = ReadEnvironmentVariable(L"TMP");
		if (!a_childTemp.empty()) {
			std::filesystem::create_directories(a_childTemp);
			assert(SetEnvironmentVariableW(L"TEMP", a_childTemp.c_str()));
			assert(SetEnvironmentVariableW(L"TMP", a_childTemp.c_str()));
		}
		assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));
		RestoreEnvironmentVariable(L"TEMP", originalTemp);
		RestoreEnvironmentVariable(L"TMP", originalTmp);
		assert(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0);
		DWORD exitCode = 1;
		assert(GetExitCodeProcess(process.hProcess, &exitCode));
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return static_cast<int>(exitCode);
	}

	int RunLeaseMutationProbe(
		const std::filesystem::path& a_executable,
		const std::filesystem::path& a_first,
		const std::filesystem::path& a_second,
		std::string_view a_operation,
		const std::filesystem::path& a_childTemp)
	{
		auto quote = [](const std::filesystem::path& a_value) {
			return L"\"" + a_value.wstring() + L"\"";
		};
		const std::wstring operation(a_operation.begin(), a_operation.end());
		std::wstring command = quote(a_executable) + L" --lease-mutation-probe " +
		                       quote(a_first) + L" " + quote(a_second) + L" " + operation;
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const auto originalTemp = ReadEnvironmentVariable(L"TEMP");
		const auto originalTmp = ReadEnvironmentVariable(L"TMP");
		std::filesystem::create_directories(a_childTemp);
		assert(SetEnvironmentVariableW(L"TEMP", a_childTemp.c_str()));
		assert(SetEnvironmentVariableW(L"TMP", a_childTemp.c_str()));
		assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));
		RestoreEnvironmentVariable(L"TEMP", originalTemp);
		RestoreEnvironmentVariable(L"TMP", originalTmp);
		assert(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0);
		DWORD exitCode = 1;
		assert(GetExitCodeProcess(process.hProcess, &exitCode));
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return static_cast<int>(exitCode);
	}

	void TerminateLeaseHolder(
		const std::filesystem::path& a_executable,
		const std::filesystem::path& a_first,
		const std::filesystem::path& a_second,
		const std::filesystem::path& a_ready,
		const std::filesystem::path& a_childTemp = {})
	{
		auto quote = [](const std::filesystem::path& a_value) {
			return L"\"" + a_value.wstring() + L"\"";
		};
		std::wstring command =
			quote(a_executable) + L" --lease-hold " + quote(a_first) + L" " + quote(a_second) + L" " + quote(a_ready);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const auto originalTemp = ReadEnvironmentVariable(L"TEMP");
		const auto originalTmp = ReadEnvironmentVariable(L"TMP");
		if (!a_childTemp.empty()) {
			std::filesystem::create_directories(a_childTemp);
			assert(SetEnvironmentVariableW(L"TEMP", a_childTemp.c_str()));
			assert(SetEnvironmentVariableW(L"TMP", a_childTemp.c_str()));
		}
		assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));
		RestoreEnvironmentVariable(L"TEMP", originalTemp);
		RestoreEnvironmentVariable(L"TMP", originalTmp);
		for (unsigned attempt = 0; attempt < 500 && !std::filesystem::exists(a_ready); ++attempt)
			Sleep(10);
		assert(std::filesystem::exists(a_ready));
		assert(TerminateProcess(process.hProcess, 91));
		assert(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}
#endif

	Entry MakeEntry(std::string a_logical, std::string a_exact, std::uint8_t a_value, std::size_t a_size = 64)
	{
		Entry entry{ std::move(a_logical), std::move(a_exact), "{\"schema\":1}", {} };
		entry.bytecode.assign(a_size, static_cast<std::byte>(a_value));
		return entry;
	}

	std::filesystem::path ActivePack(const std::filesystem::path& a_first, const std::filesystem::path& a_second)
	{
		return std::filesystem::file_size(a_first) >= std::filesystem::file_size(a_second) ? a_first : a_second;
	}

	template <class T>
	void Overwrite(const std::filesystem::path& a_path, std::uint64_t a_offset, const T& a_value)
	{
		std::fstream stream(a_path, std::ios::binary | std::ios::in | std::ios::out);
		assert(stream);
		stream.seekp(static_cast<std::streamoff>(a_offset));
		stream.write(reinterpret_cast<const char*>(std::addressof(a_value)), sizeof(a_value));
		stream.flush();
		assert(stream);
	}

	void SetPackGeneration(const std::filesystem::path& a_path, std::uint64_t a_generation)
	{
		std::array<std::byte, 80> header{};
		std::fstream stream(a_path, std::ios::binary | std::ios::in | std::ios::out);
		assert(stream.read(reinterpret_cast<char*>(header.data()), header.size()));
		std::memcpy(header.data() + 16, &a_generation, sizeof(a_generation));
		const auto hash = Util::CryptoHash::Sha256Bytes(std::span(header.data(), 48));
		std::memcpy(header.data() + 48, hash.data(), hash.size());
		stream.seekp(0);
		stream.write(reinterpret_cast<const char*>(header.data()), header.size());
		stream.flush();
		assert(stream);
	}
}

int main(int argc, char** argv)
{
	static_assert(!ShouldReadLooseBlob(false, false));
	static_assert(!ShouldReadLooseBlob(false, true));
	static_assert(ShouldReadLooseBlob(true, false));
	static_assert(!ShouldReadLooseBlob(true, true));
	static_assert(ClassifyLayoutMembers({ false, false, false, false, false }) == LayoutState::Absent);
	static_assert(ClassifyLayoutMembers({ true, true, true, true, true }) == LayoutState::Complete);
	static_assert(ClassifyValidatedLayout(LayoutState::Complete, true, true) == LayoutState::Complete);
	static_assert(ClassifyValidatedLayout(LayoutState::Complete, false, true) == LayoutState::PartialOrInvalid);
	static_assert(ClassifyValidatedLayout(LayoutState::Complete, true, false) == LayoutState::PartialOrInvalid);
	static_assert(ClassifyValidatedLayout(LayoutState::PartialOrInvalid, true, true) == LayoutState::PartialOrInvalid);
	{
		std::string identityError;
		assert(ValidateDistinctFileIdentities({ "a", "b", "c", "d" }, &identityError));
		assert(!ValidateDistinctFileIdentities({ "a", "b", "a", "d" }, &identityError));
		assert(!identityError.empty());
	}

	for (std::uint32_t mask = 0; mask < 32; ++mask) {
		std::array<bool, 5> present{};
		for (std::size_t index = 0; index < present.size(); ++index)
			present[index] = (mask & (1u << index)) != 0;
		const auto expected =
			mask == 0  ? LayoutState::Absent :
			mask == 31 ? LayoutState::Complete :
						 LayoutState::PartialOrInvalid;
		assert(ClassifyLayoutMembers(present) == expected);
	}

	if (argc == 5 && std::string_view(argv[1]) == "--lease-probe") {
		std::string error;
		Store external(argv[2], argv[3], Lane::Optimized, TestPackSetId());
		const bool opened = external.Open(&error);
		const bool expected = std::string_view(argv[4]) == "open";
		return opened == expected ? 0 : 1;
	}
	if (argc == 5 && std::string_view(argv[1]) == "--lease-mutation-probe") {
		std::string error;
		Store external(argv[2], argv[3], Lane::Optimized, TestPackSetId());
		const auto operation = std::string_view(argv[4]);
		bool mutationSucceeded = false;
		if (operation == "append")
			mutationSucceeded = external.Append(MakeEntry("blocked", "blocked-exact", 0x7f), &error);
		else if (operation == "checkpoint")
			mutationSucceeded = external.Checkpoint(&error);
		else if (operation == "compact")
			mutationSucceeded = external.Compact(&error);
		else if (operation == "reset")
			mutationSucceeded = external.Reset(&error) != ResetDisposition::FailedBeforeCommit;
		else
			return 2;
		return !mutationSucceeded && !error.empty() ? 0 : 1;
	}
#ifdef _WIN32
	if (argc == 5 && std::string_view(argv[1]) == "--lease-hold") {
		std::string error;
		Store external(argv[2], argv[3], Lane::Optimized, TestPackSetId());
		if (!external.Open(&error))
			return 1;
		std::ofstream(argv[4]).put('1');
		Sleep(30000);
		return 2;
	}
#endif
	if (argc == 5) {
		std::string error;
		Store external(argv[1], argv[2], Lane::Optimized, ParsePackSetId(argv[4]));
		assert(external.Open(&error));
		const auto record = external.Find(argv[3], &error);
		assert(record && !record->bytecode.empty());
		return 0;
	}
	assert(argc == 1);
	const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto root = std::filesystem::temp_directory_path() / ("csx-pack-test-" + unique);
	std::filesystem::create_directories(root);

	// Runtime admission enforces the same strict manifest shape as packaging.
	{
		std::string manifestError;
		const auto valid = MakePackManifest();
		const auto contract = ParseManifestContract(valid, "VR", &manifestError);
		assert(contract);
		assert(contract->files[0].lane == Lane::Optimized);
		assert(contract->files[0].generation == 1);
		assert(contract->files[0].recordCount == 3);
		auto previousSeedAbi = valid;
		previousSeedAbi["shaderCacheABI"] = "previous-build-abi";
		assert(ParseManifestContract(previousSeedAbi, "VR", &manifestError));
		auto expectRejected = [&](nlohmann::json manifest) {
			manifestError.clear();
			assert(!ParseManifestContract(manifest, "VR", &manifestError));
			assert(!manifestError.empty());
		};

		auto zeroIdentity = valid;
		zeroIdentity["packSetId"] = std::string(32, '0');
		expectRejected(std::move(zeroIdentity));
		auto missingHash = valid;
		missingHash.erase("hashAlgorithm");
		expectRejected(std::move(missingHash));
		auto wrongCountType = valid;
		wrongCountType["optimizedRecordCount"] = "3";
		expectRejected(std::move(wrongCountType));
		auto booleanSchemaVersion = valid;
		booleanSchemaVersion["schemaVersion"] = true;
		expectRejected(std::move(booleanSchemaVersion));
		auto floatingFormatVersion = valid;
		floatingFormatVersion["formatVersion"] = 1.0;
		expectRejected(std::move(floatingFormatVersion));
		auto missingFile = valid;
		missingFile["files"].erase("Developer.B.csxpack");
		expectRejected(std::move(missingFile));
		auto wrongLane = valid;
		wrongLane["files"]["Developer.A.csxpack"]["lane"] = 1;
		expectRejected(std::move(wrongLane));
		auto ambiguousGeneration = valid;
		ambiguousGeneration["files"]["Optimized.B.csxpack"]["generation"] = 1;
		expectRejected(std::move(ambiguousGeneration));
		auto wrongAggregate = valid;
		wrongAggregate["optimizedRecordCount"] = 4;
		expectRejected(std::move(wrongAggregate));
		auto duplicateVariant = valid;
		duplicateVariant["compatibilityVariants"].push_back("default");
		expectRejected(std::move(duplicateVariant));
	}

	// One data corpus is evaluated by this runtime-unit test and the Python
	// archive/FOMOD validator test so admission decisions cannot drift silently.
	{
		const auto corpusPath = std::filesystem::path(__FILE__).parent_path() / "data" /
		                        "shader_cache_pack_contract_cases.json";
		std::ifstream corpusStream(corpusPath);
		assert(corpusStream);
		nlohmann::json corpus;
		corpusStream >> corpus;
		assert(corpus["schemaVersion"] == 1);
		auto unescapePointerToken = [](std::string value) {
			for (std::size_t index = 0; (index = value.find("~1", index)) != std::string::npos;)
				value.replace(index, 2, "/");
			for (std::size_t index = 0; (index = value.find("~0", index)) != std::string::npos;)
				value.replace(index, 2, "~");
			return value;
		};
		auto fileIndex = [](std::string_view a_name) -> std::size_t {
			if (a_name == "Optimized.A.csxpack")
				return 0;
			if (a_name == "Optimized.B.csxpack")
				return 1;
			if (a_name == "Developer.A.csxpack")
				return 2;
			assert(a_name == "Developer.B.csxpack");
			return 3;
		};
		for (const auto& testCase : corpus["cases"]) {
			auto manifest = MakePackManifest();
			for (const auto& [pointer, value] : testCase.value("manifestOverrides", nlohmann::json::object()).items())
				manifest[nlohmann::json::json_pointer(pointer)] = value;
			for (const auto& pointerValue : testCase.value("manifestRemovals", nlohmann::json::array())) {
				const auto pointer = pointerValue.get<std::string>();
				const auto separator = pointer.find_last_of('/');
				assert(separator != std::string::npos);
				auto& parent = separator == 0 ? manifest : manifest.at(nlohmann::json::json_pointer(pointer.substr(0, separator)));
				parent.erase(unescapePointerToken(pointer.substr(separator + 1)));
			}

			std::string diagnostic;
			const auto contract = ParseManifestContract(manifest, "VR", &diagnostic);
			bool accepted = false;
			if (contract) {
				std::array<PackFileState, 4> states{
					PackFileState{ TestPackSetId(), Lane::Optimized, true, 1, 3 },
					PackFileState{ TestPackSetId(), Lane::Optimized, true, 0, 0 },
					PackFileState{ TestPackSetId(), Lane::Developer, true, 1, 0 },
					PackFileState{ TestPackSetId(), Lane::Developer, true, 0, 0 },
				};
				for (const auto& [name, overrides] : testCase.value("fileOverrides", nlohmann::json::object()).items()) {
					auto& state = states[fileIndex(name)];
					if (const auto found = overrides.find("packSetId"); found != overrides.end())
						state.packSetId = ParsePackSetId(found->get_ref<const std::string&>());
					if (const auto found = overrides.find("lane"); found != overrides.end())
						state.lane = static_cast<Lane>(found->get<std::uint32_t>());
					if (const auto found = overrides.find("valid"); found != overrides.end())
						state.valid = found->get<bool>();
					if (const auto found = overrides.find("generation"); found != overrides.end())
						state.generation = found->get<std::uint64_t>();
					if (const auto found = overrides.find("recordCount"); found != overrides.end())
						state.recordCount = found->get<std::uint64_t>();
				}
				accepted = ValidateManifestFileStates(*contract, states, &diagnostic);
			}
			assert(accepted == testCase["accepted"].get<bool>());
		}
	}

	// Fixed names alone do not make a managed layout authoritative. Every member
	// must be an openable regular pack file satisfying its manifest baseline.
	{
		const auto emptyRoot = root / "read-only-empty-layout";
		std::filesystem::create_directories(emptyRoot);
		const auto first = emptyRoot / "Optimized.A.csxpack";
		const auto second = emptyRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string layoutError;
		Store readOnly(first, second, Lane::Optimized, TestPackSetId());
		assert(!readOnly.Open(&layoutError));
		assert(std::filesystem::file_size(first) == 0);
		assert(std::filesystem::file_size(second) == 0);
		assert(readOnly.InitializeEmptyFilesAndOpen(&layoutError));
	}
	{
		const auto invalidLayoutRoot = root / "invalid-layout";
		std::filesystem::create_directories(invalidLayoutRoot);
		const auto directoryMember = invalidLayoutRoot / "Optimized.A.csxpack";
		const auto regularMember = invalidLayoutRoot / "Optimized.B.csxpack";
		std::filesystem::create_directories(directoryMember);
		std::ofstream(regularMember, std::ios::binary).close();
		std::string layoutError;
		Store directoryLayout(directoryMember, regularMember, Lane::Optimized, TestPackSetId());
		assert(!directoryLayout.Open(&layoutError));
		assert(!layoutError.empty());
		assert(std::filesystem::file_size(regularMember) == 0);
	}
#ifdef _WIN32
	// Admission resolves relative names once and guards every ordinary parent.
	// Later current-directory changes cannot redirect reads or mutations.
	{
		const auto originalWorkingDirectory = std::filesystem::current_path();
		const auto stableRoot = root / "stable-relative-root";
		const auto alternateRoot = root / "alternate-relative-root";
		const auto relativeDirectory = std::filesystem::path("Data") / "ShaderCache";
		std::filesystem::create_directories(stableRoot / relativeDirectory);
		std::filesystem::create_directories(alternateRoot / relativeDirectory);
		for (const auto& base : { stableRoot, alternateRoot }) {
			std::ofstream(base / relativeDirectory / "Optimized.A.csxpack", std::ios::binary).close();
			std::ofstream(base / relativeDirectory / "Optimized.B.csxpack", std::ios::binary).close();
		}
		std::filesystem::current_path(stableRoot);
		std::string stableError;
		Store stable(
			relativeDirectory / "Optimized.A.csxpack",
			relativeDirectory / "Optimized.B.csxpack",
			Lane::Optimized,
			TestPackSetId());
		assert(stable.InitializeEmptyFilesAndOpen(&stableError));
		std::filesystem::current_path(alternateRoot);
		assert(stable.Append(MakeEntry("stable", "stable-exact", 0x70), &stableError));
		assert(stable.Checkpoint(&stableError));
		assert(stable.Find("stable-exact", &stableError));
		assert(stable.Compact(&stableError));
		assert(stable.Find("stable-exact", &stableError));
		assert(stable.Reset(&stableError) == ResetDisposition::Complete);
		assert(!stable.Find("stable-exact", &stableError));
		assert(std::filesystem::file_size(
				   alternateRoot / relativeDirectory / "Optimized.A.csxpack") == 0);
		assert(std::filesystem::file_size(
				   alternateRoot / relativeDirectory / "Optimized.B.csxpack") == 0);
		std::error_code renameError;
		std::filesystem::rename(
			stableRoot / relativeDirectory,
			stableRoot / "displaced-cache",
			renameError);
		assert(renameError);
		stable.Close();
		renameError.clear();
		std::filesystem::rename(
			stableRoot / relativeDirectory,
			stableRoot / "displaced-cache",
			renameError);
		assert(!renameError);
		std::filesystem::current_path(originalWorkingDirectory);
	}

	// An intermediate directory reparse point is rejected before either member
	// is admitted. The test is conditional when symlink creation is unavailable.
	{
		const auto target = root / "reparse-target";
		const auto link = root / "reparse-link";
		std::filesystem::create_directories(target);
		const auto first = target / "Optimized.A.csxpack";
		const auto second = target / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string setupError;
		{
			Store setup(first, second, Lane::Optimized, TestPackSetId());
			assert(setup.InitializeEmptyFilesAndOpen(&setupError));
		}
		constexpr DWORD allowUnprivilegedCreate = 0x2;
		if (CreateSymbolicLinkW(
				link.c_str(), target.c_str(),
				SYMBOLIC_LINK_FLAG_DIRECTORY | allowUnprivilegedCreate)) {
			std::string reparseError;
			Store throughReparse(
				link / first.filename(), link / second.filename(),
				Lane::Optimized, TestPackSetId());
			assert(!throughReparse.Open(&reparseError));
			assert(reparseError.find("reparse") != std::string::npos);
		}
	}

	// Ownership acquired before a thrown admission step must be reacquirable in
	// the same process without terminating it.
	{
		const auto failureRoot = root / "registry-failure";
		std::filesystem::create_directories(failureRoot);
		const auto first = failureRoot / "Optimized.A.csxpack";
		const auto second = failureRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string failureError;
		{
			Store setup(first, second, Lane::Optimized, TestPackSetId());
			assert(setup.InitializeEmptyFilesAndOpen(&failureError));
		}
		SetTestFailurePoints(static_cast<std::uint32_t>(TestFailurePoint::AfterRegistryInsert));
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(!failed.Open(&failureError));
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.Open(&failureError));
	}

	// Lazy mutation admission has the same transactional ownership boundary as
	// explicit Open: rejection and exceptions leave no guards or writer lease.
	for (const auto operation : { "append", "reset" }) {
		const auto admissionRoot = root / (std::string("lazy-admission-") + operation);
		const auto movedRoot = root / (std::string("lazy-admission-moved-") + operation);
		std::filesystem::create_directories(admissionRoot);
		const auto first = admissionRoot / "Optimized.A.csxpack";
		const auto second = admissionRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string admissionError;
		Store rejected(first, second, Lane::Optimized, TestPackSetId());
		if (std::string_view(operation) == "append")
			assert(!rejected.Append(MakeEntry("rejected", "rejected-exact", 0x75), &admissionError));
		else
			assert(rejected.Reset(&admissionError) == ResetDisposition::FailedBeforeCommit);
		assert(!admissionError.empty());
		std::error_code renameError;
		std::filesystem::rename(admissionRoot, movedRoot, renameError);
		assert(!renameError);
		std::filesystem::rename(movedRoot, admissionRoot, renameError);
		assert(!renameError);
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.InitializeEmptyFilesAndOpen(&admissionError));
	}
	for (const auto operation : { "append", "reset" }) {
		const auto admissionRoot = root / (std::string("lazy-admission-exception-") + operation);
		std::filesystem::create_directories(admissionRoot);
		const auto first = admissionRoot / "Optimized.A.csxpack";
		const auto second = admissionRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string admissionError;
		{
			Store setup(first, second, Lane::Optimized, TestPackSetId());
			assert(setup.InitializeEmptyFilesAndOpen(&admissionError));
		}
		SetTestFailurePoints(static_cast<std::uint32_t>(TestFailurePoint::AfterRegistryInsert));
		Store rejected(first, second, Lane::Optimized, TestPackSetId());
		if (std::string_view(operation) == "append")
			assert(!rejected.Append(MakeEntry("rejected", "rejected-exact", 0x76), &admissionError));
		else
			assert(rejected.Reset(&admissionError) == ResetDisposition::FailedBeforeCommit);
		assert(admissionError.find("injected") != std::string::npos);
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.Open(&admissionError));
	}

	// A failed isolated bootstrap reports when its attempted rollback cannot
	// establish the original zero-byte state.
	{
		const auto rollbackRoot = root / "bootstrap-rollback-failure";
		std::filesystem::create_directories(rollbackRoot);
		const auto first = rollbackRoot / "Optimized.A.csxpack";
		const auto second = rollbackRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		const auto failurePoints =
			static_cast<std::uint32_t>(TestFailurePoint::BeforeSecondBootstrapInitialization) |
			static_cast<std::uint32_t>(TestFailurePoint::DuringBootstrapRollback);
		SetTestFailurePoints(failurePoints);
		std::string rollbackError;
		{
			Store failed(first, second, Lane::Optimized, TestPackSetId());
			assert(!failed.InitializeEmptyFilesAndOpen(&rollbackError));
			assert(rollbackError.find("rollback failed") != std::string::npos);
			assert(std::filesystem::file_size(first) > 0);
			assert(std::filesystem::file_size(second) == 0);
		}
		std::ofstream(first, std::ios::binary | std::ios::trunc).close();
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.InitializeEmptyFilesAndOpen(&rollbackError));
	}

	// Exceptional bootstrap exits attempt and verify the same rollback as
	// ordinary initialization failures.
	{
		const auto rollbackRoot = root / "bootstrap-exception-rollback";
		std::filesystem::create_directories(rollbackRoot);
		const auto first = rollbackRoot / "Optimized.A.csxpack";
		const auto second = rollbackRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		SetTestFailurePoints(static_cast<std::uint32_t>(TestFailurePoint::AfterFirstBootstrapInitialization));
		std::string rollbackError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(!failed.InitializeEmptyFilesAndOpen(&rollbackError));
		assert(rollbackError.find("raised an exception") != std::string::npos);
		assert(rollbackError.find("rollback restored") != std::string::npos);
		assert(std::filesystem::file_size(first) == 0);
		assert(std::filesystem::file_size(second) == 0);
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.InitializeEmptyFilesAndOpen(&rollbackError));
	}

	// Exceptions raised by rollback bookkeeping or diagnostics cannot pre-empt
	// restoration and verification of either bootstrap member.
	for (const auto failurePoint : {
			 TestFailurePoint::ThrowBeforeFirstBootstrapRollback,
			 TestFailurePoint::ThrowBetweenBootstrapRollbackMembers,
			 TestFailurePoint::ThrowDuringBootstrapRollbackDiagnostic }) {
		const auto rollbackRoot = root / ("bootstrap-recovery-exception-" + std::to_string(static_cast<std::uint32_t>(failurePoint)));
		std::filesystem::create_directories(rollbackRoot);
		const auto first = rollbackRoot / "Optimized.A.csxpack";
		const auto second = rollbackRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		SetTestFailurePoints(
			static_cast<std::uint32_t>(TestFailurePoint::AfterFirstBootstrapInitialization) |
			static_cast<std::uint32_t>(failurePoint));
		std::string rollbackError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(!failed.InitializeEmptyFilesAndOpen(&rollbackError));
		assert(rollbackError.find("rollback restored") != std::string::npos);
		assert(std::filesystem::file_size(first) == 0);
		assert(std::filesystem::file_size(second) == 0);
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.InitializeEmptyFilesAndOpen(&rollbackError));
	}

	// Bootstrap rollback remains armed until admission and index publication
	// finish, rather than ending after the second file is initialized.
	for (const auto failurePoint : {
			 TestFailurePoint::BeforeStoreAdmissionCommit,
			 TestFailurePoint::DuringStoreAdmissionCommit,
			 TestFailurePoint::DuringStoreIndexPublication }) {
		const auto rollbackRoot = root / ("bootstrap-admission-rollback-" + std::to_string(static_cast<std::uint32_t>(failurePoint)));
		std::filesystem::create_directories(rollbackRoot);
		const auto first = rollbackRoot / "Optimized.A.csxpack";
		const auto second = rollbackRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		SetTestFailurePoints(static_cast<std::uint32_t>(failurePoint));
		std::string rollbackError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(!failed.InitializeEmptyFilesAndOpen(&rollbackError));
		assert(rollbackError.find("rollback restored") != std::string::npos);
		assert(!failed.GetStats().available);
		assert(std::filesystem::file_size(first) == 0);
		assert(std::filesystem::file_size(second) == 0);

		Store ordinary(first, second, Lane::Optimized, TestPackSetId());
		assert(!ordinary.Open(&rollbackError));
		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.InitializeEmptyFilesAndOpen(&rollbackError));
	}
	{
		const auto rollbackRoot = root / "bootstrap-admission-diagnostic-rollback";
		std::filesystem::create_directories(rollbackRoot);
		const auto first = rollbackRoot / "Optimized.A.csxpack";
		const auto second = rollbackRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		SetTestFailurePoints(
			static_cast<std::uint32_t>(TestFailurePoint::DuringStoreIndexPublication) |
			static_cast<std::uint32_t>(TestFailurePoint::ThrowDuringBootstrapRollbackDiagnostic));
		std::string rollbackError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(!failed.InitializeEmptyFilesAndOpen(&rollbackError));
		assert(rollbackError.find("detailed diagnostic unavailable") != std::string::npos);
		assert(std::filesystem::file_size(first) == 0);
		assert(std::filesystem::file_size(second) == 0);
	}
#endif
	{
		const auto invalidLayoutRoot = root / "malformed-layout";
		std::filesystem::create_directories(invalidLayoutRoot);
		const auto malformedMember = invalidLayoutRoot / "Optimized.A.csxpack";
		const auto regularMember = invalidLayoutRoot / "Optimized.B.csxpack";
		std::ofstream(malformedMember, std::ios::binary).write("bad", 3);
		std::ofstream(regularMember, std::ios::binary).close();
		std::string layoutError;
		Store malformedLayout(malformedMember, regularMember, Lane::Optimized, TestPackSetId());
		assert(!malformedLayout.Open(&layoutError));
		assert(!layoutError.empty());
		assert(std::filesystem::file_size(malformedMember) == 3);
		assert(std::filesystem::file_size(regularMember) == 0);
	}
#ifdef _WIN32
	{
		const auto unreadableRoot = root / "unreadable-layout";
		std::filesystem::create_directories(unreadableRoot);
		const auto first = unreadableRoot / "Optimized.A.csxpack";
		const auto second = unreadableRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		{
			std::string setupError;
			Store setup(first, second, Lane::Optimized, TestPackSetId());
			assert(setup.InitializeEmptyFilesAndOpen(&setupError));
		}
		const HANDLE blocker = CreateFileW(
			first.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		assert(blocker != INVALID_HANDLE_VALUE);
		const auto secondSize = std::filesystem::file_size(second);
		std::string unreadableError;
		Store unreadable(first, second, Lane::Optimized, TestPackSetId());
		assert(!unreadable.Open(&unreadableError));
		assert(!unreadableError.empty());
		assert(std::filesystem::file_size(second) == secondSize);
		CloseHandle(blocker);
		std::string repairedError;
		Store repaired(first, second, Lane::Optimized, TestPackSetId());
		assert(repaired.Open(&repairedError));
	}
#endif
	{
		const auto mismatchRoot = root / "header-mismatch-layout";
		std::filesystem::create_directories(mismatchRoot);
		const auto first = mismatchRoot / "Optimized.A.csxpack";
		const auto second = mismatchRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		{
			std::string setupError;
			Store setup(first, second, Lane::Optimized, TestPackSetId());
			assert(setup.InitializeEmptyFilesAndOpen(&setupError));
		}
		PackSetId otherSet = TestPackSetId();
		otherSet.back() = std::byte{ 0x7f };
		std::string mismatchError;
		Store wrongIdentity(first, second, Lane::Optimized, otherSet);
		assert(!wrongIdentity.Open(&mismatchError));
		Store wrongLane(first, second, Lane::Developer, TestPackSetId());
		assert(!wrongLane.Open(&mismatchError));
	}

	// The reserved zero identity is rejected before either fixed file is mutated.
	{
		const auto invalidRoot = root / "zero-identity";
		std::filesystem::create_directories(invalidRoot);
		const auto first = invalidRoot / "Optimized.A.csxpack";
		const auto second = invalidRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string invalidError;
		Store invalid(first, second, Lane::Optimized, PackSetId{});
		assert(!invalid.Open(&invalidError));
		assert(!invalidError.empty());
		assert(std::filesystem::file_size(first) == 0);
		assert(std::filesystem::file_size(second) == 0);
	}

	// A live index must not trust payload bytes that changed after Open().
	{
		const auto mutationRoot = root / "mutation";
		std::filesystem::create_directories(mutationRoot);
		const auto first = mutationRoot / "Optimized.A.csxpack";
		const auto second = mutationRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string mutationError;
		Store mutation(first, second, Lane::Optimized, TestPackSetId());
		assert(mutation.InitializeEmptyFilesAndOpen(&mutationError));
		assert(mutation.Append(MakeEntry("logical", "exact", 0x71), &mutationError));
		assert(mutation.Checkpoint(&mutationError));
		const char changed = 'X';
		Overwrite(ActivePack(first, second), 160, changed);
		mutationError.clear();
		assert(!mutation.Find("exact", &mutationError));
		assert(!mutationError.empty());
	}

	// Oversized record dimensions are contained as a corrupt tail instead of
	// wrapping record arithmetic or addressing beyond the mapped payload.
	{
		const auto overflowRoot = root / "overflow";
		std::filesystem::create_directories(overflowRoot);
		const auto first = overflowRoot / "Optimized.A.csxpack";
		const auto second = overflowRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string overflowError;
		{
			Store writer(first, second, Lane::Optimized, TestPackSetId());
			assert(writer.InitializeEmptyFilesAndOpen(&overflowError));
			assert(writer.Append(MakeEntry("logical", "exact", 0x72), &overflowError));
			assert(writer.Checkpoint(&overflowError));
		}
		const auto maximum = (std::numeric_limits<std::uint32_t>::max)();
		Overwrite(ActivePack(first, second), 104, maximum);
		Store reader(first, second, Lane::Optimized, TestPackSetId());
		assert(reader.Open(&overflowError));
		assert(reader.GetStats().corruptTailBytes > 0);
		assert(!reader.Find("exact", &overflowError));
	}

	// Sequence zero and UINT64_MAX are reserved. The largest usable sequence is
	// visible in both readers; the exhausted sentinel is an invalid tail.
	{
		const auto sequenceRoot = root / "record-sequence-domain";
		std::filesystem::create_directories(sequenceRoot);
		const auto first = sequenceRoot / "Optimized.A.csxpack";
		const auto second = sequenceRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string sequenceError;
		{
			Store writer(first, second, Lane::Optimized, TestPackSetId());
			assert(writer.InitializeEmptyFilesAndOpen(&sequenceError));
			assert(writer.Append(MakeEntry("sequence", "sequence-exact", 0x74), &sequenceError));
			assert(writer.Checkpoint(&sequenceError));
		}
		const auto activePath = ActivePack(first, second);
		const auto maximumUsable = (std::numeric_limits<std::uint64_t>::max)() - 1;
		Overwrite(activePath, 96, maximumUsable);
		{
			Store reader(first, second, Lane::Optimized, TestPackSetId());
			assert(reader.Open(&sequenceError));
			assert(reader.GetStats().recordCount == 1);
		}
		const auto exhausted = (std::numeric_limits<std::uint64_t>::max)();
		Overwrite(activePath, 96, exhausted);
		{
			Store reader(first, second, Lane::Optimized, TestPackSetId());
			sequenceError.clear();
			assert(reader.Open(&sequenceError));
			assert(reader.GetStats().recordCount == 0);
			assert(reader.GetStats().corruptTailBytes > 0);
			assert(!sequenceError.empty());
		}
	}

	// A nonzero pack-set identity is also the cross-process writer lease key.
	{
		const auto leaseRoot = root / "lease";
		std::filesystem::create_directories(leaseRoot);
		const auto first = leaseRoot / "Optimized.A.csxpack";
		const auto second = leaseRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		const auto setID = TestPackSetId();
		std::string leaseError;
		Store owner(first, second, Lane::Optimized, setID);
		assert(owner.InitializeEmptyFilesAndOpen(&leaseError));
		const auto firstSize = std::filesystem::file_size(first);
		const auto secondSize = std::filesystem::file_size(second);
		Store contender(first, second, Lane::Optimized, setID);
		assert(!contender.Open(&leaseError));
		assert(!leaseError.empty());
		Store reversed(second, first, Lane::Optimized, setID);
		assert(!reversed.Open(&leaseError));
		Store wrongLane(first, second, Lane::Developer, setID);
		assert(!wrongLane.Open(&leaseError));
		assert(std::filesystem::file_size(first) == firstSize);
		assert(std::filesystem::file_size(second) == secondSize);
#ifdef _WIN32
		std::error_code replacementError;
		std::filesystem::rename(first, leaseRoot / "displaced.csxpack", replacementError);
		assert(replacementError);
		assert(std::filesystem::exists(first));
#endif
		bool threadOpened = true;
		std::jthread contenderThread([&] {
			std::string threadError;
			Store threadContender(
				first.parent_path() / "." / first.filename(),
				second.parent_path() / "." / second.filename(),
				Lane::Optimized,
				setID);
			threadOpened = threadContender.Open(&threadError);
		});
		contenderThread.join();
		assert(!threadOpened);
#ifdef _WIN32
		assert(RunLeaseProbe(argv[0], second, first, false, leaseRoot / "alternate-temp") == 0);
		const auto aliasFirst = leaseRoot / "Optimized.A.alias.csxpack";
		const auto aliasSecond = leaseRoot / "Optimized.B.alias.csxpack";
		assert(CreateHardLinkW(aliasFirst.c_str(), first.c_str(), nullptr));
		assert(CreateHardLinkW(aliasSecond.c_str(), second.c_str(), nullptr));
		assert(RunLeaseProbe(argv[0], aliasFirst, aliasSecond, false) == 0);
		for (const auto operation : { "append", "checkpoint", "compact", "reset" }) {
			assert(RunLeaseMutationProbe(
					   argv[0], second, first, operation,
					   leaseRoot / (std::string("mutation-temp-") + operation)) == 0);
			assert(std::filesystem::file_size(first) == firstSize);
			assert(std::filesystem::file_size(second) == secondSize);
		}
#endif
	}

#ifdef _WIN32
	// A/B slot names must represent two distinct physical files. Exact-path and
	// hard-link aliases fail before initialization and release provisional guards.
	{
		const auto topologyRoot = root / "same-object-a-b";
		std::filesystem::create_directories(topologyRoot);
		const auto first = topologyRoot / "Optimized.A.csxpack";
		const auto alias = topologyRoot / "Optimized.B.csxpack";
		const auto independent = topologyRoot / "Independent.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(independent, std::ios::binary).close();
		assert(CreateHardLinkW(alias.c_str(), first.c_str(), nullptr));
		std::string topologyError;
		Store exactAlias(first, first, Lane::Optimized, TestPackSetId());
		assert(!exactAlias.InitializeEmptyFilesAndOpen(&topologyError));
		assert(std::filesystem::file_size(first) == 0);
		Store hardLinkAlias(first, alias, Lane::Optimized, TestPackSetId());
		assert(!hardLinkAlias.InitializeEmptyFilesAndOpen(&topologyError));
		assert(std::filesystem::file_size(first) == 0);
		Store recoveredTopology(first, independent, Lane::Optimized, TestPackSetId());
		assert(recoveredTopology.InitializeEmptyFilesAndOpen(&topologyError));
	}
#endif

	// Equal readable generations are ambiguous rather than an arbitrary A/B tie.
	{
		const auto ambiguousRoot = root / "ambiguous-generation";
		std::filesystem::create_directories(ambiguousRoot);
		const auto first = ambiguousRoot / "Optimized.A.csxpack";
		const auto second = ambiguousRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string ambiguousError;
		{
			Store writer(first, second, Lane::Optimized, TestPackSetId());
			assert(writer.InitializeEmptyFilesAndOpen(&ambiguousError));
			assert(writer.Append(MakeEntry("logical", "exact", 0x73), &ambiguousError));
			assert(writer.Checkpoint(&ambiguousError));
			assert(writer.Compact(&ambiguousError));
		}
		SetPackGeneration(first, 2);
		Store reader(first, second, Lane::Optimized, TestPackSetId());
		assert(!reader.Open(&ambiguousError));
		assert(ambiguousError.find("ambiguous") != std::string::npos);
	}
#ifdef _WIN32
	{
		const auto leaseRoot = root / "lease";
		assert(RunLeaseProbe(
				   argv[0],
				   leaseRoot / "Optimized.B.csxpack",
				   leaseRoot / "Optimized.A.csxpack",
				   true) == 0);
	}
	{
		const auto releaseRoot = root / "cross-thread-release";
		std::filesystem::create_directories(releaseRoot);
		const auto first = releaseRoot / "Optimized.A.csxpack";
		const auto second = releaseRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		auto owner = std::make_unique<Store>(first, second, Lane::Optimized, TestPackSetId());
		std::string releaseError;
		assert(owner->InitializeEmptyFilesAndOpen(&releaseError));
		std::jthread releaser([owned = std::move(owner)]() mutable { owned.reset(); });
		releaser.join();
		assert(RunLeaseProbe(argv[0], first, second, true) == 0);
	}
	{
		const auto abandonedRoot = root / "abandoned-lease";
		std::filesystem::create_directories(abandonedRoot);
		const auto first = abandonedRoot / "Optimized.A.csxpack";
		const auto second = abandonedRoot / "Optimized.B.csxpack";
		const auto ready = abandonedRoot / "ready";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		{
			std::string setupError;
			Store setup(first, second, Lane::Optimized, TestPackSetId());
			assert(setup.InitializeEmptyFilesAndOpen(&setupError));
		}
		TerminateLeaseHolder(argv[0], first, second, ready, abandonedRoot / "holder-temp");
		std::string leaseError;
		Store recoveredLease(first, second, Lane::Optimized, TestPackSetId());
		assert(recoveredLease.Open(&leaseError));
	}
#endif

	const auto a = root / "Optimized.A.csxpack";
	const auto b = root / "Optimized.B.csxpack";
	std::ofstream(a, std::ios::binary).close();
	std::ofstream(b, std::ios::binary).close();

	std::string error;
	{
		Store store(a, b, Lane::Optimized, TestPackSetId());
		assert(store.InitializeEmptyFilesAndOpen(&error));
		assert(store.Append(MakeEntry("water|provider=1", "water|source=old|provider=1", 0x11), &error));
		assert(store.Append(MakeEntry("water|provider=1", "water|source=new|provider=1", 0x22), &error));
		assert(store.Append(MakeEntry("water|provider=2", "water|source=new|provider=2", 0x33), &error));
		assert(store.Checkpoint(&error));
		const auto contract = ParseManifestContract(MakePackManifest(), "VR", &error);
		assert(contract);
		assert(ValidateOptimizedStore(store, *contract, &error));
		auto swappedManifest = MakePackManifest();
		std::swap(
			swappedManifest["files"]["Optimized.A.csxpack"],
			swappedManifest["files"]["Optimized.B.csxpack"]);
		const auto swappedContract = ParseManifestContract(swappedManifest, "VR", &error);
		assert(swappedContract);
		assert(!ValidateOptimizedStore(store, *swappedContract, &error));

		auto old = store.Find("water|source=old|provider=1", &error);
		auto current = store.Find("water|source=new|provider=1", &error);
		assert(old && current);
		assert(old->bytecode.front() == std::byte{ 0x11 });
		assert(current->bytecode.front() == std::byte{ 0x22 });
		auto compatible = store.FindCompatible(
			"water|provider=1",
			[](std::string_view a_metadata) { return a_metadata == "{\"schema\":1}"; },
			&error);
		assert(compatible && compatible->bytecode.front() == std::byte{ 0x22 });
		assert(!store.FindCompatible("water|provider=1", [](std::string_view) { return false; }, &error));
		const auto before = store.GetStats();
		assert(before.recordCount == 3 && before.liveRecordCount == 2 && before.supersededBytes > 0);
	}

	// A committed prefix remains readable when a process dies during the next append.
	{
		std::ofstream tail(a, std::ios::binary | std::ios::app);
		const char incomplete[] = "CSXREC1";
		tail.write(incomplete, sizeof(incomplete));
	}
	Store recovered(a, b, Lane::Optimized, TestPackSetId());
	error.clear();
	assert(recovered.Open(&error));
	assert(!error.empty());
	assert(recovered.Find("water|source=new|provider=1", &error));
	const auto compatibleAfterReopen = recovered.FindCompatible(
		"water|provider=1",
		[](std::string_view) { return true; },
		&error);
	assert(compatibleAfterReopen && compatibleAfterReopen->bytecode.front() == std::byte{ 0x22 });
	assert(recovered.GetStats().corruptTailBytes > 0);

	// Compaction retains the latest exact record per logical/compatibility key in
	// the new generation while the previous generation remains searchable.
	assert(recovered.Compact(&error));
	{
		const auto contract = ParseManifestContract(MakePackManifest(), "VR", &error);
		assert(contract);
		assert(ValidateOptimizedStore(recovered, *contract, &error));
	}
	const auto compacted = recovered.GetStats();
	assert(compacted.activeGeneration == 2);
	assert(compacted.recordCount == 2);
	assert(compacted.liveRecordCount == 2);
	assert(compacted.supersededBytes == 0);
	assert(compacted.Fragmentation() == 0.0);
	assert(recovered.Find("water|source=new|provider=1", &error));
	assert(recovered.Find("water|source=new|provider=2", &error));
	assert(recovered.Find("water|source=old|provider=1", &error));

	// Exercise the opposite A/B role: B is active after the first compaction,
	// and the next compaction must safely initialize and promote A.
	assert(recovered.Append(MakeEntry("water|provider=1", "water|source=newest|provider=1", 0x44), &error));
	assert(recovered.Checkpoint(&error));
	assert(recovered.Compact(&error));
	assert(recovered.GetStats().activeGeneration == 3);
	assert(recovered.Find("water|source=newest|provider=1", &error));

	// A compaction failure proven to precede target mutation preserves the
	// coherent Store. Every later failure withdraws Store-level authority before
	// returning, independently of the caller's lane quarantine.
	auto verifyCompactionFailure = [&](TestFailurePoint a_failurePoint, std::string_view a_name, bool a_secondRole) {
		const auto compactRoot = root / (std::string("compact-failure-") + std::string(a_name) + (a_secondRole ? "-second" : "-first"));
		std::filesystem::create_directories(compactRoot);
		const auto first = compactRoot / "Optimized.A.csxpack";
		const auto second = compactRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string compactError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(failed.InitializeEmptyFilesAndOpen(&compactError));
		assert(failed.Append(MakeEntry("compact", "compact-exact", 0x51), &compactError));
		assert(failed.Checkpoint(&compactError));
		if (a_secondRole) {
			assert(failed.Compact(&compactError));
			assert(failed.Append(MakeEntry("compact", "compact-newer", 0x52), &compactError));
			assert(failed.Checkpoint(&compactError));
		}

		SetTestFailurePoints(static_cast<std::uint32_t>(a_failurePoint));
		compactError.clear();
		assert(!failed.Compact(&compactError));
		assert(!compactError.empty());
		assert(!failed.GetStats().available);
		assert(!failed.Find(a_secondRole ? "compact-newer" : "compact-exact", &compactError));
		const auto identities = failed.GetFileIdentityKeys();
		assert(identities[0].empty() && identities[1].empty());
	};
	for (const auto [failurePoint, name] : {
			 std::pair{ TestFailurePoint::AfterInitializeTruncate, "truncate-false" },
			 std::pair{ TestFailurePoint::ThrowAfterInitializeTruncate, "truncate-throw" },
			 std::pair{ TestFailurePoint::AfterInitializeWrite, "write-false" },
			 std::pair{ TestFailurePoint::ThrowAfterInitializeWrite, "write-throw" },
			 std::pair{ TestFailurePoint::AfterInitializeDurableFlush, "durable-false" },
			 std::pair{ TestFailurePoint::ThrowAfterInitializeDurableFlush, "durable-throw" },
			 std::pair{ TestFailurePoint::DuringCompactionCopy, "copy" },
			 std::pair{ TestFailurePoint::BeforeStoreAdmissionCommit, "admission-false" },
			 std::pair{ TestFailurePoint::DuringStoreAdmissionCommit, "admission-throw" },
			 std::pair{ TestFailurePoint::DuringStoreIndexPublication, "index-throw" } }) {
		verifyCompactionFailure(failurePoint, name, false);
		verifyCompactionFailure(failurePoint, name, true);
	}
	{
		const auto compactRoot = root / "compact-failure-before-mutation";
		std::filesystem::create_directories(compactRoot);
		const auto first = compactRoot / "Optimized.A.csxpack";
		const auto second = compactRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string compactError;
		Store unchanged(first, second, Lane::Optimized, TestPackSetId());
		assert(unchanged.InitializeEmptyFilesAndOpen(&compactError));
		assert(unchanged.Append(MakeEntry("compact", "compact-exact", 0x53), &compactError));
		assert(unchanged.Checkpoint(&compactError));
		SetTestFailurePoints(static_cast<std::uint32_t>(TestFailurePoint::BeforeInitializeMutation));
		assert(!unchanged.Compact(&compactError));
		assert(unchanged.GetStats().available);
		assert(unchanged.Find("compact-exact", &compactError));
	}

	// Reset first commits a new generation barrier, so a failed cleanup cannot
	// make any record from the prior generation visible again.
	assert(recovered.Reset(&error) == ResetDisposition::Complete);
	{
		const auto contract = ParseManifestContract(MakePackManifest(), "VR", &error);
		assert(contract);
		assert(ValidateOptimizedStore(recovered, *contract, &error));
	}
	assert(recovered.GetStats().activeGeneration == 5);
	assert(!recovered.Find("water|source=newest|provider=1", &error));
	assert(recovered.Append(MakeEntry("water|provider=1", "water|source=reset|provider=1", 0x55), &error));
	assert(recovered.Checkpoint(&error));
	assert(recovered.Find("water|source=reset|provider=1", &error));

#ifdef _WIN32
	// Cleanup failure after the empty generation barrier is a committed but
	// explicitly degraded reset, not an unqualified success.
	{
		const auto degradedRoot = root / "degraded-reset";
		std::filesystem::create_directories(degradedRoot);
		const auto first = degradedRoot / "Optimized.A.csxpack";
		const auto second = degradedRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string resetError;
		{
			Store degraded(first, second, Lane::Optimized, TestPackSetId());
			assert(degraded.InitializeEmptyFilesAndOpen(&resetError));
			assert(degraded.Append(MakeEntry("reset", "reset-exact", 0x60), &resetError));
			assert(degraded.Checkpoint(&resetError));

			assert(SetFileAttributesW(first.c_str(), FILE_ATTRIBUTE_READONLY));
			resetError.clear();
			assert(degraded.Reset(&resetError) == ResetDisposition::CommittedDegraded);
			assert(!resetError.empty());
			assert(degraded.GetStats().available);
			assert(!degraded.Find("reset-exact", &resetError));
			assert(SetFileAttributesW(first.c_str(), FILE_ATTRIBUTE_NORMAL));
		}

		resetError.clear();
		Store restarted(first, second, Lane::Optimized, TestPackSetId());
		assert(restarted.Open(&resetError));
		assert(resetError.find("generation gap") != std::string::npos);
		assert(!restarted.Find("reset-exact", &resetError));
	}

	// Cleanup-only false returns and exceptions preserve the already reopened
	// empty generation while excluding the uncertain superseded member.
	auto verifyCleanupFailure = [&](std::uint32_t a_failurePoints, std::string_view a_name) {
		const auto degradedRoot = root / (std::string("degraded-reset-") + std::string(a_name));
		std::filesystem::create_directories(degradedRoot);
		const auto first = degradedRoot / "Optimized.A.csxpack";
		const auto second = degradedRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string resetError;
		Store degraded(first, second, Lane::Optimized, TestPackSetId());
		assert(degraded.InitializeEmptyFilesAndOpen(&resetError));
		assert(degraded.Append(MakeEntry("old", "old-exact", 0x61), &resetError));
		assert(degraded.Checkpoint(&resetError));
		SetTestFailurePoints(a_failurePoints);
		resetError.clear();
		assert(degraded.Reset(&resetError) == ResetDisposition::CommittedDegraded);
		assert(!resetError.empty());
		assert(degraded.GetStats().available);
		assert(!degraded.Find("old-exact", &resetError));
		assert(degraded.Append(MakeEntry("new", "new-exact", 0x62), &resetError));
		assert(degraded.Checkpoint(&resetError));
		assert(degraded.Find("new-exact", &resetError));
		const auto identities = degraded.GetFileIdentityKeys();
		assert(!identities[0].empty() && !identities[1].empty());
	};
	for (const auto [failurePoint, name] : {
			 std::pair{ TestFailurePoint::BeforeResetCleanupMutation, "before-mutation" },
			 std::pair{ TestFailurePoint::AfterResetCleanupTruncate, "truncate-false" },
			 std::pair{ TestFailurePoint::ThrowAfterResetCleanupTruncate, "truncate-throw" },
			 std::pair{ TestFailurePoint::AfterResetCleanupWrite, "write-false" },
			 std::pair{ TestFailurePoint::ThrowAfterResetCleanupWrite, "write-throw" },
			 std::pair{ TestFailurePoint::AfterResetCleanupDurableFlush, "durable-false" },
			 std::pair{ TestFailurePoint::ThrowAfterResetCleanupDurableFlush, "durable-throw" },
			 std::pair{ TestFailurePoint::ThrowBeforeResetCleanupVerification, "verification-throw" } }) {
		verifyCleanupFailure(static_cast<std::uint32_t>(failurePoint), name);
	}
	verifyCleanupFailure(
		static_cast<std::uint32_t>(TestFailurePoint::BeforeResetCleanupMutation) |
			static_cast<std::uint32_t>(TestFailurePoint::ThrowDuringResetCleanupDiagnostic),
		"diagnostic-throw");

	// A committed barrier whose authoritative generation cannot be reopened is
	// quarantined, while a later clean Store can reacquire and recover it.
	auto verifyQuarantinedReset = [&](TestFailurePoint a_failurePoint, std::string_view a_name) {
		const auto resetRoot = root / (std::string("reset-reopen-") + std::string(a_name));
		std::filesystem::create_directories(resetRoot);
		const auto first = resetRoot / "Optimized.A.csxpack";
		const auto second = resetRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string resetError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(failed.InitializeEmptyFilesAndOpen(&resetError));
		assert(failed.Append(MakeEntry("old", "old-exact", 0x77), &resetError));
		assert(failed.Checkpoint(&resetError));
		SetTestFailurePoints(static_cast<std::uint32_t>(a_failurePoint));
		assert(failed.Reset(&resetError) == ResetDisposition::CommittedDegraded);
		assert(!resetError.empty());
		assert(!failed.GetStats().available);
		assert(!failed.Find("old-exact", &resetError));

		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		assert(recovered.Open(&resetError));
		assert(recovered.GetStats().available);
		assert(!recovered.Find("old-exact", &resetError));
	};
	verifyQuarantinedReset(TestFailurePoint::BeforeStoreAdmissionCommit, "before-commit");
	verifyQuarantinedReset(TestFailurePoint::DuringStoreAdmissionCommit, "during-commit");
	verifyQuarantinedReset(TestFailurePoint::BeforeFinalResetReopen, "final-reopen");

	// A reset-target failure is "before commit" only when no physical mutation
	// began. The prior coherent Store remains usable in that proven case.
	{
		const auto resetRoot = root / "reset-before-mutation";
		std::filesystem::create_directories(resetRoot);
		const auto first = resetRoot / "Optimized.A.csxpack";
		const auto second = resetRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string resetError;
		Store unchanged(first, second, Lane::Optimized, TestPackSetId());
		assert(unchanged.InitializeEmptyFilesAndOpen(&resetError));
		assert(unchanged.Append(MakeEntry("old", "old-exact", 0x78), &resetError));
		assert(unchanged.Checkpoint(&resetError));
		SetTestFailurePoints(static_cast<std::uint32_t>(TestFailurePoint::BeforeInitializeMutation));
		assert(unchanged.Reset(&resetError) == ResetDisposition::FailedBeforeCommit);
		assert(unchanged.GetStats().available);
		assert(unchanged.Find("old-exact", &resetError));
	}

	// Once reset-target mutation begins, every false return or exception is
	// commit-uncertain (or known durable), invalidates stale authority, and
	// releases path and writer ownership while the failed Store remains alive.
	auto verifyUncertainReset = [&](TestFailurePoint a_failurePoint, std::string_view a_name, bool a_pairRemainsAdmissible) {
		const auto resetRoot = root / (std::string("reset-initialize-") + std::string(a_name));
		const auto movedRoot = root / (std::string("reset-initialize-moved-") + std::string(a_name));
		std::filesystem::create_directories(resetRoot);
		const auto first = resetRoot / "Optimized.A.csxpack";
		const auto second = resetRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string resetError;
		Store failed(first, second, Lane::Optimized, TestPackSetId());
		assert(failed.InitializeEmptyFilesAndOpen(&resetError));
		assert(failed.Append(MakeEntry("old", "old-exact", 0x79), &resetError));
		assert(failed.Checkpoint(&resetError));
		SetTestFailurePoints(static_cast<std::uint32_t>(a_failurePoint));
		assert(failed.Reset(&resetError) == ResetDisposition::CommittedDegraded);
		assert(!resetError.empty());
		assert(!failed.GetStats().available);
		assert(!failed.Find("old-exact", &resetError));

		std::error_code renameError;
		std::filesystem::rename(resetRoot, movedRoot, renameError);
		assert(!renameError);
		std::filesystem::rename(movedRoot, resetRoot, renameError);
		assert(!renameError);

		Store recovered(first, second, Lane::Optimized, TestPackSetId());
		if (a_pairRemainsAdmissible) {
			assert(recovered.Open(&resetError));
			assert(!recovered.Find("old-exact", &resetError));
		} else {
			assert(!recovered.Open(&resetError));
			std::ofstream(first, std::ios::binary | std::ios::trunc).close();
			std::ofstream(second, std::ios::binary | std::ios::trunc).close();
			assert(recovered.InitializeEmptyFilesAndOpen(&resetError));
		}
	};
	verifyUncertainReset(TestFailurePoint::AfterInitializeTruncate, "truncate-false", false);
	verifyUncertainReset(TestFailurePoint::ThrowAfterInitializeTruncate, "truncate-throw", false);
	verifyUncertainReset(TestFailurePoint::AfterInitializeWrite, "write-false", true);
	verifyUncertainReset(TestFailurePoint::ThrowAfterInitializeWrite, "write-throw", true);
	verifyUncertainReset(TestFailurePoint::AfterInitializeDurableFlush, "durable-false", true);
	verifyUncertainReset(TestFailurePoint::ThrowAfterInitializeDurableFlush, "durable-throw", true);
#endif

	// The retained physical-identity guards prevent a backing path from being
	// replaced while the writer lease is active. Once closed, a missing member
	// fails the next read-only admission without mutating its peer.
	const auto guardedPath = ActivePack(a, b);
	std::error_code guardedRemoveError;
	assert(!std::filesystem::remove(guardedPath, guardedRemoveError));
	assert(guardedRemoveError);
	recovered.Close();
	assert(std::filesystem::remove(guardedPath));
	error.clear();
	Store missingMember(a, b, Lane::Optimized, TestPackSetId());
	assert(!missingMember.Open(&error));
	assert(!error.empty());

	// One failed lane must not disable an independent healthy lane.
	const auto laneRoot = root / "lane-isolation";
	std::filesystem::create_directories(laneRoot);
	const auto optimizedA = laneRoot / "Optimized.A.csxpack";
	const auto optimizedB = laneRoot / "Optimized.B.csxpack";
	const auto developerA = laneRoot / "Developer.A.csxpack";
	const auto developerB = laneRoot / "Developer.B.csxpack";
	std::ofstream(optimizedA, std::ios::binary).close();
	std::ofstream(optimizedB, std::ios::binary).close();
	std::ofstream(developerA, std::ios::binary).close();
	Store healthyLane(optimizedA, optimizedB, Lane::Optimized, TestPackSetId());
	Store incompleteLane(developerA, developerB, Lane::Developer, TestPackSetId());
	error.clear();
	assert(healthyLane.InitializeEmptyFilesAndOpen(&error));
	error.clear();
	assert(!incompleteLane.Open(&error));
	assert(!error.empty());
	assert(healthyLane.Append(MakeEntry("healthy", "healthy-exact", 0x61), &error));
	assert(healthyLane.Checkpoint(&error));
	assert(healthyLane.Find("healthy-exact", &error));
	healthyLane.Close();
	incompleteLane.Close();

	std::filesystem::remove_all(root);
	return 0;
}
