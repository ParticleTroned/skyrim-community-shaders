#include "BuildProvenance.h"

#include "Utils/FileSystem.h"
#include "Utils/WinApi.h"

#include <BuildProvenance.generated.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace
{
	using json = nlohmann::json;

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
			const auto modulePath = Util::PathHelpers::GetCurrentModuleRealPath();
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
			wchar_t path[MAX_PATH]{};
			const auto module = GetModuleHandleW(L"d3dcompiler_47.dll");
			if (!module || !GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))))
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
