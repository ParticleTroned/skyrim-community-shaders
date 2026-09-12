#include "HomePageRenderer.h"
#include "PCH.h"

#include <imgui.h>

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

#include <algorithm>
#include <initializer_list>
#include <string>

namespace
{
	constexpr float FORK_NOTICE_OFFSET_LINES = 1.15f;
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

	bool BigRadioButton(const char* label, int* value, int buttonValue, float diameter)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 labelSize = ImGui::CalcTextSize(label);
		const float rowHeight = std::max(diameter, labelSize.y);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const ImVec2 size(diameter + style.ItemInnerSpacing.x + labelSize.x, rowHeight);
		const bool selected = *value == buttonValue;

		const bool pressed = ImGui::InvisibleButton(label, size);
		if (pressed)
			*value = buttonValue;

		const ImVec2 center(pos.x + diameter * 0.5f, pos.y + rowHeight * 0.5f);
		const float radius = diameter * 0.5f;
		const ImU32 bgColor = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
		const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
		const ImU32 checkColor = ImGui::GetColorU32(ImGuiCol_CheckMark);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddCircleFilled(center, radius, bgColor, 32);
		drawList->AddCircle(center, radius, borderColor, 32, std::max(1.0f, 2.0f * Util::GetUIScale()));
		if (selected)
			drawList->AddCircleFilled(center, radius * 0.62f, checkColor, 32);

