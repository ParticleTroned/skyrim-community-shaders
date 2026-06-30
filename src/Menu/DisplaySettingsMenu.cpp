#include "DisplaySettingsMenu.h"

#include "Globals.h"
#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"

#include <imgui.h>

#include <algorithm>

namespace
{
	// SkyUI/vanilla pause-menu SystemPage Scaleform: the System tab's page movieclip and the state/index values
	// for "the Display options view is showing" (System → SETTINGS → DISPLAY). See SkyUI SystemPage.as.
	constexpr const char* kPagePath = "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc";
	constexpr int         kOptionsListsState = 4;     // SystemPage.OPTIONS_LISTS_STATE
	constexpr int         kDisplayCategoryIndex = 1;  // SettingsList entry order: 0 Gameplay, 1 Display, 2 Audio
}

DisplaySettingsMenu* DisplaySettingsMenu::GetSingleton()
{
	static DisplaySettingsMenu singleton;
	return &singleton;
}

void DisplaySettingsMenu::Register()
{
	if (auto* ui = RE::UI::GetSingleton()) {
		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
		logger::info("[DisplaySettings] registered pause-menu Settings->Display hook");
	}
}

RE::GPtr<RE::GFxMovieView> DisplaySettingsMenu::GetSystemTabView() const
{
	const auto menu = RE::UI::GetSingleton()->GetMenu<RE::JournalMenu>(RE::JournalMenu::MENU_NAME);
	if (!menu)
		return nullptr;
	return menu->GetRuntimeData().systemTab.view;  // GPtr<GFxMovieView>
}

void DisplaySettingsMenu::Update()
{
	// Poll the pause-menu Scaleform while it is open: open our window the moment the player enters the Display
	// options view (System → SETTINGS → DISPLAY). Rising-edge so it only opens once per selection.
	if (!journalOpen)
		return;

	// Our window was just closed: send the page back to the category list (deferred here so the GFx Invoke runs
	// on the main thread, not the present thread where Draw() closes the window).
	if (navBackPending.load(std::memory_order_relaxed)) {
		NavigateSystemPageBack();
		navBackPending.store(false, std::memory_order_relaxed);
		return;
	}

	if (isOpen.load(std::memory_order_relaxed))
		return;

	const auto view = GetSystemTabView();
	if (!view)
		return;

	RE::GFxValue state, selIndex;
	const std::string statePath = std::string(kPagePath) + ".iCurrentState";
	const std::string indexPath = std::string(kPagePath) + ".SettingsList.selectedIndex";
	if (!view->GetVariable(&state, statePath.c_str()) || !view->GetVariable(&selIndex, indexPath.c_str()))
		return;

	const int curState = state.IsNumber() ? static_cast<int>(state.GetNumber()) : -1;
	const int curIndex = selIndex.IsNumber() ? static_cast<int>(selIndex.GetNumber()) : -1;
	const bool displayNow = (curState == kOptionsListsState && curIndex == kDisplayCategoryIndex);

	if (displayNow && !wasDisplayOptions)
		Activate();
	wasDisplayOptions = displayNow;
}

void DisplaySettingsMenu::NavigateSystemPageBack()
{
	// Simulate pressing the menu's Back/Cancel: SystemPage.onCancelPress() runs the page's own transition (from
	// the Display options view back to the Settings-category list, with the proper bookkeeping + cancel sound).
	// Doing the EndState/StartState by hand soft-locked the menu, so let the page drive it.
	auto* ui = RE::UI::GetSingleton();
	if (!ui || !ui->IsMenuOpen(RE::JournalMenu::MENU_NAME))
		return;
	const auto view = GetSystemTabView();
	if (!view)
		return;

	const std::string cancelPath = std::string(kPagePath) + ".onCancelPress";
	view->Invoke(cancelPath.c_str(), nullptr, nullptr, 0);
	// Treat the Display view as already-handled so a one-frame lag in the state change can't re-open our window.
	wasDisplayOptions = true;
}

void DisplaySettingsMenu::Activate()
{
	isOpen.store(true, std::memory_order_relaxed);
	navFocusPending = true;  // focus the window for controller nav on the first frame it draws
	closing = false;
	animProgress = 0.0f;  // animate in from scratch
}

