#include "Menu/AdaptiveBalancePresets.h"
#include "Utils/StringUtils.h"

#include <fmt/format.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#define T(key, fallback) fallback

namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F function;
		explicit scope_exit(F value) : function(std::move(value)) {}
		~scope_exit() { function(); }
	};
}

namespace Util
{
	std::map<std::string, ImRect> controls;
	void AddTooltip(const char* help)
	{
		controls[help] = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
	}
	struct DisableGuard
	{
		explicit DisableGuard(bool disabled) { ImGui::BeginDisabled(disabled); }
		~DisableGuard() { ImGui::EndDisabled(); }
	};
	struct StyledButtonWrapper
	{
		StyledButtonWrapper(ImVec4 normal, ImVec4 hovered, ImVec4 active)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, normal);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
		}
		~StyledButtonWrapper() { ImGui::PopStyleColor(3); }
	};
}

ImGuiID presetNameInputId = 0;
bool TrackedPresetNameInput(const char* label, const char* hint, std::string* name)
{
	const bool changed = ImGui::InputTextWithHint(label, hint, name);
	if (std::strcmp(label, "##PresetName") == 0) {
		presetNameInputId = ImGui::GetItemID();
		Util::controls["Preset name input"] = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
	}
	return changed;
}

bool TrackedPresetButton(const char* label, ImVec2 size)
{
	const bool clicked = ImGui::Button(label, size);
	Util::controls[label] = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
	return clicked;
}

#include "adaptive_preset_folder_ui_under_test.h"

namespace
{
	void Require(bool value, const char* reason)
	{
		if (!value)
			throw std::runtime_error(reason);
	}

	void Frame(const std::function<void()>& draw)
	{
		if (!ImGui::GetIO().Fonts->IsBuilt()) {
			unsigned char* pixels;
			int width, height;
			ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			ImGui::GetIO().Fonts->SetTexID(ImTextureID(1));
		}
		Util::controls.clear();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0, 0 });
		ImGui::SetNextWindowSize({ 1000, 800 });
		ImGui::Begin("Preset test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		draw();
		ImGui::End();
		ImGui::Render();
		Require(GImGui->ErrorCountCurrentFrame == 0, "Preset UI must balance ImGui state");
	}

	void Click(const std::string& label, const std::function<void()>& draw)
	{
		const auto point = Util::controls.at(label).GetCenter();
		auto& io = ImGui::GetIO();
		io.AddMousePosEvent(point.x, point.y);
		Frame(draw);
		io.AddMouseButtonEvent(0, true);
		Frame(draw);
		io.AddMouseButtonEvent(0, false);
		Frame(draw);
	}
}

