#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace BuildProvenance
{
	std::string_view GetBuildId();
	std::string_view GetShaderCacheAbiId();
	const std::string& GetArtifactSha256();
	const std::string& GetShaderCompilerIdentity();
	nlohmann::json GetProducer();
	std::optional<nlohmann::json> ValidateExpectedBuild(const nlohmann::json& a_args);
	void AttachProducer(nlohmann::json& a_output);
	void LogRuntimeIdentity();
}
