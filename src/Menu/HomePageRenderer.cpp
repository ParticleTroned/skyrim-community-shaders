#include "HomePageRenderer.h"
#include "PCH.h"

#include <imgui.h>

#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

#include <algorithm>
#include <initializer_list>

namespace
{
	constexpr float FORK_NOTICE_OFFSET_LINES = 2.0f;
	constexpr float FORK_NOTICE_ITALIC_SLANT = 0.18f;

	float CenteredTextX(float windowWidth, float textWidth)
	{
		return std::max(0.0f, (windowWidth - textWidth) * 0.5f);
	}

	void DrawCenteredTextColored(const char* text, float windowWidth, const ImVec4& color)
	{
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		ImGui::SetCursorPosX(CenteredTextX(windowWidth, textSize.x));
		ImGui::TextColored(color, "%s", text);
	}

	void DrawCenteredItalicTextLine(const char* text, float windowWidth, const ImVec4& color)
	{
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		const float lineHeight = ImGui::GetTextLineHeight();
		const float lineHeightWithSpacing = ImGui::GetTextLineHeightWithSpacing();
		const float slantWidth = lineHeight * FORK_NOTICE_ITALIC_SLANT;
		const float localX = CenteredTextX(windowWidth, textSize.x + slantWidth);
		const float localY = ImGui::GetCursorPosY();

		ImGui::SetCursorPosX(localX);
		const ImVec2 textPos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const int vtxStart = drawList->VtxBuffer.Size;
		drawList->AddText(textPos, ImGui::GetColorU32(color), text);
		const int vtxEnd = drawList->VtxBuffer.Size;

		const float lineBottom = textPos.y + lineHeight;
		for (int i = vtxStart; i < vtxEnd; ++i) {
			ImDrawVert& vtx = drawList->VtxBuffer[i];
			vtx.pos.x += (lineBottom - vtx.pos.y) * FORK_NOTICE_ITALIC_SLANT;
		}

		ImGui::SetCursorPosY(localY + lineHeightWithSpacing);
	}

	void DrawCenteredItalicTextBlock(std::initializer_list<const char*> lines, float windowWidth, const ImVec4& color)
	{
		for (const char* line : lines) {
			DrawCenteredItalicTextLine(line, windowWidth, color);
		}
	}
}

// Static member definitions
bool HomePageRenderer::isFirstTimeSetupShown = false;
uint32_t HomePageRenderer::keyThatClosedDialog = 0;

bool HomePageRenderer::ShouldSkipKeyRelease(uint32_t key)
{
	if (keyThatClosedDialog && key == keyThatClosedDialog) {
		keyThatClosedDialog = 0;
		return true;
	}
	return false;
}

void HomePageRenderer::RenderHomePage()
{
	ImGui::BeginChild("HomePage", ImVec2(0, 0), false);

	RenderWelcomeSection();
	ImGui::Spacing();

	// RenderQuickLinksSection();
	// ImGui::Spacing();

	// RenderFAQSection();

	ImGui::EndChild();
}

