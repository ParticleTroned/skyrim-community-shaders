#include "DisplaySettingsMenu.h"

#include "Globals.h"
#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"

#include <imgui.h>

DisplaySettingsMenu* DisplaySettingsMenu::GetSingleton()
{
	static DisplaySettingsMenu singleton;
	return &singleton;
}

void DisplaySettingsMenu::Register()
{
	if (auto* ui = RE::UI::GetSingleton()) {
		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
		logger::info("[DisplaySettings] registered System-menu entry hook");
	}
}

bool DisplaySettingsMenu::SetupSystemMenuEntry() const
{
	// Adapted from powerof3/PhotoMode (MIT): grab the System tab's GFx view, force the Mod Manager entry
	// visible, then rename it to our title. Selecting it fires ModManagerMenu, which we intercept below.
	const auto menu = RE::UI::GetSingleton()->GetMenu<RE::JournalMenu>(RE::JournalMenu::MENU_NAME);
	if (!menu) {
		logger::info("[DisplaySettings] SetupSystemMenuEntry: no JournalMenu");
		return false;
	}
	const auto& view = menu->GetRuntimeData().systemTab.view;  // GPtr<GFxMovieView>

	RE::GFxValue page;
	if (!view || !view->GetVariable(&page, "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc")) {
		logger::info("[DisplaySettings] SetupSystemMenuEntry: GFx path _root.QuestJournalFader.Menu_mc.SystemFader.Page_mc not found (view={})", static_cast<bool>(view));
		return false;
	}

	RE::GFxValue showModMenu;
	if (page.GetMember("_showModMenu", &showModMenu) && !showModMenu.GetBool()) {
		std::array<RE::GFxValue, 1> args;
		args[0] = true;
		page.Invoke("SetShowMod", nullptr, args.data(), args.size());
		logger::info("[DisplaySettings] forced SetShowMod(true)");
	}

	RE::GFxValue categoryList;
	if (!page.GetMember("CategoryList", &categoryList)) {
		logger::info("[DisplaySettings] SetupSystemMenuEntry: no CategoryList member");
		return false;
	}

	RE::GFxValue entryList;
	if (!categoryList.GetMember("entryList", &entryList)) {
		logger::info("[DisplaySettings] SetupSystemMenuEntry: no entryList member");
		return false;
	}

	// Hijack the entry whose selection opens "Mod Manager Menu" (which we intercept in ProcessEvent).
	// PhotoMode targets "$MOD MANAGER"; on AE-style menus that slot is "$CREATIONS" ("$INSTALLED CONTENT" is a
	// different menu, so it is NOT a fallback). Renaming the entry's text leaves its action intact. Priority
	// order matters — pick the first candidate that EXISTS, not the lowest index.
	static constexpr std::array candidates{ "$MOD MANAGER"sv, "$CREATIONS"sv };
	const std::uint32_t count = entryList.GetArraySize();
	std::vector<std::string> texts(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		RE::GFxValue elem;
		if (!entryList.GetElement(i, &elem))
			continue;
		RE::GFxValue text;
		if (elem.GetMember("text", &text) && text.IsString())
			texts[i] = text.GetString();
	}

	std::optional<std::uint32_t> hijackIndex;
	std::string_view             hijackText;
	for (const auto cand : candidates) {
		for (std::uint32_t i = 0; i < count; ++i) {
			if (texts[i] == cand) {
				hijackIndex = i;
				hijackText = cand;
				break;
			}
		}
		if (hijackIndex)
			break;
	}

	if (hijackIndex) {
		RE::GFxValue entry;
		view->CreateObject(&entry);
		entry.SetMember("text", "Display Settings");
		entryList.SetElement(*hijackIndex, entry);
		categoryList.Invoke("InvalidateData");
		logger::info("[DisplaySettings] hijacked '{}' (index {}) -> 'Display Settings'", hijackText, *hijackIndex);
		return true;
	}

	logger::info("[DisplaySettings] SetupSystemMenuEntry: no hijackable entry found");
	return false;
}

void DisplaySettingsMenu::Activate()
{
	isOpen = true;
}

