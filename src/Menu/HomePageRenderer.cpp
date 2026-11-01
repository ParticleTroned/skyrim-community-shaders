#include "HomePageRenderer.h"
#include "I18n/I18n.h"
#include "PCH.h"
#include "SettingsActions.h"
#include "SettingsPage.h"
#include "Utils/WinApi.h"

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
	MenuUI::SettingsPage page("Home", { { "welcome", T("menu.home.welcome", "Welcome"), T("menu.home.about_community_shaders_expanded_and_its_contributors", "About Community Shaders Expanded and its contributors."), T("menu.home.project_credits_and_community", "Project, credits and community"), true, true, T("menu.home.explore_your_installation", "Explore your installation") }, { "cache", T("menu.home.shader_cache", "Shader cache"), T("menu.home.review_changes_affecting_your_compiled_shaders", "Review changes affecting your compiled shaders."), T("menu.home.shader_cache_compatibility", "Shader cache compatibility"), true, true } }, T("menu.home.welcome", "Welcome"), T("menu.home.choose_a_page_to_get_started", "Choose a page to get started."));
	if (page.Is("welcome"))
		RenderWelcomeSection();
	if (page.Is("cache"))
		RenderCacheMismatchSection();
}

void HomePageRenderer::RenderModeSection()
{
	auto* menu = Menu::GetSingleton();
	if (!menu)
		return;
	auto& settings = menu->GetSettings();
	settings.PerformanceUiMode = std::clamp(settings.PerformanceUiMode, 0, 1);
	const MenuUI::Choice choices[]{
		{ "essentials", T("menu.home.essentials_recommended", "Essentials (Recommended)"),
			T("menu.home.essentials_description", "Focus on common visual and performance controls."),
			T("menu.home.essentials_help", "Keeps the SE Essentials grouping and simplified controls. Your saved settings remain available in Advanced mode.") },
		{ "advanced", T("menu.home.advanced_full_ui", "Advanced (Full UI)"),
			T("menu.home.advanced_description", "Show every feature and its detailed settings."),
			T("menu.home.advanced_help", "Includes diagnostics, developer tools and the full feature list. Switching mode does not change rendering settings.") }
	};
	const int selected = MenuUI::ChoiceCards("HomeInterfaceMode", settings.PerformanceUiMode, choices);
	if (selected >= 0)
		settings.PerformanceUiMode = selected;
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
									T("menu.home.this_is_an_unofficial_fork_of_community_shaders_restoring_particle_lights", "This is an unofficial fork of Community Shaders restoring Particle Lights."),
									T("menu.home.not_affiliated_with_or_endorsed_by_the_community_shaders_team", "Not affiliated with or endorsed by the Community Shaders team"),
									T("menu.home.visit_their_discord_to_get_the_original_and_support_their_outstanding_efforts", "- Visit their Discord to get the Original and support their outstanding efforts -"),
								},
		windowSize.x, forkNoticeColor);

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
			feedback.error = !Util::OpenInShell(DISCORD_URL, feedback.message);
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
			ImGui::Text(T("menu.home.open_mgo_discord", "Open MGO Discord"));
		}
	} else if (Util::ButtonWithFlash(T("menu.home.discord_homediscordbutton", "Discord##HomeDiscordButton"), discordButtonSize)) {
		feedback.error = !Util::OpenInShell(DISCORD_URL, feedback.message);
	}

	ImGui::SameLine(0.0f, linkButtonSpacing);
	if (Util::ButtonWithFlash(T("menu.home.github_homegithubbutton", "GitHub##HomeGitHubButton"), ImVec2(githubButtonWidth, linkButtonHeight))) {
		feedback.error = !Util::OpenInShell(GITHUB_URL, feedback.message);
	}

	ImGui::PopStyleVar();

	feedback.Draw();
}

