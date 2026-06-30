#pragma once

#include <RE/Skyrim.h>

/**
 * @brief PhotoMode-style "Display Settings" in-game menu.
 *
 * Adds a "Display Settings" entry to Skyrim's System (pause) menu by hijacking the Mod Manager
 * Scaleform slot — no ESP/Papyrus required (mechanism adapted from powerof3/PhotoMode, MIT). Selecting
 * it opens a centered, non-movable, screen-covering ImGui window (rendered inside CS's existing ImGui
 * frame via OverlayRenderer) that will hold the upscaling + HDR settings.
 */
class DisplaySettingsMenu : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static DisplaySettingsMenu* GetSingleton();

	/** @brief Register the MenuOpenCloseEvent sink. Call once the UI singleton is available (kDataLoaded). */
	void Register();

	/** @brief Draw the window when open. MUST be called inside the ImGui frame (from OverlayRenderer). */
	void Draw();

	[[nodiscard]] bool IsOpen() const { return isOpen; }
	void Close() { isOpen = false; }

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

private:
	DisplaySettingsMenu() = default;

	// Rename the System menu's Mod Manager Scaleform entry to "Display Settings". Returns true on success.
	bool SetupSystemMenuEntry() const;
	void Activate();

	bool isOpen = false;
	// True until the entry has been injected into the currently-open System menu (re-armed each open).
	bool wantSystemMenuEntry = false;
};
