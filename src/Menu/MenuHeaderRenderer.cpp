#include "MenuHeaderRenderer.h"
#include "FeatureListRenderer.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "ThemeManager.h"
#include "Util.h"
#include <algorithm>
#include <cfloat>
#include <imgui_internal.h>

namespace
{
	constexpr float kHeaderActionIconScale = 0.75f;
	const char* ShaderFailureTooltip()
	{
		return T("menu.header.shader_failure_help", "Show/hide the shader failure message. Errors may appear in game. Check feature versions and load order, then CSX.log. Use the Nexus Mods page or Discord for help.");
	}
	struct HeaderContentAlignment
	{
		ImGuiID window = 0;
		int frame = -1;
		float right = 0;
	};
	HeaderContentAlignment headerContentAlignment;

	float GetHeaderIconSize()
	{
		return ImGui::GetFontSize() * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER;
	}

	float GetHeaderActionIconSize()
	{
		return GetHeaderIconSize() * 1.5f * kHeaderActionIconScale;
	}

	float GetDockedActionIconSize(float a_titleBarHeight)
	{
		const float desiredSize = ImGui::GetFontSize() * ThemeManager::Constants::DOCKED_ICON_SIZE_MULTIPLIER * 1.5f;
		const float maxSize = std::max(1.0f, a_titleBarHeight - 2.0f * std::max(1.0f, ImGui::GetStyle().FramePadding.y * 0.35f));
		return std::min(desiredSize, maxSize);
	}
}

void MenuHeaderRenderer::RenderHeader(
	bool isDocked,
	bool showLogo,
	bool canShowIcons,
	float uiScale,
	const Menu::UIIcons& uiIcons)
{
	if (!globals::menu) {
		logger::error("MenuHeaderRenderer::RenderHeader: globals::menu is null, cannot render header");
		return;
	}

	std::string title{ Plugin::MENU_TITLE };
	auto actionIcons = BuildActionIcons(canShowIcons, uiIcons);
	if (!isDocked)
		actionIcons.push_back({ "HeaderClose", nullptr, nullptr, T("menu.header.close", "Close menu"), [] { globals::menu->CloseMenu(); }, ActionIcon::Glyph::Close });

	if (isDocked) {
		// Draw action icons in the title bar area
		RenderDockedIcons(actionIcons, uiScale);
	} else {
		RenderStableHeader(title, showLogo, actionIcons, uiScale, uiIcons);
	}

	// Add separators - no separator needed for docked mode since icons are in title bar
	const bool renderedInlineHeader = !isDocked;
	if (renderedInlineHeader) {
		// First separator - always shown when not docked
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}

	// If icons are disabled or missing, show action buttons as text between separators (only when not docked)
	auto shaderCache = globals::shaderCache;
	if (!canShowIcons && renderedInlineHeader) {
		if (ImGui::BeginTable("##ActionButtons", 4, ImGuiTableFlags_SizingStretchSame)) {
			// Save Settings Button
			ImGui::TableNextColumn();
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::Save, T("menu.save_settings", "Save Settings"), { -1, 0 });

			// Restore Saved Settings Button
			ImGui::TableNextColumn();
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::Load, T("menu.restore_settings", "Restore Saved Settings"), { -1, 0 });

			ImGui::TableNextColumn();
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::RestoreDefaults, T("menu.actions.restore_defaults", "Restore Defaults"), { -1, 0 });

			// Clear Shader Cache Button
			ImGui::TableNextColumn();
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::ClearShaderCache, T("menu.clear_shader_cache", "Clear Shader Cache"), { -1, 0 });

			// Error message toggle if needed
			if (shaderCache->GetFailedTasks()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Button(T("menu.toggle_error_message", "Toggle Error Message"), { -1, 0 })) {
					shaderCache->ToggleErrorMessages();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted(ShaderFailureTooltip());
				}
			}

			ImGui::EndTable();
		}

		// Second separator - only shown if icons are disabled/missing or if there are failed tasks (and not docked)
		if (renderedInlineHeader) {
			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
			ImGui::Spacing();
		}
	} else if (shaderCache->GetFailedTasks() && renderedInlineHeader) {
		// If icons are enabled but there are failed tasks, show error toggle button
		// and add the second separator (only when not docked)
		if (ImGui::Button(T("menu.toggle_error_message", "Toggle Error Message"), { -1, 0 })) {
			shaderCache->ToggleErrorMessages();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(ShaderFailureTooltip());
		}

		// Add second separator when showing error button
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}
}

