#include "StabilizerIntegration.h"
#include "StabilizerSettings.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <string_view>

namespace VRFpsStabilizer
{
	namespace
	{
		constexpr double kDistanceStep = 500.0;

		bool IsGameUnitDistance(std::string_view key)
		{
			constexpr std::array keys{
				"CPULateStartDistanceToSwitchBackUp", "RainLODOffset",
				"fBlockLevel0Distance:TerrainManager", "fBlockLevel1Distance:TerrainManager",
				"fTreeLoadDistance:TerrainManager"
			};
			return std::ranges::any_of(keys, [&](const char* distanceKey) { return Equal(key, distanceKey); });
		}

		struct Editor
		{
			IniDocument document;
			bool initialized = false;
			bool readable = false;
			uint64_t revision = 0;
			std::string error;
			ImGuiTextFilter filter;
			int level = 0;
			int location = 0;
			int event = 0;
		};

		Editor& GetEditor(ConfigFile file)
		{
			static Editor mainEditor, locationEditor;
			return file == ConfigFile::Locations ? locationEditor : mainEditor;
		}

		void Tooltip(const char* text, bool distance = false, bool fadeMultiplier = false)
		{
			if (auto tooltip = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(text);
				if (distance)
					ImGui::TextUnformatted("Use +/- for 500 game-unit steps, or type a whole-number distance.");
				else if (fadeMultiplier)
					ImGui::TextUnformatted("Use +/- for steps of 1, or type a value for smaller adjustments.");
			}
		}

		void Reload(Editor& editor, ConfigFile file)
		{
			editor.document = {};
			editor.readable = Load(file, editor.document, editor.error);
			editor.initialized = true;
			editor.revision = Status().revision;
		}

		void DrawActions(Editor& editor, ConfigFile file)
		{
			const auto runtime = Status();
			const bool dirty = editor.document.text != editor.document.original;
			std::string validation;
			const bool valid = file != ConfigFile::Main || ValidateSettings(editor.document, validation);
			{
				auto disabled = Util::DisableGuard(!editor.readable || !dirty || !valid || runtime.pending);
				if (ImGui::Button(runtime.available ? "Save & Apply" : "Save INI")) {
					if (Save(file, editor.document, editor.error))
						editor.revision = Status().revision;
				}
			}
			Tooltip("Save this INI's edits. With Stabilizer's live interface, reload them in the running game. Quality, location and event rules still follow their configured conditions.");
			ImGui::SameLine();
			if (ImGui::Button(dirty ? "Discard changes..." : "Read INI")) {
				if (dirty)
					ImGui::OpenPopup("Discard Stabilizer edits?");
				else
					Reload(editor, file);
			}
			Tooltip("Read the installed INI into the editor. This does not execute Stabilizer's reload interface.");
			if (ImGui::BeginPopup("Discard Stabilizer edits?")) {
				ImGui::TextUnformatted("Discard unsaved changes in this INI?");
				if (ImGui::Button("Discard & read INI")) {
					Reload(editor, file);
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Keep editing"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			{
				auto disabled = Util::DisableGuard(dirty || runtime.pending || !runtime.available || !editor.readable);
				if (ImGui::Button("Apply saved INI"))
					RequestReload(file, editor.error);
			}
			Tooltip("Ask Stabilizer to reload the saved file, including edits made outside CSX. Unsaved editor changes must be saved or discarded first.");
			if (dirty)
				Util::Text::WrappedWarning("Unsaved changes in %s.", file == ConfigFile::Main ? "VRFpsStabilizer.ini" : "VRFpsStabilizerLocation.ini");
			if (!validation.empty())
				Util::Text::WrappedError("%s", validation.c_str());
			if (!editor.error.empty())
				Util::Text::WrappedError("%s", editor.error.c_str());
		}

		void DrawSetting(Editor& editor, const Setting& setting)
		{
			if (!editor.filter.PassFilter(setting.label) && !editor.filter.PassFilter(setting.key))
				return;
			ImGui::PushID(setting.key);
			const std::string_view key = setting.key;
			const bool distance = IsGameUnitDistance(key);
			const bool fadeMultiplier = key.find("LODFadeOutMult") != key.npos || key.find("LODFadeOutModifier") != key.npos;
			auto value = editor.document.Get("Settings", setting.key);
			if (!value) {
				ImGui::TextDisabled("%s (not configured)", setting.label);
				Tooltip(setting.help, distance, fadeMultiplier);
				ImGui::SameLine();
				if (ImGui::SmallButton("Configure"))
					editor.document.Set("Settings", setting.key, setting.suggested, editor.error);
				Tooltip("Add this option using the supplied 1.4.13 beta template value. Saving is still required; omitted settings otherwise keep Stabilizer's own defaults.");
				ImGui::PopID();
				return;
			}
			double number = 0;
			const bool numeric = ParseNumber(*value, number);
			bool changed = false;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
			if (setting.kind == SettingKind::Boolean && numeric && (number == 0 || number == 1)) {
				bool enabled = number == 1;
				changed = ImGui::Checkbox(setting.label, &enabled);
				if (changed)
					*value = enabled ? "1" : "0";
			} else if (setting.kind != SettingKind::WorldList && numeric) {
				const bool integral = setting.kind == SettingKind::Integer || distance;
				const double step = distance ? kDistanceStep : (integral || fadeMultiplier ? 1.0 : 0.1);
				changed = ImGui::InputDouble(setting.label, &number, step, 0, integral ? "%.0f" : "%.4g");
				if (changed)
					*value = integral ? std::format("{:.0f}", std::round(number)) : std::format("{:.8g}", number);
			} else {
				changed = ImGui::InputText(setting.label, &*value);
			}
			Tooltip(setting.help, distance, fadeMultiplier);
			if (changed) {
				editor.error.clear();
				editor.document.Set("Settings", setting.key, *value, editor.error);
			}
			ImGui::PopID();
		}

		void DrawCommands(Editor& editor, const char* section, const char* help)
		{
			ImGui::PushID(section);
			ImGui::TextWrapped("%s", help);
			auto body = editor.document.Body(section);
			if (ImGui::InputTextMultiline("##Commands", &body, ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 12))) {
				editor.error.clear();
				editor.document.SetBody(section, body, editor.error);
			}
			Tooltip("One entry per line. # starts a comment. Keep section headers out of this field. Changes are saved only by Save & Apply; existing comments remain until you edit them.");
			ImGui::PopID();
		}

