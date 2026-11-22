#include "HomePageRenderer.h"
#include "PCH.h"
#include "SettingsPage.h"
#include "Utils/WinApi.h"

#include <cmath>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

#include "Globals.h"
#include "Menu.h"
#include "Plugin.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>

namespace
{
	float CenteredTextX(float windowWidth, float textWidth)
	{
		return std::max(0.0f, (windowWidth - textWidth) * 0.5f);
	}

	void DrawCenteredText(const char* text, float left, float width, const ImVec4& color, bool italic = false)
	{
		const float slant = italic ? ImGui::GetTextLineHeight() * .18f : 0;
		const float wrapWidth = std::max(1.0f, width - slant);
		ImGui::PushTextWrapPos(-1);
		const SKSE::stl::scope_exit restoreWrap([] { ImGui::PopTextWrapPos(); });
		auto* draw = ImGui::GetWindowDrawList();
		const int start = draw->VtxBuffer.Size;
		const float top = ImGui::GetCursorScreenPos().y;
		const float localTop = ImGui::GetCursorPosY();
		const char* end = text + std::strlen(text);
		int row = 0;
		for (const char* line = text; line < end; ++row) {
			const char* paragraphEnd = std::find(line, end, '\n');
			const char* lineEnd = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), line, paragraphEnd, wrapWidth);
			if (lineEnd == line && line < paragraphEnd) {
				unsigned int character;
				lineEnd += std::max(1, ImTextCharFromUtf8(&character, line, paragraphEnd));
			}
			const auto size = ImGui::CalcTextSize(line, lineEnd, false);
			ImGui::SetCursorPos({ left + CenteredTextX(width, size.x + slant), localTop + row * ImGui::GetTextLineHeight() });
			ImGui::TextColored(color, "%.*s", static_cast<int>(lineEnd - line), line);
			line = lineEnd;
			while (line < paragraphEnd && (*line == ' ' || *line == '\t'))
				++line;
			if (line == paragraphEnd && line < end)
				++line;
		}
		if (italic) {
			const float height = ImGui::GetTextLineHeight();
			for (int i = start; i < draw->VtxBuffer.Size; ++i) {
				auto& vertex = draw->VtxBuffer[i];
				vertex.pos.x += (height - std::fmod(std::max(0.0f, vertex.pos.y - top), height)) * .18f;
			}
		}
	}

	void DrawCenteredTextColored(const char* text, float windowWidth, const ImVec4& color)
	{
		DrawCenteredText(text, ImGui::GetStyle().WindowPadding.x, windowWidth - ImGui::GetStyle().WindowPadding.x * 2, color);
	}

	void DrawCenteredItalicTextLine(const char* text, float windowWidth, const ImVec4& color)
	{
		DrawCenteredText(text, ImGui::GetStyle().WindowPadding.x, windowWidth - ImGui::GetStyle().WindowPadding.x * 2, color, true);
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
	MenuUI::SettingsPage page("Home", {});
	if (page.Is("overview"))
		RenderWelcomeSection();
}

