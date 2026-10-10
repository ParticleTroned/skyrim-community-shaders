#include "MenuHeaderRenderer.h"
#include "FeatureListRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Features/LightLimitFix.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Fonts.h"
#include "Globals.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "State.h"
#include "ThemeManager.h"
#include "Util.h"

namespace
{
	using RoleFontGuard = MenuFonts::FontRoleGuard;
	constexpr float kHeaderActionIconScale = 0.75f;
	constexpr const char* kShaderFailureTooltip =
		"Show/hide the shader failure message. Errors may appear in game. Check feature versions and load order, then CommunityShaders.log. Use the Nexus Mods page or Discord for help.";

	struct HeaderContentAlignment
	{
		ImGuiID window = 0;
		int frame = -1;
		float right = 0;
	};
	HeaderContentAlignment headerContentAlignment;

	float GetHeaderIconSize(float a_uiScale)
	{
		return ImGui::GetFontSize() * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER * a_uiScale;
	}

	float GetHeaderActionIconSize(float a_uiScale)
	{
		return GetHeaderIconSize(a_uiScale) * 1.5f * kHeaderActionIconScale;
	}

	float GetDockedActionIconSize(float a_uiScale, float a_titleBarHeight)
	{
		const float desiredSize = ImGui::GetFontSize() * ThemeManager::Constants::DOCKED_ICON_SIZE_MULTIPLIER * a_uiScale * 1.5f;
		const float maxSize = std::max(1.0f, a_titleBarHeight - 2.0f * std::max(1.0f, ImGui::GetStyle().FramePadding.y * 0.35f));
		return std::min(desiredSize, maxSize);
	}

	float GetSteamVRDockHandleSize(float a_uiScale)
	{
		return GetHeaderActionIconSize(a_uiScale);
	}

	float GetUndockedIconSpacing(float a_uiScale)
	{
		return ThemeManager::Constants::UNDOCKED_ICON_ITEM_SPACING * a_uiScale;
	}

	float GetUndockedActionButtonSize(float a_uiScale)
	{
		const auto& style = ImGui::GetStyle();
		const float iconSize = GetHeaderActionIconSize(a_uiScale);
		const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * a_uiScale;
		return std::max(0.0f, iconSize - paddingReduction) + style.FramePadding.x * 2.0f;
	}

	float GetSteamVRResizeHandleSize(float a_uiScale)
	{
		const float baseHandleSize = std::max(ImGui::GetFontSize() * 1.15f, 18.0f) * a_uiScale;
		return baseHandleSize * 1.875f;
	}

	float GetSteamVRResizeHandleInset(float a_uiScale)
	{
		return std::max(ImGui::GetStyle().WindowBorderSize, 1.0f * a_uiScale);
	}

	struct MenuResizeDragState
	{
		ImGuiID activeId = 0;
		ImVec2 startMouse = ImVec2(0.0f, 0.0f);
		ImVec2 startPos = ImVec2(0.0f, 0.0f);
		ImVec2 startSize = ImVec2(0.0f, 0.0f);
	};

	MenuResizeDragState g_menuResizeDragState;
	constexpr float kSteamVRResizePointerSensitivity = 1.5f;

	bool HasValidMousePos(const ImVec2& a_pos)
	{
		return std::isfinite(a_pos.x) &&
		       std::isfinite(a_pos.y) &&
		       a_pos.x > -100000.0f &&
		       a_pos.y > -100000.0f;
	}

	float ClampResizeValue(float a_value, float a_min, float a_max)
	{
		if (a_max < a_min) {
			return a_min;
		}

		return std::clamp(a_value, a_min, a_max);
	}

	float GetSteamVRHeaderRightInset(float a_uiScale)
	{
		const auto& style = ImGui::GetStyle();
		return std::max(style.WindowBorderSize + style.CellPadding.x + style.FramePadding.x + 8.0f * a_uiScale, 8.0f * a_uiScale);
	}

