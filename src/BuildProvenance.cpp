#include "BuildProvenance.h"

#include "Utils/FileSystem.h"
#include "Utils/WinApi.h"

#include <BuildProvenance.generated.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <Psapi.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace
{
	using json = nlohmann::json;

	std::filesystem::path GetMappedModulePath(HMODULE a_module)
	{
		if (!a_module)
			return {};

		std::vector<wchar_t> mappedPath(32768);
		const auto mappedLength = GetMappedFileNameW(
			GetCurrentProcess(), a_module, mappedPath.data(), static_cast<DWORD>(mappedPath.size()));
		if (mappedLength == 0 || mappedLength >= mappedPath.size())
			return {};
		const std::wstring mapped(mappedPath.data(), mappedLength);
		if (mapped.starts_with(LR"(\\?\)"))
			return std::filesystem::path(mapped.substr(4));
		if (!mapped.starts_with(LR"(\Device\)"))
			return std::filesystem::path(mapped);

		std::array<wchar_t, 512> drives{};
		const auto driveLength = GetLogicalDriveStringsW(static_cast<DWORD>(drives.size()), drives.data());
		if (driveLength == 0 || driveLength >= drives.size())
			return {};
		for (const wchar_t* drive = drives.data(); *drive; drive += std::wcslen(drive) + 1) {
			if (std::wcslen(drive) < 2)
				continue;
			const std::wstring driveName(drive, 2);
			std::array<wchar_t, 32768> device{};
			if (!QueryDosDeviceW(driveName.c_str(), device.data(), static_cast<DWORD>(device.size())))
				continue;
			const std::wstring_view deviceName(device.data());
			if (mapped.size() < deviceName.size() ||
				_wcsnicmp(mapped.c_str(), deviceName.data(), deviceName.size()) != 0) {
				continue;
			}
			return std::filesystem::path(driveName + mapped.substr(deviceName.size()));
		}
		return {};
	}

	std::filesystem::path GetCurrentModuleBackingPath()
	{
		HMODULE selfModule = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentModuleBackingPath),
				&selfModule)) {
			return {};
		}
		return GetMappedModulePath(selfModule);
	}

	std::string Sha256File(const std::filesystem::path& a_path)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		DWORD objectSize = 0;
		DWORD resultSize = 0;
		std::vector<UCHAR> object;
		std::array<UCHAR, 32> digest{};
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
			return {};

		auto cleanup = [&]() {
			if (hash)
				BCryptDestroyHash(hash);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
		};
		if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0) < 0) {
			cleanup();
			return {};
		}
		object.resize(objectSize);
		if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
			cleanup();
			return {};
		}

		std::array<char, 1024 * 1024> buffer{};
		while (stream) {
			stream.read(buffer.data(), buffer.size());
			const auto count = stream.gcount();
			if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0) {
				cleanup();
				return {};
			}
		}
		if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
			cleanup();
			return {};
		}
		cleanup();

		std::ostringstream output;
		output << std::hex << std::setfill('0');
		for (const auto byte : digest)
			output << std::setw(2) << static_cast<unsigned>(byte);
		return output.str();
	}

	struct RuntimeIdentity
	{
		std::string artifactSha256;
		std::string expectedArtifactSha256;
		std::string sidecarError;
		bool sidecarVerified = false;
	};

	const RuntimeIdentity& GetRuntimeIdentity()
	{
		static RuntimeIdentity identity = []() {
			RuntimeIdentity result;
			// GetModuleFileName/GetModuleFileNameEx returns MO2's virtual Data path.
			// Reading the loaded DLL back through that USVFS path can fail to reach
			// EOF during SKSEPlugin_Load. Query the mapped image backing file instead;
			// if it cannot be proven, fail verification without touching the virtual
			// path or blocking game startup.
			const auto modulePath = GetCurrentModuleBackingPath();
			if (modulePath.empty()) {
				result.sidecarError = "physical module backing path unavailable";
				return result;
			}
			result.artifactSha256 = Sha256File(modulePath);
			try {
				const auto manifestPath = modulePath.parent_path() / L"CSX.BuildManifest.json";
				std::ifstream stream(manifestPath);
				if (!stream) {
					result.sidecarError = "CSX.BuildManifest.json not found beside DLL";
					return result;
				}
				const auto manifest = json::parse(stream);
				if (manifest.value("buildId", std::string()) != CSX::EmbeddedBuildProvenance::BUILD_ID) {
					result.sidecarError = "sidecar Build ID does not match embedded Build ID";
					return result;
				}
				result.expectedArtifactSha256 = manifest.value("artifact", json::object()).value("sha256", std::string());
				result.sidecarVerified = !result.artifactSha256.empty() && result.artifactSha256 == result.expectedArtifactSha256;
				if (!result.sidecarVerified)
					result.sidecarError = "sidecar artifact SHA-256 does not match loaded DLL";
			} catch (const std::exception& error) {
				result.sidecarError = error.what();
			}
			return result;
		}();
		return identity;
	}
}