		drawList->AddText(
			ImVec2(pos.x + diameter + style.ItemInnerSpacing.x, pos.y + (rowHeight - labelSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			label);

		return pressed;
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

	RenderCacheMismatchSection();

	// RenderQuickLinksSection();
	// ImGui::Spacing();

	// RenderFAQSection();

	ImGui::EndChild();
}

void HomePageRenderer::RenderModeSection()
{
	auto* menu = Menu::GetSingleton();
	if (!menu)
		return;

	auto& settings = menu->GetSettings();
	settings.UiMode = std::clamp(settings.UiMode, 0, 1);

	const char* modeLabel = "UI Mode";
	const char* essentialsLabel = "Essentials (Recommended)";
	const char* advancedLabel = "Advanced (Full UI)";
	const ImGuiStyle& style = ImGui::GetStyle();
	const float contentWidth = ImGui::GetContentRegionAvail().x;
	const float radioDiameter = ImGui::GetFrameHeight() * 1.5f;
	const ImVec2 modeLabelSize = ImGui::CalcTextSize(modeLabel);
	const ImVec2 essentialsLabelSize = ImGui::CalcTextSize(essentialsLabel);
	const ImVec2 advancedLabelSize = ImGui::CalcTextSize(advancedLabel);
	const float modeLabelWidth = modeLabelSize.x;
	const float essentialsWidth = radioDiameter + style.ItemInnerSpacing.x + essentialsLabelSize.x;
	const float advancedWidth = radioDiameter + style.ItemInnerSpacing.x + advancedLabelSize.x;
	const float spacing = style.ItemSpacing.x * 3.0f;
	const float rowWidth = modeLabelWidth + essentialsWidth + advancedWidth + spacing;
	const float rowHeight = std::max(radioDiameter, modeLabelSize.y);
	const float rowY = ImGui::GetCursorPosY();
	const float rowX = ImGui::GetCursorPosX() + std::max(0.0f, (contentWidth - rowWidth) * 0.5f);

	ImGui::SetCursorPos(ImVec2(rowX, rowY + (rowHeight - modeLabelSize.y) * 0.5f));
	ImGui::TextUnformatted(modeLabel);

	ImGui::PushID("HomeInterfaceMode");
	ImGui::SetCursorPos(ImVec2(rowX + modeLabelWidth + style.ItemSpacing.x, rowY));
	BigRadioButton(essentialsLabel, &settings.UiMode, 0, radioDiameter);
	ImGui::SetCursorPos(ImVec2(rowX + modeLabelWidth + style.ItemSpacing.x * 2.0f + essentialsWidth, rowY));
	BigRadioButton(advancedLabel, &settings.UiMode, 1, radioDiameter);
	ImGui::PopID();

	ImGui::SetCursorPosY(rowY + rowHeight);
}

void HomePageRenderer::RenderWelcomeSection()
{
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
	const char* forkTitle = Plugin::DISPLAY_NAME.data();
	const char* forkVersion = Plugin::FORK_VERSION.data();
	const float titleLineGap = 2.0f * scale;
	const float titleAreaBottomY = titleBlockY + titleBlockHeight + ImGui::GetStyle().ItemSpacing.y +
	                               baseLineHeightWithSpacing * FORK_NOTICE_OFFSET_LINES;

	ImGui::SetWindowFontScale(TITLE_PRIMARY_LINE_FONT_SCALE);
	const float titleLineHeight = ImGui::GetTextLineHeight();

	ImGui::SetWindowFontScale(TITLE_SECONDARY_LINE_FONT_SCALE);
	const float versionLineHeight = ImGui::GetTextLineHeight();

	const float titleGroupHeight = titleLineHeight + titleLineGap + versionLineHeight;
	const float titleGroupY = titleBlockY + std::max(0.0f, (titleAreaBottomY - titleBlockY - titleGroupHeight) * 0.5f);

	ImGui::SetWindowFontScale(TITLE_PRIMARY_LINE_FONT_SCALE);
	ImGui::SetCursorPosY(titleGroupY);
	DrawCenteredTextColored(forkTitle, windowSize.x, titleColor);

	ImGui::SetWindowFontScale(TITLE_SECONDARY_LINE_FONT_SCALE);
	ImGui::SetCursorPosY(titleGroupY + titleLineHeight + titleLineGap);
	DrawCenteredTextColored(forkVersion, windowSize.x, titleColor);

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
	RenderModeSection();
	ImGui::Spacing();

	// Vertical padding between intro text and the social artwork.
	ImGui::Dummy(ImVec2(0.0f, 25.0f * scale));

	// Faultier artwork - centered with proper error checking.
	bool faultierAvailable = false;
	if (menu && menu->uiIcons.faultier.texture != nullptr &&
		menu->uiIcons.faultier.size.x > 0 && menu->uiIcons.faultier.size.y > 0) {
		faultierAvailable = true;
	}

	if (faultierAvailable) {
		const ImVec2 originalSize(menu->uiIcons.faultier.size.x, menu->uiIcons.faultier.size.y);
		const float aspectRatio = originalSize.y / originalSize.x;
		const float maxAllowedWidth = std::max(1.0f, windowSize.x - HOME_LINK_ROW_PADDING_MARGIN * scale);
		const float upperWidth = std::min(FAULTIER_MAX_WIDTH * scale, maxAllowedWidth);
		const float lowerWidth = std::min(FAULTIER_MIN_WIDTH * scale, upperWidth);
		float targetWidth = std::clamp(windowSize.x * FAULTIER_TARGET_WIDTH_RATIO, lowerWidth, upperWidth);
		float targetHeight = targetWidth * aspectRatio;
		const float maxHeight = FAULTIER_MAX_HEIGHT * scale;
		if (targetHeight > maxHeight) {
			targetHeight = maxHeight;
			targetWidth = targetHeight / aspectRatio;
		}

		const ImVec2 imageSize(targetWidth, targetHeight);
		ImGui::SetCursorPosX(CenteredTextX(windowSize.x, imageSize.x));
		ImGui::Image(menu->uiIcons.faultier.texture, imageSize);
	} else {
		const float dummyWidth = FAULTIER_MAX_WIDTH * scale;
		const float dummyHeight = FAULTIER_MAX_HEIGHT * scale;
		ImGui::SetCursorPosX(CenteredTextX(windowSize.x, dummyWidth));
		ImGui::Dummy(ImVec2(dummyWidth, dummyHeight));
	}

	ImGui::Spacing();

	// Discord + GitHub actions - centered beneath Faultier.
	bool discordIconAvailable = false;
	if (menu && menu->uiIcons.discord.texture != nullptr &&
		menu->uiIcons.discord.size.x > 0 && menu->uiIcons.discord.size.y > 0) {
		discordIconAvailable = true;
	}

	const float linkButtonHeight = HOME_LINK_BUTTON_HEIGHT * scale;
	const float linkButtonSpacing = HOME_LINK_BUTTON_SPACING * scale;
	const float githubButtonWidth = HOME_LINK_GITHUB_BUTTON_WIDTH * scale;
	ImVec2 discordButtonSize(HOME_LINK_DISCORD_BUTTON_MIN_WIDTH * scale, linkButtonHeight);
	if (discordIconAvailable) {
		const ImVec2 originalSize(menu->uiIcons.discord.size.x, menu->uiIcons.discord.size.y);
		const float aspectRatio = originalSize.x / originalSize.y;
		discordButtonSize.x = std::max(discordButtonSize.x, linkButtonHeight * aspectRatio);
	}

	const float linkRowWidth = discordButtonSize.x + linkButtonSpacing + githubButtonWidth;
	ImGui::SetCursorPosX(CenteredTextX(windowSize.x, linkRowWidth));

	if (discordIconAvailable) {
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		[[maybe_unused]] auto discordButtonStyle = Util::TransparentIconButtonStyle();
		ImGui::PushID("HomeDiscordButton");
		const bool clicked = ImGui::InvisibleButton("##button", discordButtonSize);
		const bool hovered = ImGui::IsItemHovered();
		const bool hasActiveFlash = Util::IsButtonFlashActive("HomeDiscordButton");
		if (clicked) {
			ShellExecuteA(NULL, "open", DISCORD_URL, NULL, NULL, SW_SHOWNORMAL);
			Util::TriggerButtonFlash("HomeDiscordButton");
		}
		const ImVec2 buttonMin = ImGui::GetItemRectMin();
		const ImVec2 buttonMax = ImGui::GetItemRectMax();
		const ImVec2 originalSize(menu->uiIcons.discord.size.x, menu->uiIcons.discord.size.y);
		const float imageAspectRatio = originalSize.x / originalSize.y;
		ImVec2 imageSize(discordButtonSize.x, discordButtonSize.x / imageAspectRatio);
		if (imageSize.y > discordButtonSize.y) {
			imageSize.y = discordButtonSize.y;
			imageSize.x = imageSize.y * imageAspectRatio;
		}
		const ImVec2 imageMin(
			buttonMin.x + (discordButtonSize.x - imageSize.x) * 0.5f,
			buttonMin.y + (discordButtonSize.y - imageSize.y) * 0.5f);
		const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (hovered || hasActiveFlash) {
			ImVec4 feedbackColor = hasActiveFlash ?
			                           Util::GetButtonFlashColor(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)) :
			                           ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
			feedbackColor.w = hasActiveFlash ? 0.34f : 0.18f;
			drawList->AddRectFilled(buttonMin, buttonMax, ImGui::GetColorU32(feedbackColor), ImGui::GetStyle().FrameRounding);
		}
		drawList->AddImage(menu->uiIcons.discord.texture, imageMin, imageMax);
		ImGui::PopID();
		ImGui::PopStyleVar();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Open MGO Discord");
		}
	} else if (Util::ButtonWithFlash("Discord##HomeDiscordButton", discordButtonSize)) {
		ShellExecuteA(NULL, "open", DISCORD_URL, NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::SameLine(0.0f, linkButtonSpacing);
	if (Util::ButtonWithFlash("GitHub##HomeGitHubButton", ImVec2(githubButtonWidth, linkButtonHeight))) {
		ShellExecuteA(NULL, "open", GITHUB_URL, NULL, NULL, SW_SHOWNORMAL);
	}

	// Pop the style var we pushed at the start
	ImGui::PopStyleVar();
	// Close RenderWelcomeSection()
}

void HomePageRenderer::RenderCacheMismatchSection()
{
	auto* shaderCache = globals::shaderCache;
	if (!shaderCache || (!shaderCache->IsDiskCacheHeld() &&
							!shaderCache->HasFeatureSetChanges() &&
							!shaderCache->HasFeatureSetRevertPending() &&
							!shaderCache->HasPreviousDiskCache()))
		return;

	auto* menu = Menu::GetSingleton();
	const ImVec4 warningColor = menu ? menu->GetTheme().StatusPalette.Warning : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
	const bool featureSetChanged = shaderCache->HasFeatureSetChanges();
	const bool revertPending = shaderCache->HasFeatureSetRevertPending();
	const bool featureSetCacheBackedUp = shaderCache->HasFeatureSetCacheBackup();
	const bool featureSetCacheSelectivelySeeded = shaderCache->HasSelectivelySeededFeatureSetCache();
	const bool previousCacheAvailable = shaderCache->HasPreviousDiskCache();
	const bool cacheHeld = shaderCache->IsDiskCacheHeld() && !featureSetChanged && !revertPending;
	const bool featureChangeHeld = shaderCache->IsDiskCacheHeld() && featureSetChanged && !featureSetCacheBackedUp;

	ImGui::PushStyleColor(ImGuiCol_Text, warningColor);
	const bool headerOpen = ImGui::CollapsingHeader("Shader Cache Changes");
	ImGui::PopStyleColor();
	if (!headerOpen)
		return;

	if (revertPending) {
		const ImVec4 restartColor = menu ? menu->GetTheme().StatusPalette.RestartNeeded : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
		ImGui::TextColored(restartColor, "%s", "Previous cache restored. Restart to use it.");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		return;
	}

	const char* summaryText = "A previous cache is available. Restore it if you want to go back to the earlier feature setup.";
	const char* actionText = "To go back, restore the previous cache and restart:";
	if (cacheHeld) {
		summaryText = "Saved shader cache cannot be used because a required feature is missing or failed to load.";
		actionText = "Check CSX menu > Feature Issues if available. Fix the feature install and restart to use the saved cache, or rebuild the cache for the current setup if the change was intentional:";
	} else if (featureChangeHeld) {
		summaryText = "Your feature setup changed, but CSX could not keep a usable previous cache for restore. CSX is building shaders for this session and will rebuild the cache for the current setup when compilation finishes.";
		actionText = "Restore is unavailable because no usable previous cache was kept for this change. Let compilation finish to rebuild the cache for the current setup.";
	} else if (featureSetChanged && featureSetCacheBackedUp) {
		if (!featureSetCacheSelectivelySeeded) {
			summaryText = "Your feature setup changed. CSX saved the complete previous cache, but could not safely retain individual shader families, so it is rebuilding the active cache.";
			actionText = previousCacheAvailable ?
			                 "You can restore the previous cache after compilation finishes, or let CSX finish rebuilding for the current setup." :
			                 "Let compilation finish so CSX can verify the saved previous cache.";
		} else if (previousCacheAvailable) {
			summaryText = "Your feature setup changed. CSX retained unaffected shaders, saved the complete previous cache, and is compiling only affected shaders for the current setup. You can restore the previous cache after compilation finishes.";
		} else {
			summaryText = "Your feature setup changed. CSX retained unaffected shaders and is compiling only affected shaders for the current setup. Restore availability will be verified after compilation finishes.";
			actionText = "Let compilation finish so CSX can verify the saved previous cache.";
		}
	} else if (featureSetChanged) {
		summaryText = "Your feature setup changed. CSX is building a new cache for the current setup. Previous cache is not available for restore.";
		actionText = "Let compilation finish to rebuild the cache for the current setup.";
	}

	ImGui::TextWrapped("%s", summaryText);
	ImGui::Spacing();

	using MismatchKind = Util::CacheInvalidation::CacheMismatch::Kind;
	const auto& mismatches = (cacheHeld || featureChangeHeld) ? shaderCache->GetCacheMismatches() :
	                                                            shaderCache->GetPreviousCacheMismatches();
	for (const auto& mismatch : mismatches) {
		const char* detail = mismatch.detail.c_str();
		if (mismatch.kind == MismatchKind::EnabledFlip) {
			if (cacheHeld) {
				detail = mismatch.nowPresent ?
				             "enabled now, but missing from the saved cache" :
				             "in the saved cache, but missing or failed now";
			} else {
				detail = mismatch.nowPresent ?
				             "enabled now; previous cache had it disabled" :
				             "disabled now; previous cache had it enabled";
			}
		}
		ImGui::BulletText("%s: %s", mismatch.feature.c_str(), detail);
	}
	ImGui::Spacing();

	ImGui::TextWrapped("%s", actionText);

	if (!cacheHeld && !featureChangeHeld) {
		const bool restoreDisabled = shaderCache->IsCompiling() || !previousCacheAvailable || (featureSetChanged && !featureSetCacheBackedUp);
		if (restoreDisabled)
			ImGui::BeginDisabled();

		const bool restoreClicked = ImGui::Button("Restore Previous Cache");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Go back to the earlier shader setup. Restart required.");
		}
		if (restoreClicked)
			shaderCache->RestorePreviousDiskCache();

		if (restoreDisabled)
			ImGui::EndDisabled();

		if (shaderCache->IsCompiling())
			ImGui::TextDisabled("Available after shader compilation finishes.");
	}

	if (cacheHeld) {
		if (ImGui::Button("Rebuild Cache for Current Features")) {
			shaderCache->AcceptCacheRebuild();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keep the current feature setup and rebuild shaders for it.");
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
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

	// CS Editor hotkey status — updates live as user picks keys
	{
		auto& weatherKey = menu->GetSettings().CSEditorToggleKey;
		if (weatherKey.empty()) {
			const char* warnText = "CS Editor hotkey unbound \xe2\x80\x94 chosen key uses Shift";
			ImGui::SetCursorPosX(CenteredTextX(windowWidth, ImGui::CalcTextSize(warnText).x));
			Util::Text::Warning("%s", warnText);
		} else {
			std::string infoStr = "CS Editor hotkey will be: " + Util::Input::KeyIdToString(weatherKey);
			ImGui::SetCursorPosX(CenteredTextX(windowWidth, ImGui::CalcTextSize(infoStr.c_str()).x));
			ImGui::TextDisabled("%s", infoStr.c_str());
		}
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