std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons)
{
	using Action = MenuUI::GlobalSettingsAction;
	using Glyph = ActionIcon::Glyph;
	std::vector<ActionIcon> icons{
		{ "HeaderSidebar", nullptr, nullptr,
			FeatureListRenderer::IsSidebarVisible() ? T("menu.header.hide_feature_list", "Hide feature list") : T("menu.header.show_feature_list", "Show feature list"),
			[] { FeatureListRenderer::SetSidebarVisible(!FeatureListRenderer::IsSidebarVisible()); }, Glyph::Sidebar }
	};
	if (!canShowIcons)
		return icons;
	const auto append = [&](const char* id, ID3D11ShaderResourceView* texture, Glyph glyph, Action action) {
		icons.push_back({ id, id, texture, MenuUI::GetGlobalSettingsActionHelp(action),
			[action] { MenuUI::InvokeGlobalSettingsAction(action); }, glyph, action });
	};
	append("HeaderSaveSettings", uiIcons.saveSettings.texture, Glyph::SaveSettings, Action::Save);
	append("HeaderRestoreSavedSettings", uiIcons.loadSettings.texture, Glyph::LoadSettings, Action::Load);
	append("HeaderRestoreDefaults", uiIcons.featureSettingRevert.texture, Glyph::RestoreDefaults, Action::RestoreDefaults);
	append("HeaderClearShaderCache", uiIcons.clearCache.texture, Glyph::ClearCache, Action::ClearShaderCache);
	return icons;
}

void MenuHeaderRenderer::DrawActionIcon(ImDrawList* draw, const ActionIcon& icon, ImVec2 minimum, ImVec2 maximum, ImU32 tint)
{
	if (icon.glyph == ActionIcon::Glyph::Texture) {
		if (icon.texture)
			draw->AddImage(icon.texture, minimum, maximum, { 0, 0 }, { 1, 1 }, tint);
		return;
	}
	const int vertexStart = draw->VtxBuffer.Size;
	Util::DrawActionGlyph(draw, icon.glyph, minimum, maximum,
		ImGui::GetColorU32(Util::Color::SecondaryText()), FeatureListRenderer::IsSidebarVisible());
	if (icon.glyph == ActionIcon::Glyph::Close) {
		float right = minimum.x;
		for (int vertex = vertexStart; vertex < draw->VtxBuffer.Size; ++vertex)
			right = std::max(right, draw->VtxBuffer[vertex].pos.x);
		headerContentAlignment = { ImGui::GetCurrentWindow()->ID, ImGui::GetFrameCount(), right };
	}
}

void MenuHeaderRenderer::DrawActionButton(const ActionIcon& icon, ImVec2 minimum, ImVec2 maximum, float iconInset)
{
	auto* window = ImGui::GetCurrentWindow();
	if (window->SkipItems || (!icon.texture && icon.glyph == ActionIcon::Glyph::Texture))
		return;
	const auto* reason = MenuUI::GetGlobalSettingsActionBlockReason(icon.action);
	const auto disabled = Util::DisableGuard(reason != nullptr);
	auto& context = *ImGui::GetCurrentContext();
	auto* hoveredWindow = context.HoveredWindow;
	// A docked title bar belongs to its host; popup and active-item checks still apply.
	if (window->DockNode && window->DockTabIsVisible && hoveredWindow == window->DockNode->HostWindow)
		context.HoveredWindow = window;
	const SKSE::stl::scope_exit restoreHovered([&] { context.HoveredWindow = hoveredWindow; });
	const ImRect bounds(minimum, maximum);
	const auto id = window->GetID(icon.id);
	if (!ImGui::ItemAdd(bounds, id))
		return;
	bool hovered = false, held = false;
	const bool clicked = ImGui::ButtonBehavior(bounds, id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
	auto* draw = window->DrawList;
	const bool flash = icon.flashId && Util::IsButtonFlashActive(icon.flashId);
	if (hovered || held || flash) {
		auto color = ImGui::GetStyleColorVec4(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
		if (flash)
			color = Util::GetButtonFlashColor(color);
		color.w *= held || flash ? .34f : .18f;
		draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(color));
	}
	ImVec4 tint = globals::menu->GetTheme().UseMonochromeIcons ?
	                  globals::menu->GetTheme().Palette.Text :
	                  ImVec4(1, 1, 1, 1);
	if (!hovered && !held && !flash)
		tint.w *= .85f;
	DrawActionIcon(draw, icon, { minimum.x + iconInset, minimum.y + iconInset },
		{ maximum.x - iconInset, maximum.y - iconInset }, ImGui::GetColorU32(tint));
	ImGui::RenderNavCursor(bounds, id);
	if (clicked) {
		icon.callback();
		if (icon.flashId)
			Util::TriggerButtonFlash(icon.flashId);
	}
	const auto* help = icon.action == MenuUI::GlobalSettingsAction::None ? icon.tooltip : MenuUI::GetGlobalSettingsActionHelp(icon.action);
	Util::AddTooltip(reason ? reason : help, ImGuiHoveredFlags_AllowWhenDisabled);
}

void MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	auto* window = ImGui::GetCurrentWindow();
	const auto* node = window->DockNode;
	if (actionIcons.empty() || window->SkipItems || !window->DockTabIsVisible || !node || !node->TabBar || node->IsHiddenTabBar() || node->IsNoTabBar())
		return;
	const auto titleBar = node->TabBar->BarRect;
	const float titleBarHeight = titleBar.GetHeight();
	const float iconSize = GetDockedActionIconSize(titleBarHeight);
	const float spacing = ThemeManager::Constants::DOCKED_ICON_SPACING * uiScale;
	float x = titleBar.Max.x - ThemeManager::Constants::DOCKED_RIGHT_MARGIN * uiScale;
	const float y = titleBar.Min.y + (titleBarHeight - iconSize) * .5f;
	ImGui::PushClipRect(titleBar.Min, titleBar.Max, false);
	const SKSE::stl::scope_exit restoreClip([] { ImGui::PopClipRect(); });
	for (auto it = actionIcons.rbegin(); it != actionIcons.rend(); ++it) {
		x -= iconSize + spacing;
		DrawActionButton(*it, { x, y }, { x + iconSize, y + iconSize });
	}
}