void HomePageRenderer::RenderWelcomeSection()
{
	const float scale = Util::GetUIScale();
	auto menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();
	const ImVec4 titleColor = theme.StatusPalette.InfoColor;
	ImVec4 versionColor = theme.StatusPalette.InfoColor;
	versionColor.w *= 0.86f;
	const ImVec4 forkNoticeColor = theme.Palette.Text;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * scale, 8.0f * scale));

	// Main title - centered with safe font handling
	ImGuiIO& io = ImGui::GetIO();
	ImFont* titleFont = nullptr;

	// Safely check if we have multiple fonts and the second one is valid
	if (io.Fonts && io.Fonts->Fonts.Size > 1 && io.Fonts->Fonts[1] != nullptr) {
		titleFont = io.Fonts->Fonts[1];
	}

	// Reserve the previous large title footprint so the fork notice stays in place.
	ImGui::SetWindowFontScale(TITLE_FONT_SCALE);

	// Only push font if we have a valid one, otherwise use default scaled
	if (titleFont) {
		ImGui::PushFont(titleFont, titleFont->LegacySize);
	}

	ImVec2 windowSize = ImGui::GetWindowSize();
	const float titleBlockY = ImGui::GetCursorPosY();
	const float titleBlockHeight = ImGui::GetTextLineHeightWithSpacing();
	const float baseLineHeightWithSpacing = titleBlockHeight / TITLE_FONT_SCALE;
	const char* forkTitle = "CS 1.7.1 Particle Lights Fork";
	const char* forkVersion = "PL3.15-VR";
	const float titleLineGap = 2.0f * scale;
	const float titleAreaBottomY = titleBlockY + titleBlockHeight + ImGui::GetStyle().ItemSpacing.y +
	                               baseLineHeightWithSpacing * FORK_NOTICE_OFFSET_LINES;

	ImGui::SetWindowFontScale(TITLE_FORK_FONT_SCALE);
	const float titleLineHeight = ImGui::GetTextLineHeight();

	ImGui::SetWindowFontScale(TITLE_VERSION_FONT_SCALE);
	const float versionLineHeight = ImGui::GetTextLineHeight();

	const float titleGroupHeight = titleLineHeight + titleLineGap + versionLineHeight;
	const float titleGroupY = titleBlockY + std::max(0.0f, (titleAreaBottomY - titleBlockY - titleGroupHeight) * 0.5f);

	ImGui::SetWindowFontScale(TITLE_FORK_FONT_SCALE);
	ImGui::SetCursorPosY(titleGroupY);
	DrawCenteredTextColored(forkTitle, windowSize.x, titleColor);

	ImGui::SetWindowFontScale(TITLE_VERSION_FONT_SCALE);
	ImGui::SetCursorPosY(titleGroupY + titleLineHeight + titleLineGap);
	DrawCenteredTextColored(forkVersion, windowSize.x, versionColor);

	// Only pop font if we pushed one
	if (titleFont) {
		ImGui::PopFont();
	}

	// Reset text scale back to normal
	ImGui::SetWindowFontScale(1.0f);
	ImGui::SetCursorPosY(titleAreaBottomY);

	// windowSize is already captured above for title centering

	// Intro text - centered line-by-line so the fork notice remains visually aligned.
	DrawCenteredItalicTextBlock({
									"This is an unofficial fork of Community Shaders restoring Particle Lights.",
									"Not affiliated with or endorsed by the Community Shaders team",
									"- Visit their Discord to get the Original and support their outstanding efforts -",
								},
		windowSize.x, forkNoticeColor);

	ImGui::Spacing();

	// Vertical padding between intro text and the Discord banner.
	ImGui::Dummy(ImVec2(0.0f, 25.0f * scale));

	// Discord banner - centered with proper error checking
	bool discordIconAvailable = false;

	// Check if menu exists, has icons, and Discord icon is loaded
	if (menu && menu->uiIcons.discord.texture != nullptr &&
		menu->uiIcons.discord.size.x > 0 && menu->uiIcons.discord.size.y > 0) {
		discordIconAvailable = true;
	}

	if (discordIconAvailable) {
		// Calculate scaled icon size based on window width, with min/max constraints
		ImVec2 originalSize = ImVec2(menu->uiIcons.discord.size.x, menu->uiIcons.discord.size.y);

		// Compute width based on window size with constraints and padding (handles very small windows)
		float ratioWidth = windowSize.x * DISCORD_BANNER_TARGET_WIDTH_RATIO;
		float aspectRatio = originalSize.y / originalSize.x;
		float maxAllowed = std::max(1.0f, windowSize.x - DISCORD_BANNER_PADDING_MARGIN * scale);
		float upperBound = std::min(DISCORD_BANNER_MAX_WIDTH * scale, maxAllowed);
		float lowerBound = std::min(DISCORD_BANNER_MIN_WIDTH * scale, upperBound);
		float targetWidth = std::clamp(ratioWidth, lowerBound, upperBound);

		ImVec2 iconSize = ImVec2(targetWidth, targetWidth * aspectRatio);
		ImGui::SetCursorPosX((windowSize.x - iconSize.x) * 0.5f);

		// Purely decorative: draw the banner image only (no button, no click, no tooltip)
		ImGui::Image(menu->uiIcons.discord.texture, iconSize);
	} else {
		// No Discord icon available: keep layout roughly consistent with a dummy spacer,
		// but do not show a clickable button or link.
		float dummyWidth = 200.0f * scale;
		ImGui::SetCursorPosX((windowSize.x - dummyWidth) * 0.5f);
		ImGui::Dummy(ImVec2(dummyWidth, 0.0f));
	}

	// Pop the style var we pushed at the start
	ImGui::PopStyleVar();
	// Close RenderWelcomeSection()
}

