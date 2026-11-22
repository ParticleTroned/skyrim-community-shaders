#include "ProfileExchange.h"

#include "SettingsPage.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"
#include <imgui_stdlib.h>

namespace MenuUI
{
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
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##ProfileName", "Profile name", &state.name);
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
