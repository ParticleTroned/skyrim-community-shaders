#include "HomePageRenderer.h"
#include "PCH.h"

#include <imgui.h>

#include <algorithm>
#include <initializer_list>
#include <string>

#include "Feature.h"
#include "FeatureConstraints.h"
#include "Globals.h"
#include "Menu.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UI.h"

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
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddCircleFilled(center, radius, bgColor, 32);
		drawList->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), 32, std::max(1.0f, 2.0f * Util::GetUIScale()));
		if (selected)
			drawList->AddCircleFilled(center, radius * 0.62f, ImGui::GetColorU32(ImGuiCol_CheckMark), 32);
		drawList->AddText(
			ImVec2(pos.x + diameter + style.ItemInnerSpacing.x, pos.y + (rowHeight - labelSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), label);
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
	settings.PerformanceUiMode = std::clamp(settings.PerformanceUiMode, 0, 1);
	const char* modeLabel = "Interface Mode";
	const char* essentialsLabel = "Essentials (Recommended)";
	const char* advancedLabel = "Advanced (Full UI)";
	const ImGuiStyle& style = ImGui::GetStyle();
	const float contentWidth = ImGui::GetContentRegionAvail().x;
	const float radioDiameter = ImGui::GetFrameHeight() * 2.0f;
	const ImVec2 modeLabelSize = ImGui::CalcTextSize(modeLabel);
	const float essentialsWidth = radioDiameter + style.ItemInnerSpacing.x + ImGui::CalcTextSize(essentialsLabel).x;
	const float advancedWidth = radioDiameter + style.ItemInnerSpacing.x + ImGui::CalcTextSize(advancedLabel).x;
	const float rowWidth = modeLabelSize.x + essentialsWidth + advancedWidth + style.ItemSpacing.x * 3.0f;
	const float rowHeight = std::max(radioDiameter, modeLabelSize.y);
	const float rowY = ImGui::GetCursorPosY();
	const float rowX = ImGui::GetCursorPosX() + std::max(0.0f, (contentWidth - rowWidth) * 0.5f);

	ImGui::SetCursorPos(ImVec2(rowX, rowY + (rowHeight - modeLabelSize.y) * 0.5f));
	ImGui::TextUnformatted(modeLabel);
	ImGui::PushID("HomeInterfaceMode");
	ImGui::SetCursorPos(ImVec2(rowX + modeLabelSize.x + style.ItemSpacing.x, rowY));
	BigRadioButton(essentialsLabel, &settings.PerformanceUiMode, 0, radioDiameter);
	ImGui::SetCursorPos(ImVec2(rowX + modeLabelSize.x + style.ItemSpacing.x * 2.0f + essentialsWidth, rowY));
	BigRadioButton(advancedLabel, &settings.PerformanceUiMode, 1, radioDiameter);
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

	DrawCenteredItalicTextBlock({
		"This is an unofficial fork of Community Shaders restoring Particle Lights.",
		"Not affiliated with or endorsed by the Community Shaders team",
		"- Visit their Discord to get the Original and support their outstanding efforts -",
	}, windowSize.x, forkNoticeColor);

	ImGui::Spacing();
	RenderModeSection();
	ImGui::Spacing();
	ImGui::Dummy(ImVec2(0.0f, 25.0f * scale));

	// Faultier artwork - centered with proper error checking
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

	// Discord + GitHub actions - centered beneath Faultier
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
		discordButtonSize.x = std::clamp(
			linkButtonHeight * aspectRatio,
			HOME_LINK_DISCORD_BUTTON_MIN_WIDTH * scale,
			HOME_LINK_DISCORD_BUTTON_MAX_WIDTH * scale);
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

	ImGui::PopStyleVar();
}

