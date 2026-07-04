#pragma once

#include <cstdint>
class HomePageRenderer
{
public:
	// Social links
	static constexpr const char* DISCORD_URL = "https://discord.gg/utSWStYTx2";
	static constexpr const char* GITHUB_URL = "https://github.com/ParticleTroned/skyrim-community-shaders/tree/cs-1.7-PL-SE";

	// Font scales
	static constexpr float TITLE_FONT_SCALE = 2.0f;
	static constexpr float TITLE_PRIMARY_LINE_FONT_SCALE = 1.85f;
	static constexpr float TITLE_SECONDARY_LINE_FONT_SCALE = 0.85f;
	static constexpr float SETUP_DIALOG_FONT_SCALE = 0.75f;
	static constexpr float HOTKEY_TEXT_SCALE_MULTIPLIER = 1.2f;
	static constexpr uint8_t MODAL_OVERLAY_ALPHA = 160;

	// First-time setup dialog layout (1080p baseline, scaled by GetUIScale)
	static constexpr float DIALOG_MIN_WIDTH = 500.0f;
	static constexpr float DIALOG_MAX_WIDTH = 600.0f;
	static constexpr float DIALOG_CORNER_ROUNDING = 8.0f;

	// Logo watermark height (in pixels)
	static constexpr float LOGO_WATERMARK_HEIGHT = 260.0f;

	// Home-page artwork and link widget layout.
	static constexpr float FAULTIER_TARGET_WIDTH_RATIO = 0.24f;
	static constexpr float FAULTIER_MIN_WIDTH = 96.0f;
	static constexpr float FAULTIER_MAX_WIDTH = 180.0f;
	static constexpr float FAULTIER_MAX_HEIGHT = 320.0f;
	static constexpr float HOME_LINK_BUTTON_HEIGHT = 42.0f;
	static constexpr float HOME_LINK_BUTTON_SPACING = 16.0f;
	static constexpr float HOME_LINK_GITHUB_BUTTON_WIDTH = 140.0f;
	static constexpr float HOME_LINK_DISCORD_BUTTON_MIN_WIDTH = 120.0f;
	static constexpr float HOME_LINK_DISCORD_BUTTON_MAX_WIDTH = 220.0f;
	static constexpr float HOME_LINK_ROW_PADDING_MARGIN = 40.0f;

	static void RenderHomePage();

	// First-time setup management
	static bool ShouldShowFirstTimeSetup();
	static void RenderFirstTimeSetupDialog();
	static bool TryCompleteFirstTimeSetupFromInput(uint32_t key, bool skipNextKeyRelease = true);
	static bool ShouldSkipKeyRelease(uint32_t key);

private:
	static void RenderModeSection();
	static void RenderWelcomeSection();
	static void RenderCacheMismatchSection();
	static void MarkFirstTimeSetupComplete(uint32_t closingKey, bool skipNextKeyRelease = true);

	// State
	static bool isFirstTimeSetupShown;
	static uint32_t keyThatClosedDialog;
};
