#include "AdaptiveBalancePresets.h"
#include "SettingsPage.h"

#include "Utils/StringUtils.h"
#include "Utils/UI.h"
#include <algorithm>
#include <format>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <string_view>

namespace MenuUI::AdaptiveBalancePresets
{
	namespace
	{
		constexpr std::size_t maximumFolderEntries = 512;

		void RenderLine(ImDrawList* a_draw, ImVec2 a_start, float a_width, const char* a_text)
		{
			const std::string_view text(a_text);
			const auto lineEnd = text.find_first_of("\r\n");
			const auto* textEnd = a_text + (lineEnd == std::string_view::npos ? text.size() : lineEnd);
			const auto textSize = ImGui::CalcTextSize(a_text, textEnd);
			const ImVec2 end{ a_start.x + std::max(1.0f, a_width), a_start.y + ImGui::GetTextLineHeight() };
			ImGui::RenderTextEllipsis(a_draw, a_start, end, end.x, a_text, textEnd, &textSize);
		}

		void SingleLine(const char* a_text, float a_width)
		{
			RenderLine(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), a_width, a_text);
			ImGui::Dummy({ std::max(1.0f, a_width), ImGui::GetTextLineHeight() });
			Util::AddTooltip(a_text);
		}

		bool ResolveFolder(std::string_view a_text, std::filesystem::path& a_path, std::string& a_error)
		{
			if (a_text.empty() || a_text.size() > ExportFolder::MaxPathBytes || a_text.find('\0') != a_text.npos) {
				a_error = std::format("Enter a full folder path of at most {} UTF-8 bytes.", ExportFolder::MaxPathBytes);
				return false;
			}
			try {
				const auto path = std::filesystem::path(std::u8string(a_text.begin(), a_text.end()));
				std::error_code error;
				if (!path.is_absolute() || !std::filesystem::is_directory(path, error) || error) {
					a_error = "Choose an existing folder using its full path.";
					return false;
				}
				a_path = std::filesystem::canonical(path, error);
				if (error) {
					a_error = std::format("Could not resolve the folder: {}", error.message());
					return false;
				}
				a_error.clear();
				return true;
			} catch (const std::exception& error) {
				a_error = std::format("Could not read the folder path: {}", error.what());
				return false;
			}
		}

		void BrowseFolder(ExportFolder& a_folder, const std::filesystem::path& a_path)
		{
			std::filesystem::path resolved;
			if (!ResolveFolder(Util::PathToUtf8(a_path), resolved, a_folder.error))
				return;
			a_folder.browsing = std::move(resolved);
			a_folder.input = Util::PathToUtf8(a_folder.browsing);
			a_folder.folders.clear();
			std::error_code error;
			std::filesystem::directory_iterator iterator(a_folder.browsing, error), end;
			std::size_t visited = 0;
			for (; !error && iterator != end; iterator.increment(error)) {
				std::error_code typeError;
				if ((iterator->is_directory(typeError) || (a_folder.includeFiles && iterator->is_regular_file(typeError) && Util::IEndsWithAsciiInsensitive(Util::PathToUtf8(iterator->path().extension()), ".json"))) && !typeError)
					a_folder.folders.push_back(iterator->path());
				if (typeError && a_folder.error.empty())
					a_folder.error = std::format("Some entries could not be inspected: {}", typeError.message());
				if (++visited >= maximumFolderEntries) {
					a_folder.error = "Folder listing limited. Enter a full path to reach an unlisted folder.";
					break;
				}
			}
			if (error)
				a_folder.error = std::format("Could not list folders: {}", error.message());
			std::ranges::sort(a_folder.folders);
		}

		void BeginFolderBrowse(ExportFolder& a_folder, std::filesystem::path a_defaultFolder)
		{
			a_folder.error.clear();
			if (!a_folder.directory.empty())
				a_defaultFolder = a_folder.directory;
			std::error_code error;
			while (!a_defaultFolder.empty() && !std::filesystem::is_directory(a_defaultFolder, error)) {
				const auto parent = a_defaultFolder.parent_path();
				if (parent == a_defaultFolder)
					break;
				a_defaultFolder = parent;
			}
			BrowseFolder(a_folder, a_defaultFolder);
		}

