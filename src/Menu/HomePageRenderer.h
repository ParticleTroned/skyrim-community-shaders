#pragma once

#include <cstdint>
class HomePageRenderer
{
public:
	// Font scales
	static constexpr float TITLE_FONT_SCALE = 2.0f;
	static constexpr float TITLE_FORK_FONT_SCALE = 1.85f;
	static constexpr float TITLE_VERSION_FONT_SCALE = 0.85f;
	static constexpr float SETUP_DIALOG_FONT_SCALE = 0.75f;
	static constexpr float HOTKEY_TEXT_SCALE_MULTIPLIER = 1.2f;
	static constexpr uint8_t MODAL_OVERLAY_ALPHA = 160;

	// First-time setup dialog layout (1080p baseline, scaled by GetUIScale)
	static constexpr float DIALOG_MIN_WIDTH = 500.0f;
	static constexpr float DIALOG_MAX_WIDTH = 600.0f;
	static constexpr float DIALOG_CORNER_ROUNDING = 8.0f;

	// Logo watermark height (in pixels)
	static constexpr float LOGO_WATERMARK_HEIGHT = 260.0f;

	// Banner scaling constants.
	static constexpr float DISCORD_BANNER_TARGET_WIDTH_RATIO = 0.85f;  // 85% of window width
	static constexpr float DISCORD_BANNER_MIN_WIDTH = 150.0f;
	static constexpr float DISCORD_BANNER_MAX_WIDTH = 200.0f;
	static constexpr float DISCORD_BANNER_PADDING_MARGIN = 40.0f;

	static void RenderHomePage();

	// First-time setup management
	static bool ShouldShowFirstTimeSetup();
	static void RenderFirstTimeSetupDialog();
	static bool ShouldSkipKeyRelease(uint32_t key);

private:
	static void RenderWelcomeSection();
	static void MarkFirstTimeSetupComplete(uint32_t closingKey);

	// State
	static bool isFirstTimeSetupShown;
	static uint32_t keyThatClosedDialog;
};
