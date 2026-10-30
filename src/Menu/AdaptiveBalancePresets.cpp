#include "AdaptiveBalancePresets.h"

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
		constexpr std::array names{ "Complete setup", "Base appearance", "All locations", "Worldspace", "Location", "City" };
		constexpr std::array ids{ "full", "base", "locations", "worldspace", "region", "city" };
		static_assert(names.size() == static_cast<std::size_t>(Collection::Count) && ids.size() == names.size());
		constexpr float cardPadding = .4f;
		constexpr float cardHeight = 2.8f;
		constexpr float minimumNameWidth = 3.0f;

		struct TransferLayout
		{
			std::string label;
			float actionWidth, pickerWidth;
			bool separateAction;
		};

		TransferLayout GetTransferLayout(Mode a_mode, const char* a_collection, float a_width)
		{
			const auto label = std::format("{} {}", a_mode == Mode::Import ? "Import" : "Export", a_collection);
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float actionWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
			const float pickerWidth = a_mode == Mode::Import ? ImGui::GetFrameHeight() + gap : 0;
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

		bool ModeButton(const char* a_label, bool a_selected, const Palette& a_palette, float a_width)
		{
			auto colors = Util::StyledButtonWrapper(a_selected ? a_palette.active : a_palette.normal, a_palette.hovered, a_palette.active);
			const bool clicked = ImGui::Button(a_label, { a_width, 0 });
			if (a_selected)
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(a_palette.border), ImGui::GetStyle().FrameRounding);
			return clicked;
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

	const char* CollectionName(Collection a_collection) { return names.at(static_cast<std::size_t>(a_collection)); }
	const char* CollectionId(Collection a_collection) { return ids.at(static_cast<std::size_t>(a_collection)); }

	void DrawSelection(State& a_state, const Palette& a_export, const Palette& a_import, const Targets& a_targets)
	{
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float modeWidth = (width - gap) * .5f;
		if (ModeButton(a_state.mode == Mode::Export ? "Export (active)###ExportMode" : "Export###ExportMode", a_state.mode == Mode::Export, a_export, modeWidth))
			a_state.mode = Mode::Export;
		Util::AddTooltip("Export mode: create a shareable JSON file.");
		ImGui::SameLine();
		if (ModeButton(a_state.mode == Mode::Import ? "Import (active)###ImportMode" : "Import###ImportMode", a_state.mode == Mode::Import, a_import, modeWidth))
			a_state.mode = Mode::Import;
		Util::AddTooltip("Import mode: apply a JSON file to your setup.");

		const auto& palette = a_state.mode == Mode::Export ? a_export : a_import;
		const auto origin = ImGui::GetCursorScreenPos();
		const float line = ImGui::GetTextLineHeight();
		const float verticalGap = line * .5f;
		// Reserve the impact, file action and feedback rows before sizing the diagram.
		const bool separateAction = std::ranges::any_of(names, [&](const char* title) { return GetTransferLayout(a_state.mode, title, width).separateAction; });
		const float reserved = line * 2 + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 5 +
		                       (separateAction ? ImGui::GetFrameHeightWithSpacing() : 0);
		const float height = std::clamp((ImGui::GetContentRegionAvail().y - reserved - verticalGap * 2) / 3, line * 2.1f, line * cardHeight);
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
		DrawBox(a_state, Collection::Full, "Global + five profiles + every saved place", palette, origin, { width, height });
		DrawBox(a_state, Collection::Base, "Global + 5 profiles", palette, { origin.x, secondY }, { half, height });
		DrawBox(a_state, Collection::Locations, "Every saved place", palette, { origin.x + half + gap, secondY }, { half, height });
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
			impact = importing ? "Replaces base + every saved location." : "Includes all Adaptive Balance appearance settings.";
			break;
		case Collection::Base:
			impact = importing ? "Replaces base; keeps saved locations." : "Includes the global layer, five profiles, lighting, DoF and timing.";
			break;
		case Collection::Locations:
			impact = importing ? "Merges places; replaces matching keys." : "Includes every saved worldspace, region, city, location and cell.";
			break;
		default:
			impact = importing ? "Replaces the current scope only." : "Includes this current scope's profile and layer mode.";
			break;
		}
		SingleLine(impact, width);
		const auto layout = GetTransferLayout(a_state.mode, CollectionName(a_state.collection), width);
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const auto rowOrigin = ImGui::GetCursorScreenPos();
		ImGui::SetNextItemWidth(std::max(1.0f, width - layout.pickerWidth - (layout.separateAction ? 0 : layout.actionWidth + gap)));
		ImGui::InputTextWithHint("##PresetName", importing ? "Preset name or choose a file" : "Name this preset", &a_name);
		Util::AddTooltip(a_path.c_str());
		if (importing) {
			ImGui::SameLine();
			if (ImGui::ArrowButton("##PresetFiles", ImGuiDir_Down))
				ImGui::OpenPopup("##PresetFilePicker");
			Util::AddTooltip("Choose a matching JSON preset from the Presets folder.");
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
				if (a_files.empty())
					SingleLine("No presets found. Enter a name to retry.", ImGui::GetContentRegionAvail().x);
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
		SingleLine(a_unavailableReason ? a_unavailableReason : !a_status.empty() ? a_status.c_str() :
														   importing             ? "After importing, use Save Settings to keep the result." :
																				   "Choose a box above, name the file, then export.",
			width);
		return transfer;
	}
}