void HomePageRenderer::RenderFirstTimeSetupDialog()
{
	// Block input to the game and make cursor visible - input blocking is handled by ShouldSwallowInput()
	auto& io = ImGui::GetIO();
	io.WantCaptureMouse = true;
	io.WantCaptureKeyboard = true;
	io.MouseDrawCursor = true;  // Show ImGui cursor

	const float uiScale = Util::GetUIScale();

	// Center the window properly with rounded corners and thin border
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	// Set a minimum width for better layout, but allow auto-sizing for height
	ImGui::SetNextWindowSizeConstraints(ImVec2(DIALOG_MIN_WIDTH * uiScale, 0), ImVec2(DIALOG_MAX_WIDTH * uiScale, FLT_MAX));
	ImGui::SetNextWindowFocus();

	// Style for rounded window with thin border
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, DIALOG_CORNER_ROUNDING * uiScale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;

	if (!ImGui::Begin("##FirstTimeSetup", nullptr, flags)) {
		ImGui::PopStyleVar(2);
		ImGui::End();
		return;
	}

	// Draw fullscreen fade on the dialog's own draw list (renders at dialog's z-position,
	// covering all windows beneath, with dialog content drawn on top)
	auto* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRectFullScreen();
	drawList->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, MODAL_OVERLAY_ALPHA));
	drawList->PopClipRect();

	// Set absolute font size for better readability in this welcome dialog
	float targetFontSize = 27.0f * uiScale;
	float currentFontSize = std::max(ImGui::GetFontSize(), 1.0f);
	float fontScale = targetFontSize / currentFontSize;
	ImGui::SetWindowFontScale(fontScale);

	auto menu = Menu::GetSingleton();

	// Render CS logo as background watermark with proper aspect ratio
	if (menu && menu->uiIcons.logo.texture) {
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();

		// Get the original texture size to maintain aspect ratio
		ImVec2 textureSize = menu->uiIcons.logo.size;
		float aspectRatio = textureSize.x / textureSize.y;

		// Set desired height and calculate width to maintain aspect ratio
		float logoHeight = LOGO_WATERMARK_HEIGHT * uiScale;
		float logoWidth = logoHeight * aspectRatio;

		ImVec2 logoMin(windowPos.x + (windowSize.x - logoWidth) * 0.5f,
			windowPos.y + (windowSize.y - logoHeight) * 0.5f);
		ImVec2 logoMax(logoMin.x + logoWidth, logoMin.y + logoHeight);

		// Determine watermark color based on monochrome logo setting
		ImU32 watermarkColor;
		if (menu->GetSettings().Theme.UseMonochromeLogo) {
			ImVec4 textColor = menu->GetSettings().Theme.Palette.Text;
			textColor.w = 0.24f;  // Low alpha for watermark effect
			watermarkColor = ImGui::GetColorU32(textColor);
		} else {
			watermarkColor = IM_COL32(255, 255, 255, 180);
		}

		// Render as subtle watermark background
		ImGui::GetWindowDrawList()->AddImage(menu->uiIcons.logo.texture, logoMin, logoMax,
			ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
	}

	// Center all content
	float windowWidth = ImGui::GetWindowWidth();

	// Welcome title - centered
	const char* welcomeTitle = "Welcome to Community Shaders - Particle Lights (Unofficial Fork)!";
	float welcomeTitleWidth = ImGui::CalcTextSize(welcomeTitle).x;
	ImGui::SetCursorPosX((windowWidth - welcomeTitleWidth) * 0.5f);
	ImGui::Text("%s", welcomeTitle);

	ImGui::Spacing();

	// Version text - wrapped and centered
	const char* versionText = "This appears to be a new install, update, or reinstallation of Community Shaders.";
	float textPadding = 40.0f * uiScale;  // Padding from window edges

	// Use a centered region for wrapped text
	ImGui::SetCursorPosX(textPadding);
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(windowWidth - textPadding);

	// Calculate the wrapped text size to center it
	ImVec2 textSize = ImGui::CalcTextSize(versionText, nullptr, true, windowWidth - textPadding * 2);
	float centerOffset = (windowWidth - textPadding * 2 - textSize.x) * 0.5f;
	if (centerOffset > 0) {
		ImGui::SetCursorPosX(textPadding + centerOffset);
	}

	ImGui::TextWrapped("%s", versionText);
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();

	ImGui::Spacing();

	// Description - centered
	const char* description = "Please select a hotkey to access the menu:";
	float descWidth = ImGui::CalcTextSize(description).x;
	ImGui::SetCursorPosX((windowWidth - descWidth) * 0.5f);
	ImGui::Text("%s", description);

	// Hotkey selection - clickable hotkey text
	// Show current toggle key and allow user to change it by clicking on it
	auto& themeSettings = menu->GetTheme();
	std::string currentKeyName = Util::Input::KeyIdToString(menu->GetSettings().ToggleKey);

	// Increase font size for hotkey text
	ImGui::SetWindowFontScale(fontScale * HOTKEY_TEXT_SCALE_MULTIPLIER);

	// Calculate text dimensions for centering and button area
	float hotkeyWidth = ImGui::CalcTextSize(currentKeyName.c_str()).x;
	float centerX = (windowWidth - hotkeyWidth) * 0.5f;
	ImGui::SetCursorPosX(centerX);

	// Create invisible button for hover detection and clicking
	ImVec2 buttonPos = ImGui::GetCursorScreenPos();
	ImVec2 hotkeyTextSize = ImGui::CalcTextSize(currentKeyName.c_str());
	bool hovered = false;
	bool clicked = false;

	ImGui::PushID("HotkeyButton");
	if (ImGui::InvisibleButton("##HotkeyClick", hotkeyTextSize)) {
		clicked = true;
	}
	hovered = ImGui::IsItemHovered();
	ImGui::PopID();

	// Set cursor position back for text rendering
	ImGui::SetCursorScreenPos(buttonPos);

	// Choose color based on hover state - darken when hovered.
	ImVec4 hotkeyColor = hovered ?
	                         ImVec4(themeSettings.StatusPalette.CurrentHotkey.x * 0.7f,
								 themeSettings.StatusPalette.CurrentHotkey.y * 0.7f,
								 themeSettings.StatusPalette.CurrentHotkey.z * 0.7f,
								 themeSettings.StatusPalette.CurrentHotkey.w) :
	                         themeSettings.StatusPalette.CurrentHotkey;

	ImGui::TextColored(hotkeyColor, "%s", currentKeyName.c_str());

	// Reset font scale
	ImGui::SetWindowFontScale(fontScale);

	// Handle click to start hotkey capture
	if (clicked) {
		menu->settingToggleKey = true;
	}

	// Show hotkey capture message or hotkey text
	if (menu->settingToggleKey) {
		const char* pressKeyText = "Press any key to set as toggle key...";
		float pressKeyWidth = ImGui::CalcTextSize(pressKeyText).x;
		ImGui::SetCursorPosX((windowWidth - pressKeyWidth) * 0.5f);
		ImGui::Text("%s", pressKeyText);
	}

	ImGui::Spacing();

	// "You can change this later" text - wrapped and centered
	const char* laterText = "You can change this later in General > Keybindings.";
	float laterWidth = ImGui::CalcTextSize(laterText).x;
	if (laterWidth > windowWidth - 40.0f * uiScale) {
		// Text is too wide, use wrapped text with centering
		float laterTextPadding = 40.0f * uiScale;

		ImGui::SetCursorPosX(laterTextPadding);
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(windowWidth - laterTextPadding);

		// Calculate the wrapped text size to center it
		ImVec2 laterTextSize = ImGui::CalcTextSize(laterText, nullptr, true, windowWidth - laterTextPadding * 2);
		float laterCenterOffset = (windowWidth - laterTextPadding * 2 - laterTextSize.x) * 0.5f;
		if (laterCenterOffset > 0) {
			ImGui::SetCursorPosX(laterTextPadding + laterCenterOffset);
		}

		ImGui::TextWrapped("%s", laterText);
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
	} else {
		// Text fits, center it normally
		ImGui::SetCursorPosX((windowWidth - laterWidth) * 0.5f);
		ImGui::Text("%s", laterText);
	}

	ImGui::Spacing();

	// Check for Enter or Escape key to close, but only if not capturing a hotkey
	bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
	bool shouldClose = (enterPressed || escapePressed) && !menu->settingToggleKey;

	if (shouldClose) {
		MarkFirstTimeSetupComplete(escapePressed ? VK_ESCAPE : VK_RETURN);
		// Note: Settings are automatically saved to ensure welcome screen won't show again
	}

	// Center the help text
	const char* helpText = "Press Escape or Enter to continue";
	float helpWidth = ImGui::CalcTextSize(helpText).x;
	ImGui::SetCursorPosX((windowWidth - helpWidth) * 0.5f);
	ImGui::TextDisabled("%s", helpText);

	ImGui::End();
	ImGui::PopStyleVar(2);
}

bool HomePageRenderer::ShouldShowFirstTimeSetup()
{
	// Never show first-time setup in VR mode
	if (REL::Module::IsVR()) {
		return false;
	}

	// Check if already completed this session
	if (isFirstTimeSetupShown) {
		return false;
	}

	// Check if first-time setup has been completed using the Menu settings
	auto menu = Menu::GetSingleton();
	return !menu->GetSettings().FirstTimeSetupCompleted;
}

void HomePageRenderer::MarkFirstTimeSetupComplete(uint32_t closingKey)
{
	// Set the flag in the Menu settings
	auto menu = Menu::GetSingleton();
	menu->GetSettings().FirstTimeSetupCompleted = true;
	menu->settingToggleKey = false;

	// Immediately save settings to ensure the flag is persisted
	// This prevents the welcome screen from showing again even if user doesn't manually save
	globals::state->Save();

	isFirstTimeSetupShown = true;  // Mark as shown this session
	keyThatClosedDialog = closingKey;
}