RE::BSEventNotifyControl DisplaySettingsMenu::ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	auto* ui = RE::UI::GetSingleton();

	if (a_event->opening && a_event->menuName == RE::JournalMenu::MENU_NAME) {
		// System (pause) menu opened — inject our entry into the Mod Manager slot.
		wantSystemMenuEntry = !SetupSystemMenuEntry();
	} else if (a_event->opening && a_event->menuName == RE::ModManagerMenu::MENU_NAME) {
		// Our entry was selected (it occupies the Mod Manager slot). Hide both menus and open our window.
		if (ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::ModManagerMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				queue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
			Activate();
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

void DisplaySettingsMenu::Draw()
{
	if (!isOpen)
		return;

	ImGui::GetIO().MouseDrawCursor = true;  // the System menu was hidden; draw our own cursor

	// PhotoMode-style theme (palette ported from powerof3/PhotoMode, MIT), pushed scoped so the rest of CS
	// keeps its own theme. Translucent-black panels, tan Skyrim border, flat (no rounding), subtle white grabs.
	constexpr float a = 0.68f;
	const ImVec4 bg{ 0.0f, 0.0f, 0.0f, a };
	const ImVec4 tan{ 0.569f, 0.545f, 0.506f, a };
	const ImVec4 frame{ 0.2f, 0.2f, 0.2f, a };
	const ImVec4 hilite{ 1.0f, 1.0f, 1.0f, 0.1f };
	int nCol = 0, nVar = 0;
	auto col = [&](ImGuiCol c, const ImVec4& v) { ImGui::PushStyleColor(c, v); ++nCol; };
	auto var = [&](ImGuiStyleVar s, float v) { ImGui::PushStyleVar(s, v); ++nVar; };
	col(ImGuiCol_WindowBg, bg);
	col(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
	col(ImGuiCol_PopupBg, bg);
	col(ImGuiCol_Border, tan);
	col(ImGuiCol_Separator, tan);
	col(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
	col(ImGuiCol_TextDisabled, ImVec4(1, 1, 1, 0.30f));
	col(ImGuiCol_FrameBg, frame);
	col(ImGuiCol_FrameBgHovered, frame);
	col(ImGuiCol_FrameBgActive, frame);
	col(ImGuiCol_SliderGrab, ImVec4(1, 1, 1, 0.245f));
	col(ImGuiCol_SliderGrabActive, ImVec4(1, 1, 1, 0.531f));
	col(ImGuiCol_CheckMark, ImVec4(1, 1, 1, 0.8f));
	col(ImGuiCol_Button, bg);
	col(ImGuiCol_ButtonHovered, hilite);
	col(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
	col(ImGuiCol_Header, hilite);
	col(ImGuiCol_HeaderHovered, hilite);
	col(ImGuiCol_HeaderActive, hilite);
	col(ImGuiCol_Tab, ImVec4(0, 0, 0, 0));
	col(ImGuiCol_TabHovered, ImVec4(0.2f, 0.2f, 0.2f, 1));
	col(ImGuiCol_TabActive, ImVec4(0.2f, 0.2f, 0.2f, 1));
	col(ImGuiCol_TabUnfocused, ImVec4(0, 0, 0, 0));
	col(ImGuiCol_TabUnfocusedActive, ImVec4(0.2f, 0.2f, 0.2f, 1));
	var(ImGuiStyleVar_WindowBorderSize, 3.5f);
	var(ImGuiStyleVar_FrameBorderSize, 1.0f);  // PhotoMode draws a thin border around sliders/frames
	var(ImGuiStyleVar_WindowRounding, 0.0f);
	var(ImGuiStyleVar_ChildRounding, 0.0f);
	var(ImGuiStyleVar_FrameRounding, 0.0f);
	var(ImGuiStyleVar_GrabRounding, 0.0f);
	var(ImGuiStyleVar_GrabMinSize, 20.0f);
	var(ImGuiStyleVar_TabRounding, 0.0f);

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 center{ viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f };
	// Centered, covering most of the screen, non-movable, no title bar (PhotoMode look).
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.7f, viewport->Size.y * 0.8f), ImGuiCond_Always);

	constexpr auto flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
	                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
	const bool pushedFont = s_displayFont != nullptr;
	if (ImGui::Begin("##CSDisplaySettings", nullptr, flags)) {
		if (pushedFont)
			ImGui::PushFont(s_displayFont);

		ImGui::SetWindowFontScale(1.3f);
		ImGui::TextUnformatted("DISPLAY SETTINGS");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::Separator();

		const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
		if (ImGui::BeginChild("##content", ImVec2(0, -footerHeight))) {
			if (ImGui::BeginTabBar("##DisplaySettingsTabs")) {
				if (ImGui::BeginTabItem("Upscaling")) {
					globals::features::upscaling.DrawSettings();
					ImGui::EndTabItem();
				}
				if (globals::features::hdrDisplay.loaded && ImGui::BeginTabItem("HDR")) {
					globals::features::hdrDisplay.DrawSettings();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();

		if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			isOpen = false;

		if (pushedFont)
			ImGui::PopFont();
	}
	ImGui::End();

	ImGui::PopStyleVar(nVar);
	ImGui::PopStyleColor(nCol);
}
