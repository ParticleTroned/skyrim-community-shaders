#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace SettingsSerialization
{
	enum class CanonicalizationResult
	{
		Unchanged,
		Rewritten,
		Error
	};

	/**
	 * Serializes a settings object in the same top-to-bottom group order as the UI.
	 * Groups are separated with JSON-safe blank lines.
	 */
	std::string Serialize(const nlohmann::json& a_settings);

	/**
	 * Writes settings using the canonical UI order and an atomic file replacement.
	 */
	bool WriteFileAtomic(
		const std::filesystem::path& a_path,
		const nlohmann::json& a_settings,
		std::string& o_errorMessage);

	/**
	 * Rewrites a valid settings file only when its current text is not canonical.
	 * The supplied parsed JSON is serialized directly so unknown settings survive.
	 * Files with duplicate object keys are loaded normally but not auto-rewritten.
	 */
	CanonicalizationResult CanonicalizeFile(
		const std::filesystem::path& a_path,
		const nlohmann::json& a_settings,
		std::string& o_errorMessage);
}