RE::BSEventNotifyControl DisplaySettingsMenu::ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	if (a_event->menuName == RE::JournalMenu::MENU_NAME) {
		// Gate the per-frame Scaleform polling on the pause menu being open; close our window when it closes.
		journalOpen = a_event->opening;
		wasDisplayOptions = false;
		if (!a_event->opening) {
			isOpen.store(false, std::memory_order_relaxed);  // pause menu gone — drop our window immediately
			navBackPending.store(false, std::memory_order_relaxed);  // nothing to navigate back to
			navFocusPending = false;
			closing = false;
			animProgress = 0.0f;
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

void DisplaySettingsMenu::Draw()
{
	if (!isOpen.load(std::memory_order_relaxed))
		return;

	auto& io = ImGui::GetIO();

	// Open/close animation. animProgress eases 0→1 (open) or 1→0 (closing) using the frame delta. Once a close
	// finishes, actually close the window and queue the page's back-navigation.
	constexpr float kOpenDuration = 0.18f, kCloseDuration = 0.14f;
	const float     dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
	animProgress = std::clamp(animProgress + (closing ? -dt / kCloseDuration : dt / kOpenDuration), 0.0f, 1.0f);
	if (closing && animProgress <= 0.0f) {
		isOpen.store(false, std::memory_order_relaxed);
		closing = false;
		navBackPending.store(true, std::memory_order_relaxed);
		return;
	}
	const float t = closing ? animProgress : (1.0f - (1.0f - animProgress) * (1.0f - animProgress) * (1.0f - animProgress));  // easeOutCubic in
	const float animAlpha = std::clamp(t, 0.0f, 1.0f);
	const float animSlide = (1.0f - t) * 40.0f;  // px: slides up into place on open, down on close

	// Controller vs mouse/keyboard: when a controller is driving, hide our cursor and park the mouse offscreen so
	// ImGui's gamepad nav owns focus (a stale mouse-hover would otherwise keep stealing it). KBM shows the cursor.
	const bool gamepad = s_gamepadActive;
	io.MouseDrawCursor = !gamepad;
	if (gamepad)
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

	// The box is sized by width/height and centered horizontally; boxPushUp shifts it up from vertical center.
	// CS's themed window background fills it and RenderBackgroundBlur applies the menu blur behind it.
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2         winSize{ std::max(100.0f, viewport->Size.x * boxWidth),
        std::max(100.0f, viewport->Size.y * boxHeight) };
	const ImVec2         center{ viewport->Pos.x + viewport->Size.x * 0.5f,
        viewport->Pos.y + viewport->Size.y * (0.5f - boxPushUp) + animSlide };

	// On open, focus the window so it becomes ImGui's nav target (controller directions then move within it).
	if (navFocusPending) {
		ImGui::SetNextWindowFocus();
		navFocusPending = false;
	}
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animAlpha);  // fade in/out with the open/close animation

	// NOTE: no ImGuiWindowFlags_NoNav — it would block controller/keyboard navigation and prevent the window
	// from taking nav focus (which is exactly what we need for D-pad navigation here).
	constexpr auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
	                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
	                       ImGuiWindowFlags_NoTitleBar;
	if (ImGui::Begin("##CSDisplaySettings", nullptr, flags)) {
		const float windowW = ImGui::GetWindowSize().x;

		// Centered title (slightly larger than the body), then the body font scale for the rest of the window.
		ImGui::SetWindowFontScale(1.3f * boxFontScale);
		const char* title = "COMMUNITY SHADERS SETTINGS";
		ImGui::SetCursorPosX((windowW - ImGui::CalcTextSize(title).x) * 0.5f);
		ImGui::TextUnformatted(title);
		ImGui::SetWindowFontScale(boxFontScale);
		ImGui::Separator();

		// Centered fixed-width content column.
		const float avail = ImGui::GetContentRegionAvail().x;
		const float colW = std::min(avail, 860.0f);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - colW) * 0.5f);

		// Render with the "< value >" arrow steppers (scoped to this window); colours come from CS's theme.
		// NavFlattened folds this child's items into the parent's nav so the controller focuses each row directly
		// instead of treating the whole content frame as one nav target.
		Upscaling::useArrowSteppers = true;
		if (ImGui::BeginChild("##content", ImVec2(colW, 0.0f), ImGuiChildFlags_NavFlattened)) {
			ImGui::SetWindowFontScale(boxFontScale);  // child windows don't inherit the parent's font scale
			globals::features::upscaling.DrawSettings();
			if (globals::features::hdrDisplay.loaded) {
				ImGui::SeparatorText("HDR");
				globals::features::hdrDisplay.DrawSettings();
			}
		}
		ImGui::EndChild();
		Upscaling::useArrowSteppers = false;

		// Esc (KBM) or B/Circle (controller) starts the close animation; when it finishes the window closes and
		// the pause menu navigates back (handled at the top of Draw).
		if (!closing && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)))
			closing = true;
	}
	ImGui::End();

	ImGui::PopStyleVar();  // ImGuiStyleVar_Alpha
}
