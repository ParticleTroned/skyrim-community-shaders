#pragma once

#include <string>
#include <unordered_map>

namespace Util
{
	// --- SPID Helpers (load-order-portable FormID format) ---
	// SPID format: "0x{localFormId}~{pluginName}" e.g. "0x12F89E~Skyrim.esm"

	struct SpidComponents
	{
		uint32_t localFormId = 0;
		std::string pluginName;
	};

	/// Parse a SPID string into components.
	SpidComponents ParseSpid(const std::string& spid);

	/// Format a SPID string from components.
	std::string FormatSpid(uint32_t localFormId, const std::string& pluginName);

	/// Convert a runtime FormID to a portable SPID string.
	std::string FormIdToSpid(RE::FormID formId);

	/// Resolve a SPID string to a runtime FormID (0 if not found).
	RE::FormID SpidToFormId(const std::string& spid);

	/// Get a display name: "EditorID (localFormId)" or SPID fallback.
	std::string GetFormDisplayName(RE::FormID formId);

	/// Owns a lazy editor-ID map snapshot for one initialization batch.
	class FormEditorIDIndex
	{
	public:
		/// Find the first map alias for a form, creating the snapshot on first use.
		std::string Find(const RE::TESForm* form);

	private:
		std::unordered_map<const RE::TESForm*, std::string> editorIDs;
		bool initialized = false;
	};

	/// Get the EditorID for a form, or empty string if not available.
	/// @param index Optional batch-local snapshot for fallback lookups.
	std::string GetFormEditorID(const RE::TESForm* form, FormEditorIDIndex* index = nullptr);

	/// Get a file-safe key for a form: always SPID format for load-order independence.
	std::string GetFormFileKey(const RE::TESForm* form);
}
