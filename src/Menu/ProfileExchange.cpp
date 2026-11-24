#include "ProfileExchange.h"

#include "InputPadStyle.h"
#include "SettingsPage.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"
#include <imgui_stdlib.h>

namespace MenuUI
{
	namespace
	{
		void DrawProfileName(ProfileExchangeState& state)
		{
			ImGui::SetNextItemWidth(-1);
			const bool open = ImGui::IsPopupOpen("Profile name");
			ImGui::InputTextWithHint("##ProfileName", "Enter profile name", open ? &state.nameDraft : &state.name, open ? ImGuiInputTextFlags_ReadOnly : 0);
			const auto fieldMin = ImGui::GetItemRectMin();
			const auto fieldMax = ImGui::GetItemRectMax();
			Util::AddTooltip("Enter a profile name using the keyboard.");
			if (ImGui::IsItemActivated()) {
				state.nameDraft = state.name;
				ImGui::OpenPopup("Profile name");
			}
			if (!ImGui::IsPopupOpen("Profile name"))
				return;
			const auto* viewport = ImGui::GetWindowViewport();
			const auto& style = ImGui::GetStyle();
			const float margin = ImGui::GetFontSize() * .5f;
			const float above = std::max(1.0f, fieldMin.y - viewport->WorkPos.y - margin);
			const float below = std::max(1.0f, viewport->WorkPos.y + viewport->WorkSize.y - fieldMax.y - margin);
			const bool placeBelow = below >= above;
			const float font = style.FontSizeBase * InputPadStyle::FontScale;
			const float keyExtent = font * 2.8f;
			const float preferredWidth = keyExtent * 10 + style.ItemSpacing.x * InputPadStyle::Scale * 9 + style.WindowPadding.x;
			const float preferredHeight = keyExtent * 5.7f + font * 2 + style.FramePadding.y * 2 + style.WindowPadding.y + style.ItemSpacing.y * 4;
			const float fit = std::min({ 1.0f, (placeBelow ? below : above) / preferredHeight, std::max(1.0f, viewport->WorkSize.x - margin * 2) / preferredWidth });
			const float width = preferredWidth * fit, height = preferredHeight * fit;
			const float left = viewport->WorkPos.x + margin;
			const ImVec2 position{ std::clamp(fieldMin.x, left, std::max(left, viewport->WorkPos.x + viewport->WorkSize.x - width - margin)), placeBelow ? fieldMax.y + margin : fieldMin.y - margin - height };
			const InputPadStyle padStyle(fit);
			ImGui::SetNextWindowPos(position, ImGuiCond_Always);
			ImGui::SetNextWindowSize({ width, height }, ImGuiCond_Always);
			if (ImGui::BeginPopupModal("Profile name", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
				const SKSE::stl::scope_exit closePopup([] { ImGui::EndPopup(); });
				ImGui::SetNextItemWidth(-1);
				ImGui::InputTextWithHint("##NameDraft", "Profile name", &state.nameDraft);
				const float gap = ImGui::GetStyle().ItemSpacing.x;
				const float keyWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - gap * 9) / 10);
				for (const auto row : { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-_" }) {
					for (size_t index = 0; row[index]; ++index) {
						if (index)
							ImGui::SameLine();
						char key[]{ row[index], 0 };
						if (state.upperCase && key[0] >= 'a' && key[0] <= 'z')
							key[0] = static_cast<char>(key[0] - 'a' + 'A');
						if (ImGui::Button(key, { keyWidth, keyWidth }) && state.nameDraft.size() < 100)
							state.nameDraft += key;
					}
				}
				const float contentWidth = ImGui::GetContentRegionAvail().x;
				const ImVec2 editSize{ (contentWidth - gap * 2) / 3, keyWidth * .85f };
				const ImVec2 actionSize{ (contentWidth - gap) / 2, keyWidth * .85f };
				if (ImGui::Button(state.upperCase ? "Lowercase" : "Uppercase", editSize))
					state.upperCase = !state.upperCase;
				ImGui::SameLine();
				if (ImGui::Button("Space", editSize) && state.nameDraft.size() < 100)
					state.nameDraft += ' ';
				ImGui::SameLine();
				if (ImGui::Button("Backspace", editSize) && !state.nameDraft.empty()) {
					auto characterStart = state.nameDraft.size() - 1;
					while (characterStart && (static_cast<unsigned char>(state.nameDraft[characterStart]) & 0xc0) == 0x80)
						--characterStart;
					state.nameDraft.resize(characterStart);
				}
				if (ImGui::Button("Done", actionSize)) {
					state.name = state.nameDraft;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", actionSize))
					ImGui::CloseCurrentPopup();
			}
		}
	}

	void DrawProfileExchange(ProfileExchangeState& state, const char* name, const std::filesystem::path& root,
		bool acceptsPacks, const char* blocked,
		const std::function<void(const std::filesystem::path&, const std::string&)>& importProfile,
		const std::function<void(const std::filesystem::path&, const std::string&)>& exportProfile)
	{
		ImGui::PushID(name);
		const SKSE::stl::scope_exit restore([] { ImGui::PopID(); });
		const Choice modes[]{
			{ "import", "Import", "Load a shared profile.", "Choose a profile file or installed pack. Your current settings stay active until you press Import." },
			{ "export", "Export", "Share your complete profile.", "Choose a destination and name. Export creates a new profile without changing Save settings or overwriting an existing profile." }
		};
		ChoiceSetting(name, &state.mode, modes);
		if (state.mode == 0) {
			if (AdaptiveBalancePresets::DrawPathPicker(acceptsPacks ? "Choose profile file or folder" : "Choose profile file", state.importPicker, root, true, acceptsPacks)) {
				state.source = state.importPicker.selection;
				std::error_code error;
				state.name = Util::PathToUtf8(std::filesystem::is_directory(state.source, error) ? state.source.filename() : state.source.stem());
				if (acceptsPacks && (state.source.filename() == "preset.json" || state.source.stem() == state.source.parent_path().filename()))
					state.name = Util::PathToUtf8(state.source.parent_path().filename());
				if (!acceptsPacks && state.name.ends_with(".MyPerformance"))
					state.name.resize(state.name.size() - std::string_view(".MyPerformance").size());
			}
			if (!state.source.empty()) {
				ImGui::TextWrapped("%s", Util::PathToUtf8(state.source.filename()).c_str());
				Util::AddTooltip(Util::PathToUtf8(state.source).c_str());
			}
		} else {
			AdaptiveBalancePresets::DrawPathPicker("Choose export folder", state.exportPicker, root, false, true);
			const auto destination = state.exportPicker.directory.empty() ? root : state.exportPicker.directory;
			ImGui::TextWrapped("Folder: %s", Util::PathToUtf8(destination.filename()).c_str());
			Util::AddTooltip(Util::PathToUtf8(destination).c_str());
		}
		if (state.mode != 0 || acceptsPacks) {
			DrawProfileName(state);
		}
		const auto disabled = Util::DisableGuard(blocked || (state.mode == 0 ? state.source.empty() || (acceptsPacks && state.name.empty()) : state.name.empty()));
		if (ImGui::Button(state.mode == 0 ? "Import profile" : "Export profile")) {
			if (state.mode == 0)
				importProfile(state.source, state.name);
			else
				exportProfile(state.exportPicker.directory.empty() ? root : state.exportPicker.directory, state.name);
		}
		if (blocked)
			Util::AddTooltip(blocked);
	}
}