void HomePageRenderer::RenderCacheMismatchSection()
{
	static MenuUI::ActionFeedback feedback;
	const auto showFeedback = SKSE::stl::scope_exit([&] { feedback.Draw(); });
	auto* shaderCache = globals::shaderCache;
	if (!shaderCache) {
		Util::Text::WrappedWarning(T("menu.home.shader_cache_status_is_not_available_yet", "Shader cache status is not available yet."));
		return;
	}
	if (!shaderCache->IsDiskCacheHeld() && !shaderCache->HasFeatureSetChanges() &&
		!shaderCache->HasFeatureSetRevertPending() && !shaderCache->HasPreviousDiskCache()) {
		MenuUI::DetailNote(T("menu.home.your_shader_cache_matches_the_current_feature_setup_no_cache_action_is_needed", "Your shader cache matches the current feature setup. No cache action is needed."));
		return;
	}

	auto* menu = Menu::GetSingleton();
	const char* actionBlockReason = MenuUI::GetGlobalSettingsActionBlockReason(MenuUI::GlobalSettingsAction::ClearShaderCache);
	if (actionBlockReason)
		MenuUI::DetailNote(actionBlockReason);
	const bool featureSetChanged = shaderCache->HasFeatureSetChanges();
	const bool revertPending = shaderCache->HasFeatureSetRevertPending();
	const bool featureSetCacheBackedUp = shaderCache->HasFeatureSetCacheBackup();
	const bool featureSetCacheSelectivelySeeded = shaderCache->HasSelectivelySeededFeatureSetCache();
	const bool previousCacheAvailable = shaderCache->HasPreviousDiskCache();
	const bool cacheHeld = shaderCache->IsDiskCacheHeld() && !featureSetChanged && !revertPending;
	const bool featureChangeHeld = shaderCache->IsDiskCacheHeld() && featureSetChanged && !featureSetCacheBackedUp;

	MenuUI::SectionHeading(T("menu.home.shader_cache_changes", "Shader Cache Changes"));

	if (revertPending) {
		const ImVec4 restartColor = menu ? menu->GetTheme().StatusPalette.RestartNeeded : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
		ImGui::TextColored(restartColor, "%s", T("menu.home.previous_cache_restored_restart_to_use_it", "Previous cache restored. Restart to use it."));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		return;
	}

	const char* summaryText = T("menu.home.a_previous_cache_is_available_restore_it_if_you_want_to_go_back_to_the_earlier_featur", "A previous cache is available. Restore it if you want to go back to the earlier feature setup.");
	const char* actionText = T("menu.home.to_go_back_restore_the_previous_cache_and_restart", "To go back, restore the previous cache and restart:");
	if (cacheHeld) {
		summaryText = T("menu.home.saved_shader_cache_cannot_be_used_because_a_required_feature_is_missing_or_failed_to_", "Saved shader cache cannot be used because a required feature is missing or failed to load.");
		actionText = T("menu.home.check_csx_feature_issues_if_present_fix_the_install_and_restart_to_use_the_saved_cach", "Check CSX > Feature Issues if present. Fix the install and restart to use the saved cache, or rebuild for this setup if the change was intentional:");
	} else if (featureChangeHeld) {
		summaryText = T("menu.home.feature_setup_changed_no_usable_previous_cache_could_be_kept_csx_is_compiling_this_se", "Feature setup changed; no usable previous cache could be kept. CSX is compiling this session's shaders and will rebuild the cache when finished.");
		actionText = T("menu.home.restore_unavailable_no_usable_previous_cache_was_kept_let_compilation_finish_to_rebui", "Restore unavailable: no usable previous cache was kept. Let compilation finish to rebuild for this setup.");
	} else if (featureSetChanged && featureSetCacheBackedUp) {
		if (!featureSetCacheSelectivelySeeded) {
			summaryText = T("menu.home.feature_setup_changed_the_complete_previous_cache_was_saved_but_individual_shader_fam", "Feature setup changed. The complete previous cache was saved, but individual shader families could not be retained safely. The active cache is rebuilding.");
			actionText = previousCacheAvailable ?
			                 T("menu.home.let_rebuilding_finish_the_previous_cache_can_then_be_restored", "Let rebuilding finish; the previous cache can then be restored.") :
			                 T("menu.home.let_compilation_finish_so_csx_can_verify_the_saved_previous_cache", "Let compilation finish so CSX can verify the saved previous cache.");
		} else if (previousCacheAvailable) {
			summaryText = T("menu.home.feature_setup_changed_unaffected_shaders_were_kept_and_the_complete_previous_cache_sa", "Feature setup changed. Unaffected shaders were kept and the complete previous cache saved. Only affected shaders are compiling. Restore becomes available when compilation finishes.");
		} else {
			summaryText = T("menu.home.feature_setup_changed_unaffected_shaders_were_kept_only_affected_shaders_are_compilin", "Feature setup changed. Unaffected shaders were kept; only affected shaders are compiling. Restore availability is checked after compilation finishes.");
			actionText = T("menu.home.let_compilation_finish_so_csx_can_verify_the_saved_previous_cache", "Let compilation finish so CSX can verify the saved previous cache.");
		}
	} else if (featureSetChanged) {
		summaryText = T("menu.home.feature_setup_changed_csx_is_building_a_new_cache_no_previous_cache_is_available_to_r", "Feature setup changed. CSX is building a new cache; no previous cache is available to restore.");
		actionText = T("menu.home.let_compilation_finish_to_rebuild_the_cache_for_the_current_setup", "Let compilation finish to rebuild the cache for the current setup.");
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
				             T("menu.home.enabled_now_but_missing_from_the_saved_cache", "enabled now, but missing from the saved cache") :
				             T("menu.home.in_the_saved_cache_but_missing_or_failed_now", "in the saved cache, but missing or failed now");
			} else {
				detail = mismatch.nowPresent ?
				             T("menu.home.enabled_now_previous_cache_had_it_disabled", "enabled now; previous cache had it disabled") :
				             T("menu.home.disabled_now_previous_cache_had_it_enabled", "disabled now; previous cache had it enabled");
			}
		}
		ImGui::BulletText("%s: %s", mismatch.feature.c_str(), detail);
	}
	ImGui::Spacing();

	ImGui::TextWrapped("%s", actionText);

	if (!cacheHeld && !featureChangeHeld) {
		const bool restoreDisabled = actionBlockReason || shaderCache->IsCompiling() || !previousCacheAvailable || (featureSetChanged && !featureSetCacheBackedUp);
		if (restoreDisabled)
			ImGui::BeginDisabled();

		const bool restoreClicked = ImGui::Button(T("menu.home.restore_previous_cache", "Restore Previous Cache"));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("menu.home.go_back_to_the_earlier_shader_setup_restart_required", "Go back to the earlier shader setup. Restart required."));
		}
		if (restoreClicked && !MenuUI::GetGlobalSettingsActionBlockReason(MenuUI::GlobalSettingsAction::ClearShaderCache)) {
			if (shaderCache->RestorePreviousDiskCache())
				feedback = {};
			else
				feedback = { T("menu.home.restore_cache_failed", "The previous shader cache could not be restored. Check the log for details."), true };
		}

		if (restoreDisabled)
			ImGui::EndDisabled();

		if (shaderCache->IsCompiling())
			ImGui::TextDisabled(T("menu.home.available_after_shader_compilation_finishes", "Available after shader compilation finishes."));
	}

	if (cacheHeld) {
		ImGui::BeginDisabled(actionBlockReason != nullptr);
		const bool rebuildClicked = ImGui::Button(T("menu.home.rebuild_cache_for_current_features", "Rebuild Cache for Current Features"));
		ImGui::EndDisabled();
		if (rebuildClicked && !MenuUI::GetGlobalSettingsActionBlockReason(MenuUI::GlobalSettingsAction::ClearShaderCache)) {
			shaderCache->AcceptCacheRebuild();
			feedback = {};
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("menu.home.keep_the_current_feature_setup_and_rebuild_shaders_for_it", "Keep the current feature setup and rebuild shaders for it."));
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
	// ImGui retains the window scale, so normalize it before laying out the dialog.
	ImGui::SetWindowFontScale(1.0f);

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
	if (!globals::state->Save()) {
		menu->GetSettings().FirstTimeSetupCompleted = false;
		return;
	}

	isFirstTimeSetupShown = true;
	keyThatClosedDialog = skipNextKeyRelease ? closingKey : 0;
}
