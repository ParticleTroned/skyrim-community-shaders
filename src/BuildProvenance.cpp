#include "BuildProvenance.h"

#include <BuildProvenance.generated.h>
#include <nlohmann/json.hpp>
#include <winver.h>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <DevBenchAPI.h>
#	include <atomic>
#endif

namespace
{
	using json = nlohmann::json;

	struct RuntimeIdentity
	{
		std::string artifactSha256;
		std::string sidecarError;
		bool sidecarVerified = false;
	};

	const RuntimeIdentity& GetRuntimeIdentity()
	{
		// Never open or hash the loaded DLL from SKSEPlugin_Load. MO2/USVFS may
		// expose the module through a virtual path whose read never reaches EOF,
		// blocking the game loader. The embedded Build ID is the runtime identity;
		// artifact SHA verification belongs to the deployment harness, where the
		// physical DLL and its bound sidecar are both available.
		static const RuntimeIdentity identity{
			.artifactSha256 = {},
			.sidecarError = "runtime artifact verification delegated to deployment harness",
			.sidecarVerified = false,
		};
		return identity;
	}

	std::string GetLoadedModuleVersion(HMODULE a_module)
	{
		if (!a_module)
			return "unavailable";
		const auto resource = FindResourceW(a_module, MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION);
		const auto loaded = resource ? LoadResource(a_module, resource) : nullptr;
		const auto data = loaded ? LockResource(loaded) : nullptr;
		if (!data)
			return "unknown";

		VS_FIXEDFILEINFO* version = nullptr;
		UINT versionSize = 0;
		if (!VerQueryValueW(data, L"\\", reinterpret_cast<void**>(&version), &versionSize) ||
			!version || versionSize < sizeof(VS_FIXEDFILEINFO) || version->dwSignature != 0xFEEF04BD) {
			return "unknown";
		}
		return std::format("{}.{}.{}.{}",
			HIWORD(version->dwFileVersionMS), LOWORD(version->dwFileVersionMS),
			HIWORD(version->dwFileVersionLS), LOWORD(version->dwFileVersionLS));
	}
}

namespace BuildProvenance
{
	void InstallDevBench()
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		static std::atomic_bool registered{ false };
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host || registered.exchange(true, std::memory_order_acq_rel))
			return;
		static constexpr auto descriptor = R"({"description":"Read-only CSX producer registry. Reports the embedded build identity, source commit and dirty-source state; physical DLL hashing remains the deployment harness responsibility. This endpoint does not claim any rendering service is available.","inputSchema":{"type":"object","required":["contractMajor","action","clientId","commandId"],"properties":{"contractMajor":{"type":"integer","const":1},"action":{"type":"string","enum":["registry","capabilities"]},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128}}}})";
		host->RegisterTool("communityshaders.build_api", descriptor, [](void*, const char* request, void* sink, DevBenchAPI::WriteFn write) noexcept {
				try {
					const auto args = json::parse(request ? request : "{}");
					const auto action = args.at("action").get<std::string>();
					if (!args.at("contractMajor").is_number_integer() || args.at("contractMajor") != 1 ||
						(action != "registry" && action != "capabilities"))
						throw std::invalid_argument("unsupported producer registry request");
					for (const auto field : { "clientId", "commandId" }) {
						const auto value = args.at(field).get<std::string>();
						if (value.empty() || value.size() > 128)
							throw std::invalid_argument("invalid clientId or commandId");
					}
					const json result{ { "ok", true }, { "contractMajor", 1 }, { "action", action },
						{ "producer", GetProducer() }, { "readOnly", true } };
					const auto text = result.dump();
					write(sink, text.c_str());
				} catch (const std::exception& error) {
					const auto text = json{ { "ok", false }, { "error", error.what() } }.dump();
					write(sink, text.c_str());
				} catch (...) {
					write(sink, R"({"ok":false,"error":"producer registry failure"})");
				} }, nullptr);
		logger::info("Registered communityshaders.build_api with embedded build {}", GetBuildId());
#endif
	}

	std::string_view GetBuildId() { return CSX::EmbeddedBuildProvenance::BUILD_ID; }
	std::string_view GetShaderCacheAbiId() { return CSX::EmbeddedBuildProvenance::SHADER_CACHE_ABI_ID; }
	const std::string& GetArtifactSha256() { return GetRuntimeIdentity().artifactSha256; }

	const std::string& GetShaderCompilerIdentity()
	{
		static const std::string identity = []() {
			const auto module = GetModuleHandleW(L"d3dcompiler_47.dll");
			return std::format("d3dcompiler_47.dll:{}", GetLoadedModuleVersion(module));
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
		logger::info("Build provenance: ID {}, source {}, runtime artifact verification delegated to deployment harness",
			CSX::EmbeddedBuildProvenance::BUILD_ID_SHORT, CSX::EmbeddedBuildProvenance::SOURCE_DESCRIBE);
	}
}