	ImGuiDockNode* GetDockSpaceTargetNode(ImGuiID a_dockSpaceId)
	{
		if (a_dockSpaceId == 0)
			return nullptr;

		if (auto* centralNode = ImGui::DockBuilderGetCentralNode(a_dockSpaceId))
			return centralNode;

		auto* rootNode = ImGui::DockBuilderGetNode(a_dockSpaceId);
		if (!rootNode) {
			rootNode = ImGui::DockContextFindNodeByID(ImGui::GetCurrentContext(), a_dockSpaceId);
		}
		if (!rootNode)
			return nullptr;

		return rootNode->CentralNode ? rootNode->CentralNode : rootNode;
	}

	bool DockWindowToDockSpace(ImGuiWindow* a_window, ImGuiID a_dockSpaceId)
	{
		if (!a_window || a_dockSpaceId == 0)
			return false;

		auto* targetNode = GetDockSpaceTargetNode(a_dockSpaceId);
		if (!targetNode)
			return false;

		ImGui::DockContextQueueDock(ImGui::GetCurrentContext(), nullptr, targetNode, a_window, ImGuiDir_None, 0.0f, false);
		return true;
	}
}

void MenuHeaderRenderer::RenderHeader(
	bool isDocked,
	bool showLogo,
	bool canShowIcons,
	float uiScale,
	const Menu::UIIcons& uiIcons,
	bool forceStableHeader,
	bool showSteamVRDockHandle,
	ImGuiID steamVRDockSpaceId)
{
	if (!globals::menu) {
		logger::error("MenuHeaderRenderer::RenderHeader: globals::menu is null, cannot render header");
		return;
	}

	std::string title{ Plugin::MENU_TITLE };
	const std::string brand{ "CSX" };
	const std::string version = title.substr(4);
	auto actionIcons = BuildActionIcons(canShowIcons, uiIcons);
	if (!isDocked || forceStableHeader)
		actionIcons.push_back({ "HeaderClose", nullptr, nullptr, "Close menu", [] { globals::menu->CloseMenu(); }, ActionIcon::Glyph::Close });

	if (forceStableHeader) {
		RenderStableHeader(title, showLogo, actionIcons, uiScale, uiIcons);
	} else if (isDocked) {
		// Draw action icons in the title bar area
		RenderDockedIcons(actionIcons, uiScale);
	} else {
		RenderStableHeader(title, showLogo, actionIcons, uiScale, uiIcons, showSteamVRDockHandle, steamVRDockSpaceId);
	}

	// Add separators - no separator needed for docked mode since icons are in title bar
	const bool renderedInlineHeader = !isDocked || forceStableHeader;
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
			if (Util::ButtonWithFlash("Save Settings", { -1, 0 })) {
				globals::state->Save();
			}

			// Restore Saved Settings Button
			ImGui::TableNextColumn();
			if (Util::ButtonWithFlash("Restore Saved Settings", { -1, 0 })) {
				globals::state->Load();
				globals::features::llf::particleLights.GetConfigs();
			}

			// Clear Shader Cache Button
			ImGui::TableNextColumn();
			{
				const bool capturing = shaderCache->IsCapturingActiveShaders();
				const bool awaitingMenuClose = shaderCache->IsAwaitingMenuCloseCapture();
				ImGui::BeginDisabled(capturing || awaitingMenuClose);
				const std::string label = awaitingMenuClose ?
				                              "Close the menu to finish" :
				                          capturing ?
				                              std::format("Capturing... {}", shaderCache->GetActiveShaderCaptureFramesRemaining()) :
				                              "Clear Shader Cache";
				if (ImGui::Button(label.c_str(), { -1, 0 })) {
					Util::RequestClearShaderCacheConfirmation(Util::ResolveShaderCacheClearScope());
				}
				ImGui::EndDisabled();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped("%s", Util::GetClearShaderCacheTooltip());
			}

			// Error message toggle if needed
			if (shaderCache->GetFailedTasks()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
					shaderCache->ToggleErrorMessages();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted(kShaderFailureTooltip);
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
		if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
			shaderCache->ToggleErrorMessages();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(kShaderFailureTooltip);
		}

		// Add second separator when showing error button
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}
}

