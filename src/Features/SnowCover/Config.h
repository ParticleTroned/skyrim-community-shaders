#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace SnowConfig
{
	using json = nlohmann::json;
	/** Shared finite ranges and types for persisted settings and DevBench. */
	const json& Schemas();
	/** Rejects malformed or unknown values before changing active settings. */
	void Validate(const json& value, const json& schema, const std::string& path);
	/** Checks complete world settings, including vector and range invariants. */
	void ValidateWorld(const json& config);
}