float MenuHeaderRenderer::GetFeaturePanelWidth()
{
	const float available = ImGui::GetContentRegionAvail().x;
	if (headerContentAlignment.frame != ImGui::GetFrameCount())
		return available;
	auto* window = ImGui::GetCurrentWindow();
	while (window && window->ID != headerContentAlignment.window)
		window = window->ParentWindow;
	if (!window)
		return available;
	const auto& style = ImGui::GetStyle();
	const float rightPadding = style.ChildBorderSize > 0 ? style.WindowPadding.x : 0;
	return std::clamp(headerContentAlignment.right + rightPadding - ImGui::GetCursorScreenPos().x, 1.0f, std::max(1.0f, available));
}

void MenuHeaderRenderer::RenderStableHeader(const std::string& title, bool showLogo, const std::vector<ActionIcon>& actionIcons, float uiScale, const Menu::UIIcons& uiIcons)
{
	auto* menu = globals::menu;
	if (!menu)
		return;

	ImGuiStyle& style = ImGui::GetStyle();
	const float currentFontSize = ImGui::GetFontSize();
	const float actionIconSize = GetHeaderActionIconSize();
	const float textScaleFactor = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE;
	const float paddingX = currentFontSize;
	const float paddingY = style.FramePadding.y * 2.0f;
	const float iconSpacing = ThemeManager::Constants::UNDOCKED_ICON_ITEM_SPACING * uiScale;
	const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * uiScale;

	ImFont* titleFont = menu->GetFont(Menu::FontRole::Body);
	if (!titleFont) {
		titleFont = ImGui::GetFont();
	}
	const float titleFontSize = currentFontSize * textScaleFactor * 1.3f;
	const std::string_view brand = "CSX";
	const std::string version = title.starts_with("CSX ") ? title.substr(4) : title;
	const float versionSize = currentFontSize * 1.25f;
	const ImVec2 brandSize = titleFont->CalcTextSizeA(titleFontSize, FLT_MAX, 0, brand.data());
	const ImVec2 versionTextSize = titleFont->CalcTextSizeA(versionSize, FLT_MAX, 0, version.c_str());
	const ImVec2 titleSize(brandSize.x + style.ItemSpacing.x + versionTextSize.x, std::max(brandSize.y, versionTextSize.y));

	const float brandBlockHeight = titleFontSize + currentFontSize * 1.05f;
	const float logoSize = brandBlockHeight;
	const float logoAspectRatio = showLogo && uiIcons.logo.size.y > 0.0f ? uiIcons.logo.size.x / uiIcons.logo.size.y : 1.0f;
	const float logoWidth = showLogo ? logoSize * logoAspectRatio : 0.0f;
	const float iconsWidth = actionIcons.empty() ? 0.0f :
	                                               (static_cast<float>(actionIcons.size()) * actionIconSize) +
	                                                   (static_cast<float>(actionIcons.size() - 1) * iconSpacing);

	const float brandRowHeight = std::max({ logoSize, actionIconSize, brandBlockHeight }) + paddingY * 2.0f;
	const ImVec2 cursorStart = ImGui::GetCursorPos();
	const ImVec2 screenStart = ImGui::GetCursorScreenPos();
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float subtitleWidth = ImGui::CalcTextSize("Community Shaders Expanded").x;
	const float brandWidth = std::max(titleSize.x, subtitleWidth) + logoWidth + style.ItemSpacing.x;
	const bool stackedActions = availableWidth < brandWidth + iconsWidth + paddingX * 2 + currentFontSize;
	const float headerHeight = brandRowHeight + (stackedActions ? actionIconSize + paddingY : 0);
	const float rightLimit = screenStart.x + availableWidth;
	const float iconStartX = rightLimit - paddingX - iconsWidth;
	float titleX = screenStart.x + paddingX;
	if (menu->GetTheme().CenterHeader) {
		const float titleRight = stackedActions ? rightLimit - paddingX : iconStartX - style.ItemSpacing.x;
		titleX += std::max(0.0f, (titleRight - titleX - brandWidth) * .5f);
	}
	const float centerY = screenStart.y + brandRowHeight * 0.5f;
	const float actionsY = stackedActions ? screenStart.y + brandRowHeight + actionIconSize * .5f : centerY;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImU32 logoTint = IM_COL32_WHITE;
	if (menu->GetSettings().Theme.UseMonochromeLogo) {
		logoTint = ImGui::GetColorU32(menu->GetSettings().Theme.Palette.Text);
	}

	if (showLogo && uiIcons.logo.texture) {
		const ImVec2 logoMin(titleX, centerY - logoSize * 0.5f);
		const ImVec2 logoMax(logoMin.x + logoWidth, logoMin.y + logoSize);
		drawList->AddImage(uiIcons.logo.texture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), logoTint);
		titleX = logoMax.x + style.ItemSpacing.x;
	}

	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
	const float titleClipMaxX = std::max(titleX, stackedActions ? rightLimit - paddingX : iconStartX - style.ItemSpacing.x);
	drawList->PushClipRect(
		ImVec2(titleX, screenStart.y),
		ImVec2(titleClipMaxX, screenStart.y + headerHeight),
		true);
	drawList->AddText(titleFont, titleFontSize, ImVec2(titleX, centerY - brandBlockHeight * .5f), textColor, brand.data());
	drawList->AddText(ImGui::GetFont(), versionSize, ImVec2(titleX + brandSize.x + style.ItemSpacing.x, centerY - brandBlockHeight * .5f + brandSize.y - versionTextSize.y), ImGui::GetColorU32(Util::Color::SecondaryText()), version.c_str());
	drawList->AddText(ImGui::GetFont(), currentFontSize, { titleX, centerY + brandBlockHeight * .5f - currentFontSize }, ImGui::GetColorU32(Util::Color::SecondaryText()), "Community Shaders Expanded");
	drawList->PopClipRect();

	const char* runtime = T("menu.header.runtime", "Skyrim SE / AE");
	const float badgeWidth = ImGui::CalcTextSize(runtime).x + currentFontSize * 1.2f;
	const float badgeRight = iconStartX - currentFontSize * 1.4f;
	if (!stackedActions && badgeRight - badgeWidth > titleX + std::max(titleSize.x, subtitleWidth) + currentFontSize) {
		const ImVec2 badgeMin{ badgeRight - badgeWidth, centerY - currentFontSize };
		const ImVec2 badgeMax{ badgeRight, centerY + currentFontSize };
		drawList->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(ImGuiCol_FrameBg), currentFontSize * .25f);
		drawList->AddRect(badgeMin, badgeMax, ImGui::GetColorU32(ImGuiCol_Border), currentFontSize * .25f);
		drawList->AddText({ badgeMin.x + currentFontSize * .6f, centerY - currentFontSize * .5f }, textColor, runtime);
	}

	float iconX = iconStartX;
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];
		if (!icon.texture && icon.glyph == ActionIcon::Glyph::Texture)
			continue;

		const ImVec2 buttonMin(iconX, actionsY - actionIconSize * 0.5f);
		const ImVec2 buttonMax(buttonMin.x + actionIconSize, buttonMin.y + actionIconSize);
		ImGui::SetCursorScreenPos(buttonMin);
		ImGui::ItemSize(ImVec2(actionIconSize, actionIconSize));
		DrawActionButton(icon, buttonMin, buttonMax, paddingReduction * .5f);

		iconX += actionIconSize + iconSpacing;
	}

	ImGui::SetCursorPos(ImVec2(cursorStart.x, cursorStart.y + headerHeight));
}