std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons)
{
	std::vector<ActionIcon> actionIcons{
		{ "HeaderSidebar", nullptr, nullptr,
			FeatureListRenderer::IsSidebarVisible() ? "Hide feature list" : "Show feature list",
			[] { FeatureListRenderer::SetSidebarVisible(!FeatureListRenderer::IsSidebarVisible()); }, ActionIcon::Glyph::Sidebar }
	};

	if (!canShowIcons) {
		return actionIcons;
	}

	// Build list of available action icons (in display order)
	if (uiIcons.saveSettings.texture) {
		actionIcons.push_back({ "HeaderSaveSettings",
			"HeaderSaveSettings",
			uiIcons.saveSettings.texture,
			"Save Settings",
			[]() {
				globals::state->Save();
			},
			ActionIcon::Glyph::SaveSettings });
	}
	if (uiIcons.loadSettings.texture) {
		actionIcons.push_back({ "HeaderRestoreSavedSettings",
			"HeaderRestoreSavedSettings",
			uiIcons.loadSettings.texture,
			"Restore Saved Settings",
			[]() {
				globals::state->Load();
				globals::features::llf::particleLights.GetConfigs();
			},
			ActionIcon::Glyph::LoadSettings });
	}
	if (uiIcons.clearCache.texture) {
		actionIcons.push_back({ "HeaderClearShaderCache",
			nullptr,
			uiIcons.clearCache.texture,
			Util::GetClearShaderCacheTooltip(),
			[]() {
				Util::RequestClearShaderCacheConfirmation(Util::ResolveShaderCacheClearScope());
			},
			ActionIcon::Glyph::ClearCache });
	}

	return actionIcons;
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

void MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return;

	// Get window position and calculate title bar area
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	float titleBarHeight = ImGui::GetFrameHeight();
	const float iconSize = GetDockedActionIconSize(uiScale, titleBarHeight);
	const float iconSpacing = ThemeManager::Constants::DOCKED_ICON_SPACING * uiScale;
	const float rightMargin = ThemeManager::Constants::DOCKED_RIGHT_MARGIN * uiScale;

	// Use foreground draw list to draw over the title bar
	ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

	// Calculate icon positions (right to left from close button)
	float iconX = windowPos.x + windowSize.x - rightMargin;
	float iconY = windowPos.y + (titleBarHeight - iconSize) * 0.5f;

	// Draw icons from right to left
	for (auto it = actionIcons.rbegin(); it != actionIcons.rend(); ++it) {
		iconX -= iconSize + iconSpacing;

		// Slightly reduce the icon rendering area to minimize any transparent padding
		const float paddingReduction = ThemeManager::Constants::DOCKED_ICON_PADDING_REDUCTION * uiScale;
		ImVec2 iconMin(iconX + paddingReduction, iconY + paddingReduction);
		ImVec2 iconMax(iconX + iconSize - paddingReduction, iconY + iconSize - paddingReduction);

		// Use the full area for mouse interaction (including padding)
		ImVec2 interactionMin(iconX, iconY);
		ImVec2 interactionMax(iconX + iconSize, iconY + iconSize);

		// Check mouse interaction against full area
		ImVec2 mousePos = ImGui::GetMousePos();
		bool isHovered = mousePos.x >= interactionMin.x && mousePos.x <= interactionMax.x &&
		                 mousePos.y >= interactionMin.y && mousePos.y <= interactionMax.y;
		const bool hasActiveFlash = it->flashId && Util::IsButtonFlashActive(it->flashId);

		// Only render if texture is valid
		if (it->texture || it->glyph != ActionIcon::Glyph::Texture) {
			// Draw icon with hover effect, using reduced area to minimize padding
			ImU32 tintColor;
			if (globals::menu->GetSettings().Theme.UseMonochromeIcons) {
				// Use theme text color for monochrome icons
				ImVec4 textColor = globals::menu->GetSettings().Theme.Palette.Text;
				if (!isHovered && !hasActiveFlash) {
					textColor.w *= 0.85f;  // Slightly reduce alpha when not hovered
				}
				tintColor = ImGui::GetColorU32(textColor);
			} else {
				// Use white/gray tint for colored icons
				tintColor = (isHovered || hasActiveFlash) ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 220);
			}
			DrawActionIcon(fgDrawList, *it, iconMin, iconMax, tintColor);
		}

		if (isHovered || hasActiveFlash) {
			ImVec4 feedbackColor = hasActiveFlash ?
			                           Util::GetButtonFlashColor(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)) :
			                           ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
			feedbackColor.w = hasActiveFlash ? 0.34f : 0.18f;
			fgDrawList->AddRectFilled(interactionMin, interactionMax, ImGui::GetColorU32(feedbackColor));
		}

		// Handle interaction
		if (isHovered) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				it->callback();
				if (it->flashId) {
					Util::TriggerButtonFlash(it->flashId);
				}
			}

			// Set tooltip manually since we're drawing outside normal ImGui flow
			ImGui::SetTooltip("%s", it->tooltip);
		}
	}
}

