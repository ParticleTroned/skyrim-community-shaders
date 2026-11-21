#pragma once

#include "SettingsDomains.h"
#include <filesystem>
#include <map>
#include <vector>

namespace Presets
{
	struct LookPack
	{
		std::string id;
		std::filesystem::path sourceRoot;
		std::map<std::string, Json> scenes, documents;
		std::map<std::string, bool> periodicContexts;
		std::vector<std::filesystem::path> assets;
		Json manifest = Json::object();
		Json baseline = Json::object();
		Json interior = Json::object();
		std::vector<std::string> errors;
		std::vector<std::string> notices;
		bool IsCompatible() const { return errors.empty(); }
	};

	/** Reads a bounded JSON object, rejecting duplicate keys and excessive nesting. */
	Json ReadProfileDocument(const std::filesystem::path& path);
	/** Accepts one portable folder name, never a path or Windows device name. */
	bool IsPackId(std::string_view id);
	/** Reads upstream Baseline/InteriorOnly documents and reports unavailable capabilities. */
	LookPack ReadLookPack(const std::filesystem::path& root, const std::string& id, const Json& schemas);
	/** Publishes a new pack only after all files are written; existing packs are never overwritten. */
	bool WriteLookPack(const std::filesystem::path& root, const LookPack& pack, const Json& schemas, std::string& error);
}
