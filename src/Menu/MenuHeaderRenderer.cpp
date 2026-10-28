#include "MenuHeaderRenderer.h"
#include "FeatureListRenderer.h"
#include "Layout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Features/LightLimitFix.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/VR.h"
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
		"Show/hide the shader failure message. Errors may appear in game. Check feature versions and load order, then CSX.log. Use the Nexus Mods page or Discord for help.";

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

	float GetSteamVRDockHandleSize()
	{
		return GetHeaderActionIconSize();
	}

	float GetSteamVRResizeHandleSize(float a_uiScale)
	{
		const float baseHandleSize = std::max(ImGui::GetFontSize() * 1.15f, 18.0f * a_uiScale);
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
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::Save, "Save CSX Settings", { -1, 0 });

			// Restore Saved Settings Button
			ImGui::TableNextColumn();
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::Load, "Restore Saved Settings", { -1, 0 });

			// Clear Shader Cache Button
			ImGui::TableNextColumn();
			MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::ClearShaderCache, "Clear Shader Cache", { -1, 0 });

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
				MenuUI::InvokeGlobalSettingsAction(MenuUI::GlobalSettingsAction::Save);
			},
			ActionIcon::Glyph::SaveSettings, MenuUI::GlobalSettingsAction::Save });
	}
	if (uiIcons.loadSettings.texture) {
		actionIcons.push_back({ "HeaderRestoreSavedSettings",
			"HeaderRestoreSavedSettings",
			uiIcons.loadSettings.texture,
			"Restore Saved Settings",
			[]() {
				MenuUI::InvokeGlobalSettingsAction(MenuUI::GlobalSettingsAction::Load);
			},
			ActionIcon::Glyph::LoadSettings, MenuUI::GlobalSettingsAction::Load });
	}
	if (uiIcons.clearCache.texture) {
		actionIcons.push_back({ "HeaderClearShaderCache",
			nullptr,
			uiIcons.clearCache.texture,
			Util::GetClearShaderCacheTooltip(),
			[]() {
				MenuUI::InvokeGlobalSettingsAction(MenuUI::GlobalSettingsAction::ClearShaderCache);
			},
			ActionIcon::Glyph::ClearCache, MenuUI::GlobalSettingsAction::ClearShaderCache });
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

void MenuHeaderRenderer::RenderSteamVRDockHandle(ImGuiID dockSpaceId)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window)
		return;

	const float handleSize = GetSteamVRDockHandleSize();
	const ImVec2 buttonSize(handleSize, handleSize);

	const bool headsetPointer = globals::game::isVR && globals::features::vr.IsMenuPointerInHeadset();
	const auto disabled = Util::DisableGuard(headsetPointer);
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
	Util::AddTooltip(headsetPointer ? "Use the desktop mouse to move, resize or dock. Use Menu Scale and grip dragging in VR." :
									  "Drag to move or dock\nDouble-click to dock",
		ImGuiHoveredFlags_AllowWhenDisabled);

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

void MenuHeaderRenderer::RenderSteamVRResizeHandles(float uiScale, ImVec2 minimumSize)
{
	RenderResizeHandles(uiScale, minimumSize, true);
}

void MenuHeaderRenderer::RenderResizeGrip(float uiScale, ImVec2 minimumSize, bool customResizeControls)
{
	RenderResizeHandles(uiScale, minimumSize, false, customResizeControls);
}

void MenuHeaderRenderer::RenderResizeHandles(float uiScale, ImVec2 minimumSize, bool steamVRControls, bool customResizeControls)
{
	if (globals::game::isVR && globals::features::vr.IsMenuPointerInHeadset()) {
		if (g_menuResizeDragState.activeId != 0 && ImGui::GetActiveID() == g_menuResizeDragState.activeId)
			ImGui::ClearActiveID();
		g_menuResizeDragState = {};
		return;
	}
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window || window->DockIsActive || (!steamVRControls && ((window->Flags & ImGuiWindowFlags_AlwaysAutoResize) || (!customResizeControls && (window->Flags & ImGuiWindowFlags_NoResize)))))
		return;

	const ImVec2 windowPos = window->Pos;
	const ImVec2 windowSize = window->Size;
	const float topLeftSize = GetSteamVRResizeHandleSize(uiScale);
	const float bottomRightSize = steamVRControls ? topLeftSize * 1.2f : std::max(ImGui::GetFontSize() * 1.5f, 18.0f * uiScale);
	const float handleInset = GetSteamVRResizeHandleInset(uiScale);
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 canvasOrigin = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
	const ImVec2 canvasSize = viewport ? viewport->WorkSize : ImGui::GetIO().DisplaySize;
	const float padding = MenuUI::Layout::CanvasPadding;
	// Match canvas recovery so a held edge drag cannot fight the next frame's bounds repair.
	const ImVec2 resizeBoundsMin{ canvasOrigin.x + padding, canvasOrigin.y + padding };
	const ImVec2 resizeBoundsMax{ resizeBoundsMin.x + std::max(1.0f, canvasSize.x - padding * 2),
		resizeBoundsMin.y + std::max(1.0f, canvasSize.y - padding * 2) };
	auto drawResizeHandle = [&](const char* id, const ImVec2& min, bool topLeft, float handleSize) {
		// Direct SetWindowSize calls must honor the same usable minimum as native resizing.
		const ImVec2 available = topLeft ?
		                             ImVec2(windowPos.x + windowSize.x - resizeBoundsMin.x, windowPos.y + windowSize.y - resizeBoundsMin.y) :
		                             ImVec2(resizeBoundsMax.x - windowPos.x, resizeBoundsMax.y - windowPos.y);
		const float minWidth = std::min(minimumSize.x, available.x);
		const float minHeight = std::min(minimumSize.y, available.y);
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
					clampedMousePos.x - g_menuResizeDragState.startMouse.x,
					clampedMousePos.y - g_menuResizeDragState.startMouse.y);
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
	const float actionIconSize = GetHeaderActionIconSize();
	const float textScaleFactor = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE;
	const float paddingX = currentFontSize;
	const float dockReserve = showSteamVRDockHandle ? GetSteamVRDockHandleSize() + GetSteamVRHeaderRightInset(uiScale) + style.ItemSpacing.x : 0;
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
		ImGui::SetCursorScreenPos({ rightLimit - paddingX - GetSteamVRDockHandleSize() - GetSteamVRHeaderRightInset(uiScale), actionsY - GetSteamVRDockHandleSize() * .5f });
		RenderSteamVRDockHandle(steamVRDockSpaceId);
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