void MenuHeaderRenderer::RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return;

	// Undocked: Draw icons as ImageButtons in a table column
	const float iconSize = GetHeaderActionIconSize(uiScale);
	const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * uiScale;
	const float imageExtent = std::max(0.0f, iconSize - paddingReduction);
	const ImVec2 imageSize(imageExtent, imageExtent);

	// Setup button styling for transparent background with hover effects
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GetUndockedIconSpacing(uiScale), 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);  // Remove button borders
	auto iconButtonStyle = Util::TransparentIconButtonStyle();

	// Get tint color for monochrome icons
	ImVec4 tintColor = ImVec4(1, 1, 1, 1);
	if (globals::menu->GetSettings().Theme.UseMonochromeIcons) {
		tintColor = globals::menu->GetSettings().Theme.Palette.Text;
	}

	// Draw action icons as ImageButtons
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];

		// Skip if texture is null
		if (!icon.texture && icon.glyph == ActionIcon::Glyph::Texture) {
			continue;
		}

		std::string buttonId = std::format("##{}", icon.id);

		// Use ImageButton with reduced image size to minimize padding
		const bool clicked = icon.glyph != ActionIcon::Glyph::Texture ?
		                         ImGui::InvisibleButton(buttonId.c_str(), imageSize) :
		                     icon.flashId ?
		                         Util::ImageButtonWithFlash(icon.flashId, icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor) :
		                         ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor);
		if (icon.glyph != ActionIcon::Glyph::Texture)
			DrawActionIcon(ImGui::GetWindowDrawList(), icon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(tintColor));
		if (clicked) {
			icon.callback();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", icon.tooltip);
		}

		// Add SameLine except for the last button
		if (i < actionIcons.size() - 1) {
			ImGui::SameLine();
		}
	}

	// Restore default style
	ImGui::PopStyleVar(2);  // Pop both style variables: ItemSpacing and FrameBorderSize
}

void MenuHeaderRenderer::RenderSteamVRDockHandle(float uiScale, ImGuiID dockSpaceId)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window)
		return;

	const float handleSize = GetSteamVRDockHandleSize(uiScale);
	const ImVec2 buttonSize(handleSize, handleSize);

	ImGui::InvisibleButton("##SteamVRDockHandle", buttonSize);
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();
	const bool activated = ImGui::IsItemActivated();
	const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	if (hovered || active || activated) {
		ImGui::GetIO().ConfigDockingWithShift = false;
	}
	if (doubleClicked) {
		DockWindowToDockSpace(window, dockSpaceId);
	} else if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		ImGui::StartMouseMovingWindow(window);
	}
	if (hovered) {
		ImGui::SetTooltip("Drag to move or dock\nDouble-click to dock");
	}

	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
	const float radius = handleSize * 0.32f;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 bgColor = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered :
																						ImGuiCol_Button);
	const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_Text);
	drawList->AddRectFilled(min, max, bgColor, handleSize * 0.18f);
	drawList->AddRect(ImVec2(center.x - radius, center.y - radius), ImVec2(center.x + radius, center.y + radius), lineColor, 1.0f, 0, 1.5f);
	drawList->AddLine(ImVec2(center.x - radius * 0.55f, center.y), ImVec2(center.x + radius * 0.55f, center.y), lineColor, 1.2f);
	drawList->AddLine(ImVec2(center.x, center.y - radius * 0.55f), ImVec2(center.x, center.y + radius * 0.55f), lineColor, 1.2f);
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

