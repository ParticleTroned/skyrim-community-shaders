#include "AdaptiveBalancePresets.h"
#include "I18n/I18n.h"

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
		std::array<const char*, 6> CollectionNames()
		{
			return { T("menu.adaptive_balance_presets.collection_full", "Complete setup"), T("menu.adaptive_balance_presets.collection_base", "Base appearance"), T("menu.adaptive_balance_presets.collection_locations", "All locations"), T("menu.adaptive_balance_presets.collection_worldspace", "Worldspace"), T("menu.adaptive_balance_presets.collection_region", "Location"), T("menu.adaptive_balance_presets.collection_city", "City") };
		}
		constexpr std::array ids{ "full", "base", "locations", "worldspace", "region", "city" };
		static_assert(std::tuple_size_v<decltype(CollectionNames())> == static_cast<std::size_t>(Collection::Count) && ids.size() == static_cast<std::size_t>(Collection::Count));
		constexpr float cardPadding = .4f;
		constexpr float cardHeight = 2.4f;
		constexpr float minimumNameWidth = 3.0f;
		constexpr std::size_t maximumFolderEntries = 512;

		struct TransferLayout
		{
			std::string label;
			float actionWidth, pickerWidth;
			bool separateAction;
		};

		TransferLayout GetTransferLayout(Mode a_mode, const char* a_collection, float a_width)
		{
			const auto label = fmt::format(fmt::runtime(a_mode == Mode::Import ? T("menu.adaptive_balance_presets.import_collection", "Import {}") : T("menu.adaptive_balance_presets.export_collection", "Export {}")), a_collection);
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float actionWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
			const float pickerWidth = a_mode == Mode::Import ? ImGui::CalcTextSize(T("menu.adaptive_balance_presets.choose_file", "Choose file")).x + ImGui::GetStyle().FramePadding.x * 2 + gap : 0;
			return { label, actionWidth, pickerWidth, a_width - actionWidth - pickerWidth - gap < ImGui::GetTextLineHeight() * minimumNameWidth };
		}

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
				a_error = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.folder_path_length", "Enter a full folder path of at most {} UTF-8 bytes.")), ExportFolder::MaxPathBytes);
				return false;
			}
			try {
				const auto path = std::filesystem::path(std::u8string(a_text.begin(), a_text.end()));
				std::error_code error;
				if (!path.is_absolute() || !std::filesystem::is_directory(path, error) || error) {
					a_error = T("menu.adaptive_balance_presets.folder_must_exist", "Choose an existing folder using its full path.");
					return false;
				}
				a_path = std::filesystem::canonical(path, error);
				if (error) {
					a_error = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.folder_resolve_error", "Could not resolve the folder: {}")), error.message());
					return false;
				}
				a_error.clear();
				return true;
			} catch (const std::exception& error) {
				a_error = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.folder_read_error", "Could not read the folder path: {}")), error.what());
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
				if (iterator->is_directory(typeError) && !typeError)
					a_folder.folders.push_back(iterator->path());
				if (typeError && a_folder.error.empty())
					a_folder.error = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.folder_entry_error", "Some entries could not be inspected: {}")), typeError.message());
				if (++visited >= maximumFolderEntries) {
					a_folder.error = T("menu.adaptive_balance_presets.folder_list_limited", "Folder listing limited. Enter a full path to reach an unlisted folder.");
					break;
				}
			}
			if (error)
				a_folder.error = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.folder_list_error", "Could not list folders: {}")), error.message());
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
			ImGui::TextUnformatted(T("menu.adaptive_balance_presets.export_folder", "Export folder"));
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##ExportFolderPath", T("menu.adaptive_balance_presets.full_folder_path", "Full folder path"), &a_folder.input);
			Util::AddTooltip(T("menu.adaptive_balance_presets.folder_path_help", "Enter or paste a full folder path, then use Go."));
			if (ImGui::Button(T("menu.adaptive_balance_presets.folder_go", "Go"), {})) {
				std::filesystem::path resolved;
				if (ResolveFolder(a_folder.input, resolved, a_folder.error))
					BrowseFolder(a_folder, resolved);
			}
			ImGui::SameLine();
			if (ImGui::Button(T("menu.adaptive_balance_presets.folder_parent", "Parent folder"), {}))
				BrowseFolder(a_folder, a_folder.browsing.parent_path());
			ImGui::SameLine();
			if (ImGui::Button(T("menu.adaptive_balance_presets.folder_root", "Drive root"), {}))
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
				SingleLine(T("menu.adaptive_balance_presets.no_subfolders", "No subfolders."), ImGui::GetContentRegionAvail().x);
			ImGui::EndChild();
			if (!navigate.empty())
				BrowseFolder(a_folder, navigate);
			SingleLine(a_folder.error.empty() ? T("menu.adaptive_balance_presets.folder_selection_help", "Select a subfolder, or use the folder path above.") : a_folder.error.c_str(), ImGui::GetContentRegionAvail().x);
			if (ImGui::Button(T("menu.adaptive_balance_presets.use_folder", "Use this folder"), {})) {
				if (a_folder.input.empty())
					a_folder.error = T("menu.adaptive_balance_presets.folder_empty_error", "Enter a folder path, or select Default folder.");
				else if (SetExportFolder(a_folder, a_folder.input, a_folder.error))
					ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(T("menu.adaptive_balance_presets.default_folder", "Default folder"), {})) {
				SetExportFolder(a_folder, {}, a_folder.error);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(T("menu.adaptive_balance_presets.cancel", "Cancel"), {}))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		void DrawBox(State& a_state, Collection a_collection, const char* a_detail, const Palette& a_palette, ImVec2 a_start, ImVec2 a_size)
		{
			ImGui::SetCursorScreenPos(a_start);
			const bool selected = a_state.collection == a_collection;
			auto colors = Util::StyledButtonWrapper(selected ? a_palette.active : a_palette.normal, a_palette.hovered, a_palette.active);
			if (ImGui::Button(std::format("##{}", CollectionId(a_collection)).c_str(), a_size))
				a_state.collection = a_collection;
			Util::AddTooltip(a_detail);
			auto* draw = ImGui::GetWindowDrawList();
			const ImVec2 end{ a_start.x + a_size.x, a_start.y + a_size.y };
			if (selected)
				draw->AddRect(a_start, end, ImGui::GetColorU32(a_palette.border), ImGui::GetStyle().FrameRounding, 0, 2);
			const float line = ImGui::GetTextLineHeight();
			const float inset = line * cardPadding;
			const float width = std::max(1.0f, a_size.x - 2 * inset);
			const ImVec2 titleStart{ a_start.x + inset, a_start.y + (a_size.y - line * 2) * .5f };
			RenderLine(draw, titleStart, width, CollectionName(a_collection));
			const ImVec2 detailStart{ titleStart.x, titleStart.y + line };
			RenderLine(draw, detailStart, width, a_detail);
		}
	}

	const char* CollectionName(Collection a_collection) { return CollectionNames().at(static_cast<std::size_t>(a_collection)); }
	const char* CollectionId(Collection a_collection) { return ids.at(static_cast<std::size_t>(a_collection)); }

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

	void DrawSelection(State& a_state, const Palette& a_export, const Palette& a_import, const Targets& a_targets,
		ExportFolder& a_folder, const std::filesystem::path& a_defaultFolder)
	{
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float line = ImGui::GetTextLineHeight();
		const float gap = std::max(ImGui::GetStyle().ItemSpacing.x, line * .6f);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(T("menu.adaptive_balance_presets.mode", "Mode:"));
		ImGui::SameLine();
		if (ImGui::RadioButton(std::format("{}###ExportMode", T("menu.adaptive_balance_presets.export", "Export")).c_str(), a_state.mode == Mode::Export))
			a_state.mode = Mode::Export;
		Util::AddTooltip(T("menu.adaptive_balance_presets.export_mode_help", "Select export mode, then choose the contents and use the Export button below."));
		ImGui::SameLine();
		if (ImGui::RadioButton(std::format("{}###ImportMode", T("menu.adaptive_balance_presets.import", "Import")).c_str(), a_state.mode == Mode::Import))
			a_state.mode = Mode::Import;
		Util::AddTooltip(T("menu.adaptive_balance_presets.import_mode_help", "Select import mode, then choose the contents and file and use the Import button below."));
		if (a_state.mode == Mode::Export) {
			ImGui::SameLine();
			if (ImGui::Button(std::format("{}##ExportFolder", T("menu.adaptive_balance_presets.choose_folder", "Choose folder")).c_str(), {})) {
				BeginFolderBrowse(a_folder, a_defaultFolder);
				ImGui::OpenPopup("##ExportFolderPicker");
			}
			const auto folderHint = a_folder.directory.empty() ? T("menu.adaptive_balance_presets.default_folder_help", "Default Presets folder; MO2 routes new files to Overwrite or the configured output mod.") : Util::PathToUtf8(a_folder.directory);
			Util::AddTooltip(folderHint.c_str());
			DrawFolderBrowser(a_folder, width, ImGui::GetCursorScreenPos());
		}

		const auto& palette = a_state.mode == Mode::Export ? a_export : a_import;
		const auto origin = ImGui::GetCursorScreenPos();
		const float verticalGap = line * .7f;
		// Reserve the impact, file action and feedback rows before sizing the diagram.
		const bool separateAction = std::ranges::any_of(CollectionNames(), [&](const char* title) { return GetTransferLayout(a_state.mode, title, width).separateAction; });
		const float reserved = line * 2 + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 5 +
		                       (separateAction ? ImGui::GetFrameHeightWithSpacing() : 0);
		const float height = std::clamp((ImGui::GetContentRegionAvail().y - reserved - verticalGap * 2) / 3, line * 2, line * cardHeight);
		const float half = (width - gap) * .5f;
		const float third = (width - 2 * gap) / 3;
		const float secondY = origin.y + height + verticalGap;
		const float thirdY = secondY + height + verticalGap;
		auto* draw = ImGui::GetWindowDrawList();
		const auto connector = ImGui::GetColorU32(palette.border);
		draw->AddLine({ origin.x + width * .5f, origin.y + height }, { origin.x + width * .5f, secondY - verticalGap * .5f }, connector);
		draw->AddLine({ origin.x + half * .5f, secondY - verticalGap * .5f }, { origin.x + width - half * .5f, secondY - verticalGap * .5f }, connector);
		for (float x : { origin.x + half * .5f, origin.x + width - half * .5f })
			draw->AddLine({ x, secondY - verticalGap * .5f }, { x, secondY }, connector);
		DrawBox(a_state, Collection::Full, T("menu.adaptive_balance_presets.full_detail", "Global + five profiles + every saved place"), palette, origin, { width, height });
		DrawBox(a_state, Collection::Base, T("menu.adaptive_balance_presets.base_detail", "Global + 5 profiles"), palette, { origin.x, secondY }, { half, height });
		DrawBox(a_state, Collection::Locations, T("menu.adaptive_balance_presets.locations_detail", "Every saved place"), palette, { origin.x + half + gap, secondY }, { half, height });
		const float branchY = thirdY - verticalGap * .5f;
		draw->AddLine({ origin.x + width - half * .5f, secondY + height }, { origin.x + width - half * .5f, branchY }, connector);
		draw->AddLine({ origin.x + third * .5f, branchY }, { origin.x + width - third * .5f, branchY }, connector);
		for (std::size_t index = 0; index < a_targets.size(); ++index) {
			const float x = origin.x + static_cast<float>(index) * (third + gap);
			draw->AddLine({ x + third * .5f, branchY }, { x + third * .5f, thirdY }, connector);
			DrawBox(a_state, static_cast<Collection>(static_cast<int>(Collection::Worldspace) + index), a_targets[index].c_str(), palette, { x, thirdY }, { third, height });
		}
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy({ width, height * 3 + verticalGap * 2 });
	}

	bool DrawTransfer(const State& a_state, const Palette& a_palette, std::string& a_name,
		std::span<const std::string> a_files, const std::string& a_path, const std::string& a_status,
		const char* a_unavailableReason, bool& a_refreshFiles)
	{
		const bool importing = a_state.mode == Mode::Import;
		ImGui::PushID(CollectionId(a_state.collection));
		ImGui::PushID(importing ? "import" : "export");
		const SKSE::stl::scope_exit restoreIds([] { ImGui::PopID(); ImGui::PopID(); });
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const char* impact = "";
		switch (a_state.collection) {
		case Collection::Full:
			impact = importing ? T("menu.adaptive_balance_presets.full_import_impact", "Replaces base + every saved location.") : T("menu.adaptive_balance_presets.full_export_impact", "Includes all Adaptive Balance appearance settings.");
			break;
		case Collection::Base:
			impact = importing ? T("menu.adaptive_balance_presets.base_import_impact", "Replaces base; keeps saved locations.") : T("menu.adaptive_balance_presets.base_export_impact", "Includes the global layer, five profiles, lighting, DoF and timing.");
			break;
		case Collection::Locations:
			impact = importing ? T("menu.adaptive_balance_presets.locations_import_impact", "Merges places; replaces matching keys.") : T("menu.adaptive_balance_presets.locations_export_impact", "Includes every saved worldspace, region, city, location and cell.");
			break;
		default:
			impact = importing ? T("menu.adaptive_balance_presets.scope_import_impact", "Replaces the current scope only.") : T("menu.adaptive_balance_presets.scope_export_impact", "Includes this current scope's profile and layer mode.");
			break;
		}
		SingleLine(impact, width);
		const auto layout = GetTransferLayout(a_state.mode, CollectionName(a_state.collection), width);
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const auto rowOrigin = ImGui::GetCursorScreenPos();
		ImGui::SetNextItemWidth(std::max(1.0f, width - layout.pickerWidth - (layout.separateAction ? 0 : layout.actionWidth + gap)));
		ImGui::InputTextWithHint("##PresetName", importing ? T("menu.adaptive_balance_presets.preset_name_or_choose_a_file", "Choose file or enter its preset name") : T("menu.adaptive_balance_presets.name_this_preset", "Name this preset"), &a_name);
		Util::AddTooltip(a_path.c_str());
		if (importing) {
			ImGui::SameLine();
			if (ImGui::Button(std::format("{}##PresetFiles", T("menu.adaptive_balance_presets.choose_file", "Choose file")).c_str(), {}))
				ImGui::OpenPopup("##PresetFilePicker");
			Util::AddTooltip(T("menu.adaptive_balance_presets.choose_a_matching_json_preset_from_the_presets_folder", "Choose a matching JSON preset from the Presets folder."));
			// Anchor above the file row so long filenames cannot push the picker outside the panel.
			ImGui::SetNextWindowPos(rowOrigin, ImGuiCond_Always, { 0, 1 });
			ImGui::SetNextWindowSizeConstraints({ width, 0 }, { width, ImGui::GetTextLineHeightWithSpacing() * 4 + ImGui::GetStyle().WindowPadding.y * 2 });
			if (ImGui::BeginPopup("##PresetFilePicker")) {
				a_refreshFiles |= ImGui::IsWindowAppearing();
				for (const auto& name : a_files) {
					if (ImGui::Selectable(name.c_str(), name == a_name, 0, { ImGui::GetContentRegionAvail().x, 0 }))
						a_name = name;
					Util::AddTooltip(name.c_str());
				}
				if (a_files.empty()) {
					const auto hint = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.no_matching_presets", "No matching presets found.\n{}\nChoose the contents matching the file, then reopen Choose file.")), a_path);
					SingleLine(hint.c_str(), ImGui::GetContentRegionAvail().x);
				}
				ImGui::EndPopup();
			}
		}
		if (!layout.separateAction)
			ImGui::SameLine();
		bool transfer = false;
		{
			auto colors = Util::StyledButtonWrapper(a_palette.normal, a_palette.hovered, a_palette.active);
			auto disabled = Util::DisableGuard(a_unavailableReason != nullptr);
			transfer = ImGui::Button(layout.label.c_str(), { layout.separateAction ? width : layout.actionWidth, 0 });
		}
		Util::AddTooltip(importing ? T("menu.adaptive_balance_presets.import_action_help", "Apply the selected preset file, then use Save Settings to keep the result.") : T("menu.adaptive_balance_presets.export_action_help", "Write the selected contents to a JSON preset file."));
		const auto instructions = fmt::format(fmt::runtime(T("menu.adaptive_balance_presets.transfer_instructions", "{}Choose contents above and {}, then click {}.")), importing ? "" : a_path + "\n", importing ? T("menu.adaptive_balance_presets.instruction_file", "a file") : T("menu.adaptive_balance_presets.instruction_name", "a name"), layout.label);
		SingleLine(a_unavailableReason ? a_unavailableReason : !a_status.empty() ? a_status.c_str() :
																				   instructions.c_str(),
			width);
		return transfer;
	}
}