		void DrawQualityRows(Editor& editor, const char* section)
		{
			ImGui::PushID(section);
			for (auto [key, value] : editor.document.Entries(section)) {
				if (key.find('>') != key.npos)
					continue;
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
				const char* label = key.c_str();
				const char* help = "Skyrim INI setting in this quality tier. Format: setting:category. Use Advanced entries to add, remove, or script settings.";
				if (Equal(key, "iMaxDesired:Particles")) {
					label = "Maximum particles";
					help = "Target maximum particle count for this tier. Lower values can improve performance in scenes with many particle effects.";
				} else if (Equal(key, "fBlockLevel0Distance:TerrainManager")) {
					label = "Near terrain LOD distance (game units)";
					help = "Distance for the nearest terrain LOD tier. Higher values retain more landscape detail at a greater rendering cost.";
				} else if (Equal(key, "fBlockLevel1Distance:TerrainManager")) {
					label = "Far terrain LOD distance (game units)";
					help = "Distance for the next terrain LOD tier. Keep it at least as large as the near terrain distance.";
				} else if (Equal(key, "fTreeLoadDistance:TerrainManager")) {
					label = "Tree LOD distance (game units)";
					help = "Distance at which distant tree LOD is visible. Lower values reduce distant tree coverage and rendering cost.";
				}
				ImGui::PushID(key.c_str());
				const bool distance = IsGameUnitDistance(key);
				double number = 0;
				bool changed = false;
				if (ParseNumber(value, number)) {
					const bool integral = distance || key.starts_with('i') || key.starts_with('b');
					const double step = distance ? kDistanceStep : (integral ? 1.0 : 100.0);
					changed = ImGui::InputDouble(label, &number, step, 0, integral ? "%.0f" : "%.6g");
					if (changed && std::isfinite(number))
						value = integral ? std::format("{:.0f}", std::round(number)) : std::format("{:.8g}", number);
				} else {
					changed = ImGui::InputText(label, &value);
				}
				Tooltip(help, distance);
				if (changed) {
					editor.error.clear();
					editor.document.Set(section, key, value, editor.error);
				}
				ImGui::PopID();
			}
			if (ImGui::TreeNode("Advanced entries")) {
				DrawCommands(editor, section,
					"Edit all entries in this tier, including custom settings and commands. Examples: iMaxDesired:Particles = 1500, CS>SSGI = 0, CONSOLE>command. TOGGLE> commands run on entering the level and toggle back at lower levels.");
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		void DrawLocations(Editor& editor)
		{
			bool enabled = editor.document.Get("Settings", "Enabled").value_or("0") == "1";
			if (ImGui::Checkbox("Enable location rules", &enabled))
				editor.document.Set("Settings", "Enabled", enabled ? "1" : "0", editor.error);
			Tooltip("Use VRFpsStabilizerLocation.ini to choose a quality tier by location name. Good is used for unlisted locations in the supplied configuration.");
			ImGui::Combo("Location tier", &editor.location, kLocationSections.data(), static_cast<int>(kLocationSections.size()));
			Tooltip("Select a tier to edit its location list and settings. Interior contains commands for interior cells, without a location-name list.");
			const auto* tier = kLocationSections[editor.location];
			if (editor.location < 5) {
				auto namesText = editor.document.Get("Settings", tier).value_or("");
				ImGui::TextUnformatted("Location names (comma-separated)");
				if (ImGui::InputText("##Locations", &namesText))
					editor.document.Set("Settings", tier, namesText, editor.error);
				Tooltip("Use the location names expected by Stabilizer, separated by commas, for example Whiterun,Riverwood. Changing this list moves the rule's scope; it does not move the player.");
			}
			DrawQualityRows(editor, tier);
		}

		const char* SettingSection(const Setting& setting)
		{
			const std::string_view key = setting.key;
			if (Equal(setting.group, "Performance"))
				return key.find("DynamicResolution") != key.npos ? "Native dynamic resolution (experimental)" : "Frame time and automatic quality";
			if (key.starts_with("Adjust") || key.starts_with("Minf") || key.starts_with("Maxf"))
				return "Actor, object and item fade distances";
			if (key.starts_with("Riften") || key.starts_with("OtherCities"))
				return "City distance modifiers";
			if (key.starts_with("CPULateStart") || key.starts_with("LODFadeOutValue"))
				return "CPU late-start response";
			return "Grass and distant LOD";
		}
	}

	void DrawStatus()
	{
		const auto state = Status();
		if (!state.loaded) {
			Util::Text::WrappedWarning("%s", kNotLoadedMessage);
			return;
		}
		if (state.available)
			ImGui::TextDisabled("Live reload available (Stabilizer build %u)", state.build);
		else
			Util::Text::WrappedWarning("Live reload unavailable. Install a Stabilizer version with the new interface; INI edits otherwise require a restart.");
		if (!state.message.empty()) {
			if (state.pending || state.restartRequired)
				Util::Text::WrappedWarning("%s", state.message.c_str());
			else
				ImGui::TextWrapped("%s", state.message.c_str());
		}
	}

	void DrawSettings(const char* group)
	{
		const bool locations = Equal(group, "Locations");
		const auto file = locations ? ConfigFile::Locations : ConfigFile::Main;
		auto& editor = GetEditor(file);
		const auto revision = Status().revision;
		if (!editor.initialized || (editor.revision != revision && editor.document.text == editor.document.original))
			Reload(editor, file);
		ImGui::PushID(locations ? "StabilizerLocations" : "StabilizerMain");
		DrawActions(editor, file);
		if (editor.readable) {
			ImGui::Separator();
			if (locations) {
				DrawLocations(editor);
			} else if (Equal(group, "Quality Levels")) {
				ImGui::TextWrapped("Level 0 is highest quality; level 9 is lowest. Stabilizer chooses a level using your frame-time targets.");
				ImGui::SliderInt("Quality level", &editor.level, 0, 9);
				Tooltip("Choose the level to configure. This edits that level's rules without forcing the current game to that level.");
				DrawQualityRows(editor, std::format("Level{}", editor.level).c_str());
			} else if (Equal(group, "Commands")) {
				constexpr std::array help{
					"Console commands run once when game data and the main menu load. Reloading the INI does not replay a startup event.",
					"Console commands run after a saved game is loaded.",
					"Console commands run after loading, when the player is in a cell.",
					"Console commands run when a new game starts.",
					"Conditional rules: Interior|command or Exterior,Raining|command. Optional hours: Exterior|4|16|command. Supports INI>, CS> and IMOD> commands. This section also contains the profiles edited on the Profiles page; save here before switching there."
				};
				ImGui::Combo("Run commands on", &editor.event, kEventSections.data(), static_cast<int>(kEventSections.size()));
				Tooltip("Select the event or condition controlling these commands. Saving reloads the rules; each command still waits for its event or condition.");
				DrawCommands(editor, kEventSections[editor.event], help[editor.event]);
			} else {
				editor.filter.Draw("Find a setting");
				Tooltip("Filter this page by setting name or INI key. Clear the field to show all controls again.");
				std::string_view lastSection;
				bool open = true;
				for (const auto& setting : kSettings) {
					if (!Equal(setting.group, group))
						continue;
					const auto* section = SettingSection(setting);
					if (lastSection != section) {
						open = ImGui::CollapsingHeader(section, ImGuiTreeNodeFlags_DefaultOpen);
						lastSection = section;
					}
					if (open || editor.filter.IsActive())
						DrawSetting(editor, setting);
				}
			}
		}
		ImGui::PopID();
	}

	bool HasUnsavedSettings(ConfigFile file)
	{
		const auto& editor = GetEditor(file);
		return editor.document.text != editor.document.original;
	}
}