void MenuHeaderRenderer::RenderSteamVRResizeHandles(float uiScale)
{
	RenderResizeHandles(uiScale, true);
}

void MenuHeaderRenderer::RenderResizeGrip(float uiScale, bool customResizeControls)
{
	RenderResizeHandles(uiScale, false, customResizeControls);
}

void MenuHeaderRenderer::RenderResizeHandles(float uiScale, bool steamVRControls, bool customResizeControls)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window || window->DockIsActive || (!steamVRControls && ((window->Flags & ImGuiWindowFlags_AlwaysAutoResize) || (!customResizeControls && (window->Flags & ImGuiWindowFlags_NoResize)))))
		return;

	const ImVec2 windowPos = window->Pos;
	const ImVec2 windowSize = window->Size;
	const float topLeftSize = GetSteamVRResizeHandleSize(uiScale);
	const float bottomRightSize = steamVRControls ? topLeftSize * 1.2f : std::max(ImGui::GetFontSize() * 1.5f, 18.0f) * uiScale;
	const float handleInset = GetSteamVRResizeHandleInset(uiScale);
	const bool headsetControls = steamVRControls || customResizeControls;
	const float sensitivity = steamVRControls ? kSteamVRResizePointerSensitivity : 1.0f;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 resizeBoundsMin = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
	const ImVec2 resizeBoundsMax = viewport ?
	                                   ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y) :
	                                   ImGui::GetIO().DisplaySize;
	auto drawResizeHandle = [&](const char* id, const ImVec2& min, bool topLeft, float handleSize) {
		const ImVec2 themeMinimum = ImGui::GetStyle().WindowMinSize;
		float minWidth = themeMinimum.x;
		float minHeight = themeMinimum.y;
		if (headsetControls) {
			// Available space extends from the fixed opposite corner toward the dragged edge.
			const ImVec2 available = topLeft ?
			                             ImVec2(windowPos.x + windowSize.x - resizeBoundsMin.x, windowPos.y + windowSize.y - resizeBoundsMin.y) :
			                             ImVec2(resizeBoundsMax.x - windowPos.x, resizeBoundsMax.y - windowPos.y);
			minWidth = ClampResizeValue(420.0f * uiScale, themeMinimum.x, available.x);
			minHeight = ClampResizeValue(320.0f * uiScale, themeMinimum.y, available.y);
		}
		const ImGuiID itemId = window->GetID(id);
		const ImRect bounds(min, ImVec2(min.x + handleSize, min.y + handleSize));
		if (!ImGui::ItemAdd(bounds, itemId, nullptr, ImGuiItemFlags_NoNav))
			return;
		bool hovered = false, active = false;
		const auto mouse = ImGui::GetIO().MousePos;
		const float diagonal = mouse.x - min.x + mouse.y - min.y;
		const bool insideTriangle = bounds.Contains(mouse) && (topLeft ? diagonal <= handleSize : diagonal >= handleSize);
		if (insideTriangle || ImGui::GetActiveID() == itemId)
			ImGui::ButtonBehavior(bounds, itemId, &hovered, &active, ImGuiButtonFlags_MouseButtonLeft | static_cast<ImGuiButtonFlags>(ImGuiButtonFlags_FlattenChildren));
		const bool activated = ImGui::IsItemActivated();
		if (hovered || active) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
		}

		if (activated) {
			g_menuResizeDragState.activeId = itemId;
			g_menuResizeDragState.startMouse = ImGui::GetIO().MousePos;
			g_menuResizeDragState.startPos = window->Pos;
			g_menuResizeDragState.startSize = window->Size;
		}

		if (!active && g_menuResizeDragState.activeId == itemId) {
			g_menuResizeDragState = {};
		}

		if (active && g_menuResizeDragState.activeId == itemId) {
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			if (HasValidMousePos(mousePos)) {
				const ImVec2 clampedMousePos(
					ClampResizeValue(mousePos.x, resizeBoundsMin.x, resizeBoundsMax.x),
					ClampResizeValue(mousePos.y, resizeBoundsMin.y, resizeBoundsMax.y));
				const ImVec2 delta(
					(clampedMousePos.x - g_menuResizeDragState.startMouse.x) * sensitivity,
					(clampedMousePos.y - g_menuResizeDragState.startMouse.y) * sensitivity);
				ImVec2 newPos = g_menuResizeDragState.startPos;
				ImVec2 newSize = g_menuResizeDragState.startSize;
				if (topLeft) {
					const ImVec2 fixedBottomRight(
						g_menuResizeDragState.startPos.x + g_menuResizeDragState.startSize.x,
						g_menuResizeDragState.startPos.y + g_menuResizeDragState.startSize.y);
					newPos.x = ClampResizeValue(g_menuResizeDragState.startPos.x + delta.x, resizeBoundsMin.x, fixedBottomRight.x - minWidth);
					newPos.y = ClampResizeValue(g_menuResizeDragState.startPos.y + delta.y, resizeBoundsMin.y, fixedBottomRight.y - minHeight);
					newSize.x = fixedBottomRight.x - newPos.x;
					newSize.y = fixedBottomRight.y - newPos.y;
					ImGui::SetWindowPos(window, newPos, ImGuiCond_Always);
				} else {
					const float rightEdge = ClampResizeValue(g_menuResizeDragState.startPos.x + g_menuResizeDragState.startSize.x + delta.x, g_menuResizeDragState.startPos.x + minWidth, resizeBoundsMax.x);
					const float bottomEdge = ClampResizeValue(g_menuResizeDragState.startPos.y + g_menuResizeDragState.startSize.y + delta.y, g_menuResizeDragState.startPos.y + minHeight, resizeBoundsMax.y);
					newSize.x = rightEdge - g_menuResizeDragState.startPos.x;
					newSize.y = bottomEdge - g_menuResizeDragState.startPos.y;
				}
				ImGui::SetWindowSize(window, newSize, ImGuiCond_Always);
			}
		}

		ImVec4 colorVec = ImGui::GetStyleColorVec4(active ? ImGuiCol_ResizeGripActive : hovered ? ImGuiCol_ResizeGripHovered :
																								  ImGuiCol_ResizeGrip);
		colorVec.w = std::max(colorVec.w, hovered || active ? 0.95f : 0.75f);
		const ImU32 color = ImGui::GetColorU32(colorVec);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		drawList->PushClipRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y), true);
		if (topLeft) {
			drawList->AddTriangleFilled(min, ImVec2(min.x + handleSize, min.y), ImVec2(min.x, min.y + handleSize), color);
		} else {
			const ImVec2 br = ImVec2(min.x + handleSize, min.y + handleSize);
			drawList->AddTriangleFilled(br, ImVec2(br.x - handleSize, br.y), ImVec2(br.x, br.y - handleSize), color);
		}
		drawList->PopClipRect();
	};

	if (steamVRControls)
		drawResizeHandle("##SteamVRResizeTopLeft", ImVec2(windowPos.x + handleInset, windowPos.y + handleInset), true, topLeftSize);
	drawResizeHandle(steamVRControls ? "##SteamVRResizeBottomRight" : "##MenuResizeBottomRight",
		ImVec2(windowPos.x + windowSize.x - bottomRightSize - handleInset, windowPos.y + windowSize.y - bottomRightSize - handleInset), false, bottomRightSize);
}

