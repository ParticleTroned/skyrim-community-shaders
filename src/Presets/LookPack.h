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
		std::vector<std::string> unavailable;
		std::vector<std::string> notices;
		bool requiresPostProcessing = false;
		bool IsCompatible() const { return errors.empty() && unavailable.empty(); }
		/** Separates malformed profiles from profiles requiring unavailable capabilities. */
		std::string_view CompatibilityStatus() const;
		/** Short card/header message; detailed requirements belong in the tooltip. */
		std::string CompatibilitySummary() const;
		/** Includes every validation error and unavailable requirement without losing either. */
		std::string CompatibilityDetails() const;
	};

	/** Reads a bounded JSON object, rejecting duplicate keys and excessive nesting. */
	Json ReadProfileDocument(const std::filesystem::path& path);
	/** Requires loaded settings and, for PP, reviewed profile ownership before accepting imports. */
	bool IsLookFeatureAvailable(std::string_view feature, const Json& schemas);
	/** Accepts one portable folder name, never a path or Windows device name. */
	bool IsPackId(std::string_view id);
	/** Reads upstream Baseline/InteriorOnly documents and reports unavailable capabilities. */
	LookPack ReadLookPack(const std::filesystem::path& root, const std::string& id, const Json& schemas);
	/** Publishes a new pack only after all files are written; existing packs are never overwritten. */
	bool WriteLookPack(const std::filesystem::path& root, const LookPack& pack, const Json& schemas, std::string& error);
}