		void DrawFolderBrowser(ExportFolder& a_folder, float a_width, ImVec2 a_anchor)
		{
			const auto parent = ImGui::GetWindowPos();
			const auto parentSize = ImGui::GetWindowSize();
			const float height = std::min(parentSize.y, ImGui::GetTextLineHeightWithSpacing() * 11 + ImGui::GetStyle().WindowPadding.y * 2);
			ImGui::SetNextWindowPos({ a_anchor.x, std::clamp(a_anchor.y, parent.y, parent.y + parentSize.y - height) }, ImGuiCond_Always);
			ImGui::SetNextWindowSize({ a_width, height }, ImGuiCond_Always);
			if (!ImGui::BeginPopup("##ExportFolderPicker"))
				return;
			ImGui::TextUnformatted(a_folder.title.c_str());
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##ExportFolderPath", a_folder.includeFiles ? "Full file or folder path" : "Full folder path", &a_folder.input);
			Util::AddTooltip(a_folder.includeFiles ? "Enter a full file or folder path, then use Go." : "Enter a full folder path, then use Go.");
			if (ImGui::Button("Go", {})) {
				try {
					std::error_code error;
					const auto path = std::filesystem::path(std::u8string(a_folder.input.begin(), a_folder.input.end()));
					if (a_folder.includeFiles && a_folder.input.size() <= ExportFolder::MaxPathBytes && a_folder.input.find('\0') == std::string::npos &&
						path.is_absolute() && Util::IEndsWithAsciiInsensitive(Util::PathToUtf8(path.extension()), ".json") && std::filesystem::is_regular_file(path, error)) {
						a_folder.selection = path;
						ImGui::CloseCurrentPopup();
					}
					std::filesystem::path resolved;
					if (a_folder.selection.empty() && ResolveFolder(a_folder.input, resolved, a_folder.error))
						BrowseFolder(a_folder, resolved);
				} catch (const std::exception& error) {
					a_folder.error = error.what();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Parent folder", {}))
				BrowseFolder(a_folder, a_folder.browsing.parent_path());
			ImGui::SameLine();
			if (ImGui::Button("Drive root", {}))
				BrowseFolder(a_folder, a_folder.browsing.root_path());
			std::filesystem::path navigate;
			const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
			const float listHeight = std::max(ImGui::GetTextLineHeight(), ImGui::GetContentRegionAvail().y - footerHeight);
			ImGui::BeginChild("##ExportSubfolders", { 0, listHeight }, ImGuiChildFlags_Borders);
			for (const auto& folder : a_folder.folders) {
				const auto fullPath = Util::PathToUtf8(folder);
				const auto name = Util::PathToUtf8(folder.filename());
				ImGui::PushID(fullPath.c_str());
				const SKSE::stl::scope_exit restoreFolderId([] { ImGui::PopID(); });
				const auto origin = ImGui::GetCursorScreenPos();
				const auto width = ImGui::GetContentRegionAvail().x;
				if (ImGui::Selectable("##Subfolder", false, ImGuiSelectableFlags_NoAutoClosePopups, { width, ImGui::GetTextLineHeight() }))
					navigate = folder;
				RenderLine(ImGui::GetWindowDrawList(), origin, width, name.c_str());
				Util::AddTooltip(fullPath.c_str());
			}
			if (a_folder.folders.empty())
				SingleLine(a_folder.includeFiles ? "No folders or JSON profiles." : "No subfolders.", ImGui::GetContentRegionAvail().x);
			ImGui::EndChild();
			if (!navigate.empty()) {
				std::error_code error;
				if (a_folder.includeFiles && std::filesystem::is_regular_file(navigate, error)) {
					a_folder.selection = navigate;
					ImGui::CloseCurrentPopup();
				} else
					BrowseFolder(a_folder, navigate);
			}
			SingleLine(a_folder.error.empty() ? "Select a subfolder, or use the folder path above." : a_folder.error.c_str(), ImGui::GetContentRegionAvail().x);
			if (a_folder.allowFolder && ImGui::Button("Use this folder", {})) {
				if (a_folder.input.empty())
					a_folder.error = "Enter a folder path, or select Default folder.";
				else if (SetExportFolder(a_folder, a_folder.input, a_folder.error)) {
					a_folder.selection = a_folder.directory;
					ImGui::CloseCurrentPopup();
				}
			}
			if (a_folder.allowFolder && !a_folder.includeFiles) {
				ImGui::SameLine();
				if (ImGui::Button("Default folder", {})) {
					SetExportFolder(a_folder, {}, a_folder.error);
					ImGui::CloseCurrentPopup();
				}
			}
			if (a_folder.allowFolder)
				ImGui::SameLine();
			if (ImGui::Button("Cancel", {}))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

	}

	bool DrawPathPicker(const char* label, ExportFolder& state, const std::filesystem::path& defaultFolder, bool files, bool folders)
	{
		ImGui::PushID(label);
		const SKSE::stl::scope_exit restore([] { ImGui::PopID(); });
		state.includeFiles = files;
		state.allowFolder = folders;
		state.title = label;
		const auto anchor = ImGui::GetCursorScreenPos();
		const auto width = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button(label)) {
			state.selection.clear();
			BeginFolderBrowse(state, defaultFolder);
			ImGui::OpenPopup("##ExportFolderPicker");
		}
		const auto previous = state.selection;
		DrawFolderBrowser(state, width, anchor);
		return state.selection != previous && !state.selection.empty();
	}

	bool SetExportFolder(ExportFolder& a_folder, std::string_view a_path, std::string& a_error)
	{
		if (a_path.empty()) {
			a_folder.directory.clear();
			a_error.clear();
			return true;
		}
		std::filesystem::path resolved;
		if (!ResolveFolder(a_path, resolved, a_error))
			return false;
		a_folder.directory = std::move(resolved);
		return true;
	}

}