int main()
{
	try {
		namespace Presets = MenuUI::AdaptiveBalancePresets;
		const auto root = std::filesystem::temp_directory_path() /
		                  std::format("csx-preset-folder-{}", std::chrono::steady_clock::now().time_since_epoch().count());
		Require(std::filesystem::create_directory(root), "Fixture must own a fresh directory");
		const SKSE::stl::scope_exit cleanup([&] {
			std::error_code error;
			std::filesystem::remove_all(root, error);
		});
		const auto custom = root / std::filesystem::path(u8"Shared_\u00e9_\u6f22##presets");
		std::filesystem::create_directory(custom);
		const auto regularFile = root / "not-a-folder.json";
		std::ofstream(regularFile) << "{}";

		ImGui::CreateContext();
		const SKSE::stl::scope_exit destroyContext([] { ImGui::DestroyContext(); });
		GImGui->ErrorCallback = [](ImGuiContext*, void*, const char* message) { throw std::runtime_error(message); };
		auto& io = ImGui::GetIO();
		io.DisplaySize = { 1100, 900 };
		io.DeltaTime = 1.0f / 60.0f;
		io.IniFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		Presets::State state;
		Presets::ExportFolder folder;
		const Presets::Palette exportColors{ { .42f, .34f, .12f, 1 }, { .5f, .4f, .16f, 1 }, { .6f, .48f, .2f, 1 }, { .8f, .7f, .4f, 1 } };
		const Presets::Palette importColors{ { .21f, .17f, .06f, 1 }, { .3f, .24f, .08f, 1 }, { .4f, .32f, .12f, 1 }, { .6f, .5f, .2f, 1 } };
		const Presets::Targets targets{ "Tamriel", "The Reach", "Markarth" };
		const std::array<std::string, 2> files{ std::string(96, 'W') + "_AdaptiveBrightness_City.json", "Clear_Nights_AdaptiveBrightness_City.json" };
		std::string name = "My appearance", status;
		const char* unavailable = nullptr;
		bool refresh = false;
		unsigned transfers = 0;
		float width = 850, height = 600, fontSize = 22;
		ImRect panel;
		const auto draw = [&] {
			ImGui::PushFont(ImGui::GetFont(), fontSize);
			ImGui::BeginChild("PresetPanel", { width, height });
			panel = ImGui::GetCurrentWindow()->Rect();
			const auto colors = GImGui->ColorStack.Size;
			const auto disabled = GImGui->DisabledStackSize;
			const auto ids = ImGui::GetCurrentWindow()->IDStack.Size;
			Presets::DrawSelection(state, exportColors, importColors, targets, folder, root / "Presets");
			transfers += Presets::DrawTransfer(state, state.mode == Presets::Mode::Export ? exportColors : importColors,
				name, files, "Presets/example.json", status, unavailable, refresh);
			Require(colors == GImGui->ColorStack.Size && disabled == GImGui->DisabledStackSize &&
						ids == ImGui::GetCurrentWindow()->IDStack.Size,
				"Preset UI must restore colors, disabled state and item IDs");
			ImGui::EndChild();
			ImGui::PopFont();
		};

		for (const ImVec2 size : { ImVec2{ 850, 600 }, { 700, 500 }, { 620, 440 } }) {
			width = size.x;
			height = size.y;
			for (const float font : { 18.0f, 22.0f, 27.0f }) {
				fontSize = font;
				for (const auto mode : { Presets::Mode::Export, Presets::Mode::Import }) {
					state.mode = mode;
					for (int index = 0; index < static_cast<int>(Presets::Collection::Count); ++index) {
						state.collection = static_cast<Presets::Collection>(index);
						Frame(draw);
						Frame(draw);
						const auto action = std::format("{} {}", mode == Presets::Mode::Export ? "Export" : "Import", Presets::CollectionName(state.collection));
						Require(panel.Contains(Util::controls.at(action)), "Named preset action must remain inside the panel");
					}
					if (mode == Presets::Mode::Export) {
						Click("Choose folder##ExportFolder", draw);
						Frame(draw);
						Require(!GImGui->OpenPopupStack.empty(), "Folder browser must open");
						const auto* popup = GImGui->OpenPopupStack.back().Window;
						Require(popup && panel.Contains(popup->Rect()) && popup->ScrollMax.x == 0 && popup->ScrollMax.y == 0,
							"Folder popup must fit the panel at each supported font size");
						Click("Cancel", draw);
						Require(folder.directory.empty() && transfers == 0 && !std::filesystem::exists(root / "Presets"),
							"Browse/cancel must not choose a destination or create files");
					} else {
						refresh = false;
						Click("Choose file##PresetFiles", draw);
						Frame(draw);
						Require(refresh && !GImGui->OpenPopupStack.empty(), "Opening file picker must refresh inventory");
						const auto* popup = GImGui->OpenPopupStack.back().Window;
						Require(popup && panel.Contains(popup->Rect()) && popup->ScrollMax.x == 0,
							"File picker must stay bounded even for long names");
						Click(files[0], draw);
						Require(name == files[0] && transfers == 0, "Choosing a file must not import it");
					}
				}
			}
		}

		width = 850;
		height = 600;
		fontSize = 22;
		state = {};
		Frame(draw);
		Frame(draw);
		Click("Choose folder##ExportFolder", draw);
		Click(Util::PathToUtf8(custom), draw);
		Require(folder.browsing == std::filesystem::canonical(custom) && folder.directory.empty(),
			"Unicode subfolder navigation must preserve the unselected destination");
		Click("Parent folder", draw);
		folder.input = Util::PathToUtf8(custom);
		Click("Go", draw);
		Require(folder.browsing == std::filesystem::canonical(custom) && folder.directory.empty(),
			"Pasted Unicode paths must navigate without exporting");
		Click("Use this folder", draw);
		Require(folder.directory == std::filesystem::canonical(custom) && transfers == 0,
			"Explicit folder selection must not perform a transfer");
		std::string error;
		for (const auto& invalid : { std::string("relative"), Util::PathToUtf8(root / "missing"), Util::PathToUtf8(regularFile),
				 std::string(Presets::ExportFolder::MaxPathBytes + 1, 'x'), std::string("bad\0path", 8) }) {
			Require(!Presets::SetExportFolder(folder, invalid, error) && !error.empty() && folder.directory == std::filesystem::canonical(custom),
				"Rejected paths must preserve the chosen destination");
		}
		Click("Choose folder##ExportFolder", draw);
		folder.input.clear();
		Click("Use this folder", draw);
		Require(folder.directory == std::filesystem::canonical(custom) && !folder.error.empty() && !GImGui->OpenPopupStack.empty(),
			"Empty input must not reset the destination or dismiss the browser");
		folder.input = Util::PathToUtf8(root);
		Click("Go", draw);
		Click("Cancel", draw);
		Require(folder.directory == std::filesystem::canonical(custom), "Cancellation must preserve an existing custom destination");
		Click("Choose folder##ExportFolder", draw);
		Click("Default folder", draw);
		Require(folder.directory.empty() && transfers == 0, "Default selection must restore routing without writing");

		Click("Select import mode, then choose the contents and file and use the Import button below.", draw);
		Require(state.mode == Presets::Mode::Import && transfers == 0, "Mode selection must not transfer");
		for (int index = 0; index < static_cast<int>(Presets::Collection::Count); ++index) {
			const auto collection = static_cast<Presets::Collection>(index);
			Click(std::format("##{}", Presets::CollectionId(collection)), draw);
			Require(state.collection == collection && transfers == 0, "Collection selection must not transfer");
		}
		unavailable = "Finish the pending location edit.";
		Frame(draw);
		Click("Import City", draw);
		Require(transfers == 0, "Unavailable actions must not import");
		unavailable = nullptr;
		Frame(draw);
		Click("Import City", draw);
		Require(transfers == 1, "Only the explicit import button requests a transfer");
		Click("Preset name input", draw);
		const auto cityInput = presetNameInputId;
		Require(GImGui->ActiveId == cityInput, "Filename editor must be active");
		state.collection = Presets::Collection::Base;
		Frame(draw);
		Frame(draw);
		Require(presetNameInputId != cityInput && GImGui->ActiveId != cityInput,
			"External navigation must not retain another collection's edit buffer");
		Click("Select export mode, then choose the contents and use the Export button below.", draw);
		Click("Export Base appearance", draw);
		Require(transfers == 2 && std::filesystem::is_empty(custom) && !std::filesystem::exists(root / "Presets"),
			"UI actions only return explicit transfer requests; rendering never writes files");

		for (unsigned index = 0; index < 520; ++index)
			std::filesystem::create_directory(root / std::format("folder-{:04}", index));
		Click("Choose folder##ExportFolder", draw);
		Require(folder.folders.size() <= 512 && !folder.error.empty(), "Folder enumeration must be bounded");
		Click("Cancel", draw);
		std::cout << "Production preset UI passed: real ImGui interactions, 108 layout variants, Unicode paths, cancellation, bounded enumeration and explicit transfers.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
