#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Presets
{
	using Json = nlohmann::json;
	enum class SettingsDomain
	{
		Appearance,
		Performance,
		Local
	};
	/** Limits profile snapshots to reviewed feature serializers. */
	bool HasProfileSettings(std::string_view feature);

	/** Classifies a serialized setting path; unreviewed features remain local. */
	SettingsDomain Classify(std::string_view feature, std::string_view path);
	/** Selects one ownership domain without changing the feature's serialization format. */
	Json SelectDomain(std::string_view feature, const Json& settings, SettingsDomain domain);
	/** Replaces appearance, preserving quality controls in context and location arrays. */
	Json ReplaceAppearance(std::string_view feature, const Json& current, const Json& appearance);
	/** Keeps local/quality settings and appearance edits; mixed arrays remain atomic user values. */
	Json PersonalSettings(std::string_view feature, const Json& current, const Json& baseline);
	/** Recursive object overlay; arrays are complete authored values. */
	void Overlay(Json& target, const Json& patch);
	/** Returns only edits relative to a baseline, treating arrays as complete values. */
	Json Difference(const Json& current, const Json& baseline);
	/** Removes keys supplied by a patch, including overlapping nested objects. */
	void RemoveCovered(Json& edits, const Json& patch);
	/** Rejects unrecognized keys, incompatible types and non-finite values before mutation. */
	bool ValidatePatch(const Json& patch, const Json& schema, std::string& error, const std::string& path = "");
}