void MenuHeaderRenderer::RenderStableHeader(const std::string& title, bool showLogo, const std::vector<ActionIcon>& actionIcons, float uiScale, const Menu::UIIcons& uiIcons, bool showSteamVRDockHandle, ImGuiID steamVRDockSpaceId)
{
	auto* menu = globals::menu;
	if (!menu)
		return;

	ImGuiStyle& style = ImGui::GetStyle();
	const float currentFontSize = ImGui::GetFontSize();
	const float actionIconSize = GetHeaderActionIconSize(uiScale);
	const float textScaleFactor = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE * uiScale;
	const float paddingX = currentFontSize;
	const float dockReserve = showSteamVRDockHandle ? GetSteamVRDockHandleSize(uiScale) + GetSteamVRHeaderRightInset(uiScale) + style.ItemSpacing.x : 0;
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
	const bool stackedActions = availableWidth < brandWidth + iconsWidth + dockReserve + paddingX * 2 + currentFontSize;
	const float headerHeight = brandRowHeight + (stackedActions ? actionIconSize + paddingY : 0);
	const float rightLimit = screenStart.x + availableWidth;
	const float iconStartX = rightLimit - paddingX - iconsWidth - dockReserve;
	float titleX = screenStart.x + paddingX + (showSteamVRDockHandle ? GetSteamVRResizeHandleSize(uiScale) : 0);
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

	const char* runtime = globals::game::isVR ? "Skyrim VR" : "Skyrim SE / AE";
	const float badgeWidth = ImGui::CalcTextSize(runtime).x + currentFontSize * 1.2f;
	const float badgeRight = iconStartX - currentFontSize * 1.4f;
	if (!stackedActions && badgeRight - badgeWidth > titleX + std::max(titleSize.x, subtitleWidth) + currentFontSize) {
		const ImVec2 badgeMin{ badgeRight - badgeWidth, centerY - currentFontSize };
		const ImVec2 badgeMax{ badgeRight, centerY + currentFontSize };
		drawList->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(ImGuiCol_FrameBg), currentFontSize * .25f);
		drawList->AddRect(badgeMin, badgeMax, ImGui::GetColorU32(ImGuiCol_Border), currentFontSize * .25f);
		drawList->AddText({ badgeMin.x + currentFontSize * .6f, centerY - currentFontSize * .5f }, textColor, runtime);
	}
	if (showSteamVRDockHandle) {
		ImGui::SetCursorScreenPos({ rightLimit - paddingX - GetSteamVRDockHandleSize(uiScale) - GetSteamVRHeaderRightInset(uiScale), actionsY - GetSteamVRDockHandleSize(uiScale) * .5f });
		RenderSteamVRDockHandle(uiScale, steamVRDockSpaceId);
	}

	float iconX = iconStartX;
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];
		if (!icon.texture && icon.glyph == ActionIcon::Glyph::Texture)
			continue;

		const ImVec2 buttonMin(iconX, actionsY - actionIconSize * 0.5f);
		const ImVec2 buttonMax(buttonMin.x + actionIconSize, buttonMin.y + actionIconSize);
		const ImVec2 imageMin(buttonMin.x + paddingReduction * 0.5f, buttonMin.y + paddingReduction * 0.5f);
		const ImVec2 imageMax(buttonMax.x - paddingReduction * 0.5f, buttonMax.y - paddingReduction * 0.5f);

		ImGui::SetCursorScreenPos(buttonMin);
		ImGui::PushID(static_cast<int>(i));
		const bool clicked = ImGui::InvisibleButton("##StableHeaderAction", ImVec2(actionIconSize, actionIconSize));
		const bool hovered = ImGui::IsItemHovered();
		const bool hasActiveFlash = icon.flashId && Util::IsButtonFlashActive(icon.flashId);
		if (clicked) {
			icon.callback();
			if (icon.flashId) {
				Util::TriggerButtonFlash(icon.flashId);
			}
		}
		if (hovered || hasActiveFlash) {
			ImVec4 feedbackColor = hasActiveFlash ?
			                           Util::GetButtonFlashColor(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)) :
			                           ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
			feedbackColor.w = hasActiveFlash ? 0.34f : 0.18f;
			drawList->AddRectFilled(buttonMin, buttonMax, ImGui::GetColorU32(feedbackColor));
		}
		if (hovered) {
			ImGui::SetTooltip("%s", icon.tooltip);
		}

		ImVec4 tintColor = ImVec4(1, 1, 1, 1);
		if (menu->GetSettings().Theme.UseMonochromeIcons) {
			tintColor = menu->GetSettings().Theme.Palette.Text;
		}
		DrawActionIcon(drawList, icon, imageMin, imageMax, ImGui::GetColorU32(tintColor));
		ImGui::PopID();

		iconX += actionIconSize + iconSpacing;
	}

	ImGui::SetCursorPos(ImVec2(cursorStart.x, cursorStart.y + headerHeight));
}
