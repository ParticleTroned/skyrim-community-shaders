#pragma once

#include <RE/Skyrim.h>

#include <atomic>

/**
 * @brief PhotoMode-style "Display Settings" in-game menu.
 *
 * Opens when the player selects System (pause) menu → SETTINGS → DISPLAY. While the pause menu is open we poll
 * its Scaleform state (SystemPage.iCurrentState / SettingsList.selectedIndex); when the Display options view is
 * entered we open a centered, screen-covering ImGui window over it (rendered inside CS's existing ImGui frame
 * via OverlayRenderer). Closing it sends the pause menu back to the Settings-category list.
 *
 * Threading: Draw() runs on the present thread; Update()/ProcessEvent() run on the main thread. The two flags
 * that cross that boundary (isOpen, navBackPending) are atomic.
 */
class DisplaySettingsMenu : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static DisplaySettingsMenu* GetSingleton();

	/** @brief Register the MenuOpenCloseEvent sink. Call once the UI singleton is available (kDataLoaded). */
	void Register();

	/** @brief Draw the window when open. MUST be called inside the ImGui frame (from OverlayRenderer). */
	void Draw();

	/** @brief Poll the pause-menu Scaleform state to detect Settings→Display. Call every frame (main thread). */
	void Update();

	[[nodiscard]] bool IsOpen() const { return isOpen.load(std::memory_order_relaxed); }
	void Close() { isOpen.store(false, std::memory_order_relaxed); }

	// Tracks whether the most recent input came from a controller, so Draw() can hide the cursor + park the mouse
	// offscreen for gamepad nav. Set by Menu's input queue as gamepad/KBM events arrive.
	static void SetGamepadActive(bool a_gamepad) { s_gamepadActive = a_gamepad; }

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

private:
	DisplaySettingsMenu() = default;

	void Activate();

	// The pause menu's System tab GFx view (holds the SystemPage movieclip), or nullptr if unavailable.
	RE::GPtr<RE::GFxMovieView> GetSystemTabView() const;
	// Send the pause-menu SystemPage back to its Settings-category list (called when our window closes).
	void NavigateSystemPageBack();

	// Written on the present thread (Draw, on close), read/cleared on the main thread (Update) → atomic.
	std::atomic<bool> isOpen{ false };
	std::atomic<bool> navBackPending{ false };

	// Main-thread-only state (ProcessEvent / Update).
	bool journalOpen = false;       // pause (Journal) menu open — gates the Scaleform polling in Update()
	bool wasDisplayOptions = false; // last-frame "Display options view showing", for rising-edge detection
	// Set on open; the first Draw frame focuses the window so controller nav engages, then clears it.
	bool navFocusPending = false;

	// Open/close animation: animProgress eases 0→1 on open and 1→0 on close (drives fade + slide). While
	// closing the window keeps drawing until it reaches 0, then actually closes + navigates the page back.
	float animProgress = 0.0f;
	bool  closing = false;

	// Window box geometry as fractions of the viewport; centered horizontally, boxPushUp shifts it up from
	// vertical center, boxFontScale scales the content font. (Tuned in-game, then baked here.)
	float boxWidth = 0.300f;
	float boxHeight = 0.676f;
	float boxPushUp = 0.017f;
	float boxFontScale = 1.0f;

	static inline bool s_gamepadActive = false;
};