void HomePageRenderer::RenderCacheMismatchSection()
{
	auto* shaderCache = globals::shaderCache;
	if (!shaderCache || (!shaderCache->IsDiskCacheHeld() &&
	                        !shaderCache->HasFeatureSetChanges() &&
	                        !shaderCache->HasFeatureSetRevertPending() &&
	                        !shaderCache->HasPreviousDiskCache())) {
		return;
	}

	auto menu = Menu::GetSingleton();
	const ImVec4 warningColor = menu ? menu->GetTheme().StatusPalette.Warning : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
	const bool featureSetChanged = shaderCache->HasFeatureSetChanges();
	const bool revertPending = shaderCache->HasFeatureSetRevertPending();
	const bool featureSetCacheBackedUp = shaderCache->HasFeatureSetCacheBackup();
	const bool previousCacheAvailable = shaderCache->HasPreviousDiskCache();
	const bool cacheHeld = shaderCache->IsDiskCacheHeld() && !featureSetChanged && !revertPending;
	const bool featureChangeHeld = shaderCache->IsDiskCacheHeld() && featureSetChanged && !featureSetCacheBackedUp;

	ImGui::PushStyleColor(ImGuiCol_Text, warningColor);
	const bool headerOpen = ImGui::CollapsingHeader("Shader Cache Changes", ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::PopStyleColor();
	if (!headerOpen) {
		return;
	}

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
		actionText = "Check CS menu > Feature Issues if available. Fix the feature install and restart to use the saved cache, or rebuild the cache for the current setup if the change was intentional:";
	} else if (featureChangeHeld) {
		summaryText = "Your feature setup changed, but CS could not keep a usable previous cache for restore. CS is building shaders for this session and will rebuild the cache for the current setup when compilation finishes.";
		actionText = "Restore is unavailable because no usable previous cache was kept for this change. Let compilation finish to rebuild the cache for the current setup.";
	} else if (featureSetChanged && previousCacheAvailable) {
		summaryText = "Your feature setup changed. CS saved the previous cache and is building a new cache for the current setup. You can restore the previous cache after compilation finishes.";
	} else if (featureSetChanged) {
		summaryText = "Your feature setup changed. CS is building a new cache for the current setup. Previous cache is not available for restore.";
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
			ImGui::TextUnformatted("Restores the previous cache and resets feature toggles to match it. Restart required.");
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
			ImGui::TextUnformatted("Rebuilds the disk cache for the current feature setup.");
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

void HomePageRenderer::RenderQuickLinksSection()
{
	// Quick Links title - centered
	ImVec2 windowSize = ImGui::GetWindowSize();
	ImVec2 titleSize = ImGui::CalcTextSize("Quick Links");
	ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
	ImGui::Text("Quick Links");

	ImGui::Columns(4, nullptr, false);

	// External links in a row
	if (ImGui::Button("Nexus Mods", ImVec2(-1, 0))) {
		ShellExecuteA(NULL, "open", "https://www.nexusmods.com/skyrimspecialedition/mods/86492", NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::NextColumn();
	if (ImGui::Button("GitHub", ImVec2(-1, 0))) {
		ShellExecuteA(NULL, "open", GITHUB_URL, NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::NextColumn();
	if (ImGui::Button("Wiki", ImVec2(-1, 0))) {
		ShellExecuteA(NULL, "open", "https://modding.wiki/en/skyrim/developers/community-shaders", NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::NextColumn();
	if (ImGui::Button("Developer Wiki", ImVec2(-1, 0))) {
		ShellExecuteA(NULL, "open", "https://github.com/doodlum/skyrim-community-shaders/wiki", NULL, NULL, SW_SHOWNORMAL);
	}

	ImGui::Columns(1);
}

void HomePageRenderer::RenderFAQSection()
{
	// FAQ title - centered
	ImVec2 windowSize = ImGui::GetWindowSize();
	ImVec2 titleSize = ImGui::CalcTextSize("Frequently Asked Questions");
	ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
	ImGui::Text("Frequently Asked Questions");
	ImGui::Separator();

	// FAQ items with collapsible headers
	if (ImGui::CollapsingHeader("What is Community Shaders?")) {
		ImGui::TextWrapped(
			"Community Shaders is a comprehensive graphics enhancement framework for Skyrim that "
			"provides advanced lighting, materials, and visual effects. It's designed to be modular, "
			"allowing you to enable only the features you want while maintaining good performance.");
	}

	if (ImGui::CollapsingHeader("How do I configure features?")) {
		ImGui::TextWrapped(
			"Each feature can be found in the left sidebar menu. Click on any feature to access its "
			"settings. Most features include presets and detailed tooltips to help you understand "
			"what each setting does.");
	}

	if (ImGui::CollapsingHeader("Why are some features not loading?")) {
		ImGui::TextWrapped(
			"Features may fail to load due to hardware incompatibility, missing dependencies, or "
			"conflicts with other mods. Check the 'Feature Issues' tab for detailed information "
			"about any problematic features.");
	}

	if (ImGui::CollapsingHeader("I have \"Failed Shaders\" when compiling?")) {
		ImGui::TextWrapped(
			"Failed shaders are usually caused by mixed file versions. Ensure all features are up to date "
			"and avoid mixing files from test builds or outdated versions. Please review the 'Feature Issues' tab "
			"and/or Wiki for more information. Update your features and remove any obsolete features.");
	}

	if (ImGui::CollapsingHeader("How do I improve performance?")) {
		ImGui::TextWrapped(
			"Start by enabling the Performance Overlay to monitor your FPS. Consider disabling "
			"expensive features like Screen Space GI or reducing quality settings. The 'Display' "
			"tab also includes upscaling options that can improve performance.");
	}

	if (ImGui::CollapsingHeader("Is Community Shaders compatible with ENB?")) {
		ImGui::TextWrapped(
			"No, Community Shaders is not compatible with ENB. Community Shaders will automatically "
			"disable itself if ENB is detected.");
	}

	if (ImGui::CollapsingHeader("The menu hotkey isn't working!")) {
		ImGui::TextWrapped(
			"By default, Community Shaders uses the END key to open this menu. If your keyboard "
			"doesn't have an END key or it's not working, you can change it in the General > Keybindings tab. "
			"You can also edit the hotkey in the JSON configuration files.");
	}

	if (ImGui::CollapsingHeader("I would like to help develop Community Shaders.")) {
		ImGui::TextWrapped(
			"We're always looking for talented developers to join the team! Check out our GitHub wiki "
			"for contribution guidelines and join our Discord server to connect with the development team. "
			"Whether you're interested in shader programming, C++ development, or documentation, there's "
			"always something to contribute.");
	}

	if (ImGui::CollapsingHeader("Is Community Shaders open source?")) {
		ImGui::TextWrapped(
			"Yes! Community Shaders is completely open source and available on GitHub. You can view "
			"the source code, report issues, suggest features, and contribute to the project. "
			"The project is licensed under GPL, ensuring it remains free and open for everyone."
			" Branding materials and assets (icons, nexus branding, typography, etc) are not covered by the GPL Licence."
			" Any included assets may not be used without explicit permission.");
	}
}

void HomePageRenderer::RenderActiveConstraintsSection()
{
	auto constraints = FeatureConstraints::GetAllActiveConstraints();
	if (constraints.empty()) {
		return;  // Don't show section if there are no active constraints
	}

	ImGui::Spacing();

	// Use warning color for the header to draw attention
	ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
	bool headerOpen = ImGui::CollapsingHeader(T("menu.home.active_constraints", "Active Setting Constraints"), ImGuiTreeNodeFlags_None);
	ImGui::PopStyleColor();

	if (headerOpen) {
		ImGui::TextWrapped(
			"Some settings are constrained by other features. Hover over rows for details.");

		ImGui::Spacing();

		// Prepare data for table
		struct ConstraintRow
		{
			std::string setting;
			std::string forcedTo;
			std::string constrainedBy;
			std::string firstSourceShortName;  // For "navigate to feature" on click
			std::string tooltip;
		};

		std::vector<ConstraintRow> rows;
		for (const auto& [settingId, result] : constraints) {
			ConstraintRow row;
			row.setting = std::format("{}.{}", settingId.featureShortName, settingId.settingPath);
			row.forcedTo = FeatureConstraints::FormatConstraintValue(result.forcedValue);
			for (size_t i = 0; i < result.sources.size(); ++i) {
				if (i > 0)
					row.constrainedBy += ", ";
				row.constrainedBy += result.sources[i].featureName;
			}
			if (!result.sources.empty()) {
				row.firstSourceShortName = result.sources[0].featureShortName;
			}
			// Build tooltip
			for (const auto& src : result.sources) {
				if (!row.tooltip.empty())
					row.tooltip += "\n";
				row.tooltip += std::format("{}: {}", src.featureName, src.reason);
				if (src.recommendDisableAtBoot) {
					row.tooltip += "\nConsider disabling at boot.";
				}
			}
			rows.push_back(row);
		}

		// Define headers
		std::vector<std::string> headers = { "Setting", "Forced To", "Constrained By" };

		// Custom sorts (string comparators for each column)
		std::vector<std::function<bool(const ConstraintRow&, const ConstraintRow&, bool)>> customSorts = {
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.setting, b.setting, asc); },
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.forcedTo, b.forcedTo, asc); },
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.constrainedBy, b.constrainedBy, asc); }
		};

		// Cell render -- column 2 ("Constrained By") is clickable to navigate
		// to the first source feature's settings page.
		auto cellRender = [](int rowIdx, int colIdx, const ConstraintRow& row) {
			if (colIdx == 0) {
				Util::RenderTableCell(row.setting, "", "", nullptr, ImVec4(1, 1, 1, 1), true, Util::Colors::GetWarning());
			} else if (colIdx == 1) {
				Util::RenderTableCell(row.forcedTo, "", "", nullptr, ImVec4(1, 1, 1, 1), true);
			} else if (colIdx == 2) {
				if (!row.firstSourceShortName.empty()) {
					if (ImGui::Selectable(std::format("{}##nav{}", row.constrainedBy, rowIdx).c_str())) {
						if (auto* menu = Menu::GetSingleton()) {
							menu->SelectFeatureMenu(row.firstSourceShortName);
						}
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Click to navigate to %s", row.constrainedBy.c_str());
						if (!row.tooltip.empty()) {
							ImGui::Separator();
							ImGui::Text("%s", row.tooltip.c_str());
						}
					}
				} else {
					Util::RenderTableCell(row.constrainedBy, "", row.tooltip, nullptr, ImVec4(1, 1, 1, 1), true);
				}
			}
		};

		// Render table
		Util::ShowSortedStringTableCustom<ConstraintRow>(
			"ConstraintsTable",
			headers,
			rows,
			0,     // sortColumn
			true,  // ascending
			customSorts,
			cellRender);
	}

	ImGui::Spacing();
}

void HomePageRenderer::RenderFirstTimeSetupDialog()
{
	if (!ShouldShowFirstTimeSetup()) {
		return;
	}

	// Block input to the game and make cursor visible - input blocking is handled by ShouldSwallowInput()
	auto& io = ImGui::GetIO();
	io.WantCaptureMouse = true;
	io.WantCaptureKeyboard = true;
	io.MouseDrawCursor = true;  // Show ImGui cursor

	float uiScale = Util::GetUIScale();
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(DIALOG_MIN_WIDTH * uiScale, 0), ImVec2(DIALOG_MAX_WIDTH * uiScale, FLT_MAX));
	ImGui::SetNextWindowFocus();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, DIALOG_CORNER_ROUNDING * uiScale);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;

	if (!ImGui::Begin("##FirstTimeSetup", nullptr, flags)) {
		ImGui::PopStyleVar();
		ImGui::End();
		return;
	}

	// Fullscreen fade on the dialog's draw list — covers all windows beneath at the dialog's z-position
	auto* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRectFullScreen();
	drawList->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, MODAL_OVERLAY_ALPHA));
	drawList->PopClipRect();

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
	auto centerText = [windowWidth](const char* text) {
		ImGui::SetCursorPosX((windowWidth - ImGui::CalcTextSize(text).x) * 0.5f);
	};
	auto centerWidth = [windowWidth](float width) {
		ImGui::SetCursorPosX((windowWidth - width) * 0.5f);
	};

	// Version text - two lines, both centered (reduced spacing between lines)
	const char* versionLine1 = "This appears to be a new install, update, or";
	const std::string versionLine2 = "reinstallation of " + std::string(Plugin::DISPLAY_NAME) + ".";

	centerText(versionLine1);
	ImGui::Text("%s", versionLine1);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - DIALOG_LINE_TIGHTEN * uiScale);
	centerText(versionLine2.c_str());
	ImGui::Text("%s", versionLine2.c_str());

	ImGui::Spacing();

	// Description - centered
	const char* description = "Please choose a hotkey to access the menu:";
	centerText(description);
	ImGui::Text("%s", description);

	// Hotkey selection - clickable hotkey text
	// Show current toggle key and allow user to change it by clicking on it
	auto& themeSettings = menu->GetTheme();
	bool isCapturing = menu->settingToggleKey;

	// Increase font size for hotkey text - bigger when capturing
	ImGui::SetWindowFontScale(isCapturing ? HOTKEY_TEXT_SCALE_CAPTURING : HOTKEY_TEXT_SCALE);

	// Format hotkey with brackets to make it look like a button
	std::string hotkeyStr;
	if (isCapturing) {
		hotkeyStr = "[ ... ]";
	} else {
		auto& keys = menu->GetSettings().ToggleKey;
		hotkeyStr = std::string("[ ") + Util::Input::KeyIdToString(keys) + " ]";
	}

	ImVec2 hotkeyTextSize = ImGui::CalcTextSize(hotkeyStr.c_str());

	centerWidth(hotkeyTextSize.x);
	ImVec2 buttonPos = ImGui::GetCursorScreenPos();

	// Create invisible button for hover detection and clicking
	ImGui::PushID("HotkeyButton");
	bool clicked = ImGui::InvisibleButton("##HotkeyClick", hotkeyTextSize);
	bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();

	// Set cursor position back for text rendering
	ImGui::SetCursorScreenPos(buttonPos);

	// Choose color based on state
	ImVec4 hotkeyColor;
	if (isCapturing) {
		// Pulsing effect using theme's hotkey color
		hotkeyColor = Util::GetPulsingColor(themeSettings.StatusPalette.CurrentHotkey);
	} else if (hovered) {
		hotkeyColor = ImVec4(themeSettings.StatusPalette.CurrentHotkey.x * HOTKEY_HOVER_DIM_FACTOR,
			themeSettings.StatusPalette.CurrentHotkey.y * HOTKEY_HOVER_DIM_FACTOR,
			themeSettings.StatusPalette.CurrentHotkey.z * HOTKEY_HOVER_DIM_FACTOR,
			themeSettings.StatusPalette.CurrentHotkey.w);
	} else {
		hotkeyColor = themeSettings.StatusPalette.CurrentHotkey;
	}

	ImGui::TextColored(hotkeyColor, "%s", hotkeyStr.c_str());

	ImGui::SetWindowFontScale(1.0f);

	// Handle click to start hotkey capture
	if (clicked && !isCapturing) {
		// Prevent starting capture if this click was caused by Enter key,
		// because we want Enter to close the dialog instead.
		if (!ImGui::IsKeyPressed(ImGuiKey_Enter))
			menu->settingToggleKey = true;
	}

	// Show hotkey capture message when in capture mode
	if (isCapturing) {
		const char* pressKeyText = "Press any key to set as toggle key...";
		centerText(pressKeyText);
		ImGui::TextDisabled("%s", pressKeyText);
	}

	// CS Editor hotkey status — updates live as user picks keys
	{
		auto& weatherKey = menu->GetSettings().CSEditorToggleKey;
		if (weatherKey.empty()) {
			const char* warnText = "CS Editor hotkey unbound \xe2\x80\x94 chosen key uses Shift";
			centerText(warnText);
			Util::Text::Warning("%s", warnText);
		} else {
			std::string infoStr = "CS Editor hotkey will be: " + Util::Input::KeyIdToString(weatherKey);
			centerText(infoStr.c_str());
			ImGui::TextDisabled("%s", infoStr.c_str());
		}
	}

	ImGui::Spacing();

	const char* laterText = "You can change this later in General > Keybindings.";
	centerText(laterText);
	ImGui::Text("%s", laterText);

	ImGui::Spacing();

	// Check for Enter or Escape key to close, but only if not capturing a hotkey
	bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || escapePressed) && !isCapturing) {
		MarkFirstTimeSetupComplete(escapePressed ? VK_ESCAPE : VK_RETURN);
	}

	// Help text with breathing animation
	const char* helpText = "Press Escape or Enter to continue";

	ImGui::SetWindowFontScale(HELP_TEXT_SCALE);
	centerText(helpText);
	Util::DrawBreathingText(helpText);

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
	ImGui::PopStyleVar();
}

bool HomePageRenderer::ShouldShowFirstTimeSetup()
{
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
	// Ensure we are not capturing a hotkey when closing the dialog
	menu->settingToggleKey = false;

	// Immediately save settings to ensure the flag is persisted
	// This prevents the welcome screen from showing again even if user doesn't manually save
	globals::state->Save();

	isFirstTimeSetupShown = true;
	keyThatClosedDialog = skipNextKeyRelease ? closingKey : 0;
}