namespace BuildProvenance
{
	std::string_view GetBuildId() { return CSX::EmbeddedBuildProvenance::BUILD_ID; }
	std::string_view GetShaderCacheAbiId() { return CSX::EmbeddedBuildProvenance::SHADER_CACHE_ABI_ID; }
	const std::string& GetArtifactSha256() { return GetRuntimeIdentity().artifactSha256; }

	const std::string& GetShaderCompilerIdentity()
	{
		static const std::string identity = []() {
			const auto module = GetModuleHandleW(L"d3dcompiler_47.dll");
			const auto path = GetMappedModulePath(module);
			if (path.empty())
				return std::string("d3dcompiler_47.dll:unavailable");
			const auto version = Util::GetDllVersion(path);
			return std::format("d3dcompiler_47.dll:{}:{}", version ? version->string() : "unknown", Sha256File(path));
		}();
		return identity;
	}

	nlohmann::json GetProducer()
	{
		const auto& runtime = GetRuntimeIdentity();
		return {
			{ "component", "CommunityShaders" },
			{ "buildId", CSX::EmbeddedBuildProvenance::BUILD_ID },
			{ "buildIdShort", CSX::EmbeddedBuildProvenance::BUILD_ID_SHORT },
			{ "sourceCommit", CSX::EmbeddedBuildProvenance::SOURCE_COMMIT },
			{ "sourceDescribe", CSX::EmbeddedBuildProvenance::SOURCE_DESCRIBE },
			{ "configuration", CSX::EmbeddedBuildProvenance::CONFIGURATION },
			{ "sourceDirty", CSX::EmbeddedBuildProvenance::SOURCE_DIRTY },
			{ "artifactSha256", runtime.artifactSha256 },
			{ "manifestVerified", runtime.sidecarVerified },
			{ "manifestError", runtime.sidecarError.empty() ? json(nullptr) : json(runtime.sidecarError) },
			{ "shaderCacheAbiId", CSX::EmbeddedBuildProvenance::SHADER_CACHE_ABI_ID },
			{ "shaderCompilerIdentity", GetShaderCompilerIdentity() },
		};
	}

	std::optional<nlohmann::json> ValidateExpectedBuild(const nlohmann::json& a_args)
	{
		if (!a_args.contains("expectedBuildId"))
			return std::nullopt;
		if (!a_args["expectedBuildId"].is_string())
			return json{ { "error", "expectedBuildId must be a string" }, { "code", "invalid_expected_build_id" }, { "producer", GetProducer() } };
		const auto expected = a_args["expectedBuildId"].get<std::string>();
		if (expected != GetBuildId())
			return json{ { "error", "loaded CSX build does not match requested producer" }, { "code", "producer_mismatch" }, { "expectedBuildId", expected }, { "producer", GetProducer() } };
		return std::nullopt;
	}

	void AttachProducer(nlohmann::json& a_output) { a_output["producer"] = GetProducer(); }

	void LogRuntimeIdentity()
	{
		const auto& runtime = GetRuntimeIdentity();
		logger::info("Build provenance: ID {}, source {}, artifact SHA-256 {}, manifest verified: {}",
			CSX::EmbeddedBuildProvenance::BUILD_ID_SHORT, CSX::EmbeddedBuildProvenance::SOURCE_DESCRIBE,
			runtime.artifactSha256, runtime.sidecarVerified);
		if (!runtime.sidecarVerified)
			logger::warn("Build provenance sidecar verification failed: {}", runtime.sidecarError);
	}
}