void HomePageRenderer::RenderWelcomeSection()
{
	static MenuUI::ActionFeedback feedback;
	const float scale = Util::GetUIScale();
	auto menu = Menu::GetSingleton();
	const auto& theme = menu->GetTheme();
	const ImVec4 titleColor = theme.StatusPalette.InfoColor;
	const ImVec4 forkNoticeColor = theme.Palette.Text;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * scale, 8.0f * scale));

	// Main title - centered with safe font handling
	ImGuiIO& io = ImGui::GetIO();
	ImFont* titleFont = nullptr;

	// Safely check if we have multiple fonts and the second one is valid
	if (io.Fonts && io.Fonts->Fonts.Size > 1 && io.Fonts->Fonts[1] != nullptr) {
		titleFont = io.Fonts->Fonts[1];
	}

	// Only push font if we have a valid one, otherwise use default scaled
	if (titleFont) {
		ImGui::PushFont(titleFont, titleFont->LegacySize);
	}

	ImVec2 windowSize = ImGui::GetWindowSize();
	const char* forkTitle = Plugin::DISPLAY_NAME.data();
	const char* forkVersion = Plugin::FORK_VERSION.data();
	const float titleLineGap = 2.0f * scale;

	ImGui::SetWindowFontScale(TITLE_PRIMARY_LINE_FONT_SCALE);
	DrawCenteredTextColored(forkTitle, windowSize.x, titleColor);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + titleLineGap);
	ImGui::SetWindowFontScale(TITLE_SECONDARY_LINE_FONT_SCALE);
	DrawCenteredTextColored(forkVersion, windowSize.x, titleColor);

	// Only pop font if we pushed one
	if (titleFont) {
		ImGui::PopFont();
	}

	// Reset text scale back to normal
	ImGui::SetWindowFontScale(1.0f);

	// Intro text - centered line-by-line so the fork notice remains visually aligned.
	DrawCenteredItalicTextBlock({
									"This is an unofficial fork of Community Shaders focussed on VR, performance and customisability.",
									"Not affiliated with or endorsed by the Community Shaders team",
									"\xE2\x80\x93 Visit their Discord to get the Original and support their efforts \xE2\x80\x93",
								},
		windowSize.x, forkNoticeColor);

	ImGui::Spacing();

	// Faultier artwork - centered with proper error checking.
	bool faultierAvailable = false;
	if (menu && menu->uiIcons.faultier.texture != nullptr &&
		menu->uiIcons.faultier.size.x > 0 && menu->uiIcons.faultier.size.y > 0) {
		faultierAvailable = true;
	}

	const auto& style = ImGui::GetStyle();
	const float linkButtonHeight = HOME_LINK_BUTTON_HEIGHT * scale;
	// Keep the links and third-party heading visible below the artwork.
	const float footerHeight = linkButtonHeight + ImGui::GetFrameHeight() + style.ItemSpacing.y * 4;
	const ImVec2 originalSize = faultierAvailable ? menu->uiIcons.faultier.size : ImVec2(FAULTIER_MAX_WIDTH, FAULTIER_MAX_HEIGHT);
	const float aspectRatio = originalSize.y / originalSize.x;
	const float maxAllowedWidth = std::max(1.0f, windowSize.x - HOME_LINK_ROW_PADDING_MARGIN * scale);
	const float upperWidth = std::min(FAULTIER_MAX_WIDTH * scale, maxAllowedWidth);
	const float lowerWidth = std::min(FAULTIER_MIN_WIDTH * scale, upperWidth);
	float targetWidth = std::clamp(windowSize.x * FAULTIER_TARGET_WIDTH_RATIO, lowerWidth, upperWidth);
	float targetHeight = targetWidth * aspectRatio;
	const float maxHeight = std::max(1.0f, std::min(FAULTIER_MAX_HEIGHT * scale, ImGui::GetContentRegionAvail().y - footerHeight));
	if (targetHeight > maxHeight) {
		targetHeight = maxHeight;
		targetWidth = targetHeight / aspectRatio;
	}
	const ImVec2 imageSize(targetWidth, targetHeight);
	ImGui::SetCursorPosX(CenteredTextX(windowSize.x, imageSize.x));
	if (faultierAvailable)
		ImGui::Image(menu->uiIcons.faultier.texture, imageSize);
	else
		ImGui::Dummy(imageSize);

	ImGui::Spacing();

	struct HomeLink
	{
		const char* label;
		const char* url;
		const char* tooltip;
	};
	constexpr std::array<HomeLink, 3> links{ {
		{ "GitHub##HomeGitHubButton", GITHUB_URL, "Open the CSX GitHub repository." },
		{ "Nexus Mods##HomeNexusModsButton", NEXUS_MODS_URL, "Open the CSX Nexus Mods page." },
		{ "Discord##HomeDiscordButton", DISCORD_URL, "Open the MGO Discord invite." },
	} };
	const float linkButtonSpacing = HOME_LINK_BUTTON_SPACING * scale;
	float minimumButtonWidth = 0;
	for (const auto& link : links)
		minimumButtonWidth = std::max(minimumButtonWidth, ImGui::CalcTextSize(link.label, nullptr, true).x + style.FramePadding.x * 2);
	const float linkCount = static_cast<float>(links.size());
	const float availableButtonWidth = (windowSize.x - style.WindowPadding.x * 2 - linkButtonSpacing * (linkCount - 1)) / linkCount;
	const float linkButtonWidth = std::max(minimumButtonWidth, std::min(HOME_LINK_BUTTON_WIDTH * scale, availableButtonWidth));
	const float linkRowWidth = linkButtonWidth * linkCount + linkButtonSpacing * (linkCount - 1);
	ImGui::SetCursorPosX(CenteredTextX(windowSize.x, linkRowWidth));
	for (size_t i = 0; i < links.size(); ++i) {
		if (i > 0)
			ImGui::SameLine(0.0f, linkButtonSpacing);
		const auto& link = links[i];
		if (Util::ButtonWithFlash(link.label, ImVec2(linkButtonWidth, linkButtonHeight)))
			feedback.error = !Util::OpenInShell(link.url, feedback.message);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(link.tooltip);
	}
	feedback.Draw();

	ImGui::Spacing();
	const char* technologiesLabel = "Third-party technologies##HomeThirdPartyTechnologies";
	const float technologiesWidth = ImGui::CalcTextSize(technologiesLabel, nullptr, true).x + ImGui::GetTreeNodeToLabelSpacing();
	ImGui::SetCursorPosX(CenteredTextX(windowSize.x, technologiesWidth));
	if (ImGui::TreeNodeEx(technologiesLabel, ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
		DrawCenteredTextColored("Includes NVIDIA DLSS / NGX and NVIDIA Reflex technology. NVIDIA GeForce RTX is a trademark of NVIDIA Corporation.", windowSize.x, forkNoticeColor);
		DrawCenteredTextColored("Includes AMD FidelityFX Super Resolution (FSR) and AMD Radeon Anti-Lag 2 technology. AMD, FidelityFX and Radeon are trademarks of Advanced Micro Devices, Inc.", windowSize.x, forkNoticeColor);
		DrawCenteredTextColored("NVIDIA and AMD components retain their own licences. Licence files and third-party notices are included with their components.", windowSize.x, Util::Color::SecondaryText());
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
	// ImGui retains the window scale, so remove it before measuring the base font.
	ImGui::SetWindowFontScale(1.0f);
	float targetFontSize = 27.0f * uiScale;
	float currentFontSize = std::max(ImGui::GetFontSize(), 1.0f);
	float fontScale = targetFontSize / currentFontSize;
	ImGui::SetWindowFontScale(fontScale);

	auto menu = Menu::GetSingleton();

	// Render CSX logo as background watermark with proper aspect ratio
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
	const char* welcomeTitle = "Welcome to Community Shaders Expanded (CSX) (Unofficial Fork)!";
	float welcomeTitleWidth = ImGui::CalcTextSize(welcomeTitle).x;
	ImGui::SetCursorPosX((windowWidth - welcomeTitleWidth) * 0.5f);
	ImGui::Text("%s", welcomeTitle);

	ImGui::Spacing();

	// Version text - wrapped and centered
	const char* versionText = "This appears to be a new install, update, or reinstallation of CSX.";
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

	// CSX Editor hotkey status — updates live as user picks keys
	{
		auto& weatherKey = menu->GetSettings().CSEditorToggleKey;
		if (weatherKey.empty()) {
			const char* warnText = "CSX Editor hotkey unbound \xe2\x80\x94 chosen key uses Shift";
			ImGui::SetCursorPosX(CenteredTextX(windowWidth, ImGui::CalcTextSize(warnText).x));
			Util::Text::Warning("%s", warnText);
		} else {
			std::string infoStr = "CSX Editor hotkey will be: " + Util::Input::KeyIdToString(weatherKey);
			ImGui::SetCursorPosX(CenteredTextX(windowWidth, ImGui::CalcTextSize(infoStr.c_str()).x));
			ImGui::TextDisabled("%s", infoStr.c_str());
		}
	}

	ImGui::Spacing();

	// "You can change this later" text - wrapped and centered
	const char* laterText = "You can change this later in Settings > Bindings.";
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

bool HomePageRenderer::TryCompleteFirstTimeSetupFromInput(uint32_t key, bool skipNextKeyRelease)
{
	if (key != VK_RETURN && key != VK_ESCAPE) {
		return false;
	}

	if (!ShouldShowFirstTimeSetup()) {
		return false;
	}

	auto menu = Menu::GetSingleton();
	if (menu->settingToggleKey) {
		return false;
	}

	MarkFirstTimeSetupComplete(key, skipNextKeyRelease);
	return true;
}

void HomePageRenderer::MarkFirstTimeSetupComplete(uint32_t closingKey, bool skipNextKeyRelease)
{
	// Set the flag in the Menu settings
	auto menu = Menu::GetSingleton();
	menu->GetSettings().FirstTimeSetupCompleted = true;
	menu->settingToggleKey = false;

	// Immediately save settings to ensure the flag is persisted
	// This prevents the welcome screen from showing again even if user doesn't manually save
	globals::state->Save();

	isFirstTimeSetupShown = true;  // Mark as shown this session
	keyThatClosedDialog = skipNextKeyRelease ? closingKey : 0;
}
