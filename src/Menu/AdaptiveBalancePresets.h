#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace MenuUI::AdaptiveBalancePresets
{
	/** Session-only destination and navigation for the in-menu export folder browser. */
	struct ExportFolder
	{
		static constexpr std::size_t MaxPathBytes = 4096;
		std::filesystem::path directory, browsing;
		std::string input, error;
		std::vector<std::filesystem::path> folders;
		std::filesystem::path selection;
		bool includeFiles = false;
		bool allowFolder = true;
		std::string title = "Export folder";
	};
	/** Selects an existing absolute folder; empty restores the default. Failure preserves the destination. */
	bool SetExportFolder(ExportFolder& a_folder, std::string_view a_path, std::string& a_error);
	/** Shared in-menu file/folder picker, including VR. Returns true after a selection. */
	bool DrawPathPicker(const char* label, ExportFolder& state, const std::filesystem::path& defaultFolder, bool files, bool folders);
}
