#include "MenuHeaderRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>

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

	float GetHeaderIconSize(float a_uiScale)
	{
		return ImGui::GetFontSize() * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER * a_uiScale;
	}

	float GetUndockedIconSpacing(float a_uiScale)
	{
		return ThemeManager::Constants::UNDOCKED_ICON_ITEM_SPACING * a_uiScale;
	}

	float GetUndockedActionButtonSize(float a_uiScale)
	{
		const auto& style = ImGui::GetStyle();
		const float iconSize = GetHeaderIconSize(a_uiScale);
		const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * a_uiScale;
		return std::max(0.0f, iconSize - paddingReduction) + style.FramePadding.x * 2.0f;
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

	auto versionStr = Util::GetFormattedVersion(Plugin::VERSION);
	auto title = std::format("CS {} Particle Lights Fork", versionStr);
	auto actionIcons = BuildActionIcons(canShowIcons, uiIcons);

	if (forceStableHeader) {
		RenderStableHeader(title, showLogo, actionIcons, uiScale, uiIcons);
	} else if (isDocked) {
		// Draw action icons in the title bar area
		RenderDockedIcons(actionIcons, uiScale);
	} else {
		// When not docked, show the custom header
		const bool centerHeader = globals::menu->GetTheme().CenterHeader && !showSteamVRDockHandle;

		const float baseTextScale = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE;
		const float textScaleFactor = baseTextScale * uiScale;
		const float logoSize = GetHeaderIconSize(uiScale);
		const float iconSpacing = GetUndockedIconSpacing(uiScale);
		const float actionButtonWidth = GetUndockedActionButtonSize(uiScale);
		const float actionButtonsWidth = actionIcons.empty() ? 0.0f :
		                                  actionButtonWidth * static_cast<float>(actionIcons.size()) +
		                                      iconSpacing * static_cast<float>(actionIcons.size() - 1);
		const float dockHandleWidth = showSteamVRDockHandle ? logoSize : 0.0f;
		const float dockHandleSpacing = showSteamVRDockHandle && !actionIcons.empty() ? iconSpacing : 0.0f;
		const float rightControlInset = showSteamVRDockHandle ? GetSteamVRHeaderRightInset(uiScale) : 0.0f;
		const float buttonColumnWidth = actionButtonsWidth + dockHandleSpacing + dockHandleWidth + rightControlInset;

		if ((showLogo || canShowIcons || showSteamVRDockHandle) && ImGui::BeginTable("##HeaderLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, buttonColumnWidth);
			ImGui::TableNextColumn();  // Title on the left with logo

			if (centerHeader) {
				// Calculate the width of the content
				float contentWidth = 0.0f;

				if (showLogo) {
					float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
					contentWidth = (logoSize * logoAspectRatio) + ImGui::GetStyle().ItemSpacing.x;
				}

				// Calculate text width
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					ImGui::SetWindowFontScale(textScaleFactor);
					contentWidth += ImGui::CalcTextSize(title.c_str()).x;
					ImGui::SetWindowFontScale(1.0f);
				}

				float offset = Util::GetCenterOffsetForContent(contentWidth);
				if (offset > 0.0f) {
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
				}
			} else {
				// Add padding for left-aligned layout
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ThemeManager::Constants::CURSOR_POSITION_PADDING);
			}

			// Always display logo if texture is available
			if (showLogo) {
				float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
				ImVec2 logoSizeVec(logoSize * logoAspectRatio, logoSize);

				// Determine tint color for logo
				ImU32 logoTint = IM_COL32_WHITE;
				if (globals::menu->GetSettings().Theme.UseMonochromeLogo) {
					ImVec4 textColor = globals::menu->GetSettings().Theme.Palette.Text;
					logoTint = ImGui::GetColorU32(textColor);
				}

				// Use our helper to render aligned logo and text with perfect vertical alignment
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					Util::DrawAlignedTextWithLogo(
						uiIcons.logo.texture,
						logoSizeVec,
						title.c_str(),
						textScaleFactor,
						logoTint);
				}
			} else {
				// No logo, just render the text with proper alignment
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					Util::DrawSharpText(title.c_str(), true, textScaleFactor);
				}
				ImGui::PopStyleVar();
			}

			// Buttons on the right
			ImGui::TableNextColumn();
			RenderUndockedIcons(actionIcons, uiScale);
			if (showSteamVRDockHandle) {
				if (!actionIcons.empty()) {
					ImGui::SameLine(0.0f, iconSpacing);
				}
				RenderSteamVRDockHandle(uiScale, steamVRDockSpaceId);
			}

			ImGui::EndTable();
		} else if (!(showLogo || canShowIcons)) {
			// No icons available - show just the title without the table layout
			const float fallbackTextScale = ThemeManager::Constants::HEADER_FALLBACK_TEXT_SCALE * uiScale;

			if (centerHeader) {
				// Calculate text width for centering
				float textWidth = 0.0f;
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					ImGui::SetWindowFontScale(fallbackTextScale);
					textWidth = ImGui::CalcTextSize(title.c_str()).x;
					ImGui::SetWindowFontScale(1.0f);
				}

				// Use helper to get centering offset
				float offset = Util::GetCenterOffsetForContent(textWidth);
				if (offset > 0.0f) {
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
				}
			}

			ImGui::SetWindowFontScale(fallbackTextScale);
			{
				RoleFontGuard titleFont(Menu::FontRole::Title);
				ImGui::TextUnformatted(title.c_str());
			}
			ImGui::SetWindowFontScale(1.0f);
		}
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
				globals::state->SaveTheme();
			}

			// Restore Saved Settings Button
			ImGui::TableNextColumn();
			if (Util::ButtonWithFlash("Restore Saved Settings", { -1, 0 })) {
				globals::state->Load();
				globals::features::llf::particleLights.GetConfigs();
			}

			// Clear Shader Cache Button
			ImGui::TableNextColumn();
			if (ImGui::Button("Clear Shader Cache", { -1, 0 })) {
				Util::RequestClearShaderCacheConfirmation();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Clears the shader cache and disk cache (if enabled). "
					"The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
					"The Disk Cache is a collection of compiled shaders on disk. "
					"Clearing will mean that shaders are recompiled only when the game re-encounters them. ");
			}

			// Error message toggle if needed
			if (shaderCache->GetFailedTasks()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
					shaderCache->ToggleErrorMessages();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Hide or show the shader failure message. "
						"Your installation is broken and will likely see errors in game. "
						"Please double check you have updated all features and that your load order is correct. "
						"See CommunityShaders.log for details and check the Nexus Mods page or Discord server. ");
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
			ImGui::Text(
				"Hide or show the shader failure message. "
				"Your installation is broken and will likely see errors in game. "
				"Please double check you have updated all features and that your load order is correct. "
				"See CommunityShaders.log for details and check the Nexus Mods page or Discord server. ");
		}

		// Add second separator when showing error button
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}
}

std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons)
{
	std::vector<ActionIcon> actionIcons;

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
				globals::state->SaveTheme();
			} });
	}
	if (uiIcons.loadSettings.texture) {
		actionIcons.push_back({ "HeaderRestoreSavedSettings",
			"HeaderRestoreSavedSettings",
			uiIcons.loadSettings.texture,
			"Restore Saved Settings",
			[]() {
				globals::state->Load();
				globals::features::llf::particleLights.GetConfigs();
			} });
	}
	if (uiIcons.clearCache.texture) {
		actionIcons.push_back({ "HeaderClearShaderCache",
			nullptr,
			uiIcons.clearCache.texture,
			"Clear Shader Cache\n\n"
			"Clears the shader cache and disk cache (if enabled).\n"
			"The Shader Cache is the collection of compiled shaders which replace\n"
			"the vanilla shaders at runtime. The Disk Cache is a collection of\n"
			"compiled shaders on disk. Clearing will mean that shaders are\n"
			"recompiled only when the game re-encounters them.",
			[]() {
				Util::RequestClearShaderCacheConfirmation();
			} });
	}

	return actionIcons;
}

void MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return;

	// Docked: Draw larger icons in the title bar using foreground draw list
	const float currentFontSize = ImGui::GetFontSize();
	const float iconSize = currentFontSize * ThemeManager::Constants::DOCKED_ICON_SIZE_MULTIPLIER * uiScale;
	const float iconSpacing = ThemeManager::Constants::DOCKED_ICON_SPACING * uiScale;
	const float rightMargin = ThemeManager::Constants::DOCKED_RIGHT_MARGIN * uiScale;

	// Get window position and calculate title bar area
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	float titleBarHeight = ImGui::GetFrameHeight();

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
		if (it->texture) {
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
			fgDrawList->AddImage(it->texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintColor);
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
	const float iconSize = GetHeaderIconSize(uiScale);
	const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * uiScale;
	const float imageExtent = std::max(0.0f, iconSize - paddingReduction);
	const ImVec2 imageSize(imageExtent, imageExtent);

	// Setup button styling for transparent background with hover effects
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GetUndockedIconSpacing(uiScale), 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);                                                           // Remove button borders
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
		if (!icon.texture) {
			continue;
		}

		std::string buttonId = std::format("##{}", icon.id);

		// Use ImageButton with reduced image size to minimize padding
		const bool clicked = icon.flashId ?
		                         Util::ImageButtonWithFlash(icon.flashId, icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor) :
		                         ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor);
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
	ImGui::PopStyleVar(2);    // Pop both style variables: ItemSpacing and FrameBorderSize
}

void MenuHeaderRenderer::RenderSteamVRDockHandle(float uiScale, ImGuiID dockSpaceId)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window)
		return;

	const float handleSize = GetHeaderIconSize(uiScale);
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

void MenuHeaderRenderer::RenderSteamVRResizeHandles(float uiScale)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window || window->DockIsActive)
		return;

	const ImVec2 savedCursor = ImGui::GetCursorPos();
	const ImVec2 windowPos = window->Pos;
	const ImVec2 windowSize = window->Size;
	const float handleSize = std::max(ImGui::GetFontSize() * 1.15f, 18.0f) * uiScale;
	const float handleInset = std::max(ImGui::GetStyle().WindowBorderSize + 2.0f * uiScale, 2.0f * uiScale);
	const float minWidth = 420.0f * uiScale;
	const float minHeight = 320.0f * uiScale;

	auto drawResizeHandle = [&](const char* id, const ImVec2& min, bool topLeft) {
		ImGui::SetCursorScreenPos(min);
		ImGui::InvisibleButton(id, ImVec2(handleSize, handleSize));
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		if (hovered || active) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
		}

		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			ImVec2 newPos = window->Pos;
			ImVec2 newSize = window->Size;
			if (topLeft) {
				const float maxInwardX = std::max(0.0f, newSize.x - minWidth);
				const float maxInwardY = std::max(0.0f, newSize.y - minHeight);
				const float appliedX = std::min(delta.x, maxInwardX);
				const float appliedY = std::min(delta.y, maxInwardY);
				newPos.x += appliedX;
				newPos.y += appliedY;
				newSize.x -= appliedX;
				newSize.y -= appliedY;
				ImGui::SetWindowPos(window, newPos, ImGuiCond_Always);
			} else {
				newSize.x = std::max(minWidth, newSize.x + delta.x);
				newSize.y = std::max(minHeight, newSize.y + delta.y);
			}
			ImGui::SetWindowSize(window, newSize, ImGuiCond_Always);
		}

		ImVec4 colorVec = ImGui::GetStyleColorVec4(active ? ImGuiCol_ResizeGripActive : hovered ? ImGuiCol_ResizeGripHovered :
		                                                                                     ImGuiCol_ResizeGrip);
		colorVec.w = std::max(colorVec.w, hovered || active ? 0.95f : 0.75f);
		const ImU32 color = ImGui::GetColorU32(colorVec);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		if (topLeft) {
			drawList->AddTriangleFilled(min, ImVec2(min.x + handleSize, min.y), ImVec2(min.x, min.y + handleSize), color);
		} else {
			const ImVec2 br = ImVec2(min.x + handleSize, min.y + handleSize);
			drawList->AddTriangleFilled(br, ImVec2(br.x - handleSize, br.y), ImVec2(br.x, br.y - handleSize), color);
		}
	};

	drawResizeHandle("##SteamVRResizeTopLeft", ImVec2(windowPos.x + handleInset, windowPos.y + handleInset), true);
	drawResizeHandle("##SteamVRResizeBottomRight", ImVec2(windowPos.x + windowSize.x - handleSize - handleInset, windowPos.y + windowSize.y - handleSize - handleInset), false);
	ImGui::SetCursorPos(savedCursor);
}

void MenuHeaderRenderer::RenderStableHeader(const std::string& title, bool showLogo, const std::vector<ActionIcon>& actionIcons, float uiScale, const Menu::UIIcons& uiIcons)
{
	auto* menu = globals::menu;
	if (!menu)
		return;

	ImGuiStyle& style = ImGui::GetStyle();
	const float currentFontSize = ImGui::GetFontSize();
	const float baseIconSize = currentFontSize * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER;
	const float iconSize = baseIconSize * uiScale;
	const float textScaleFactor = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE * uiScale;
	const float paddingX = ThemeManager::Constants::CURSOR_POSITION_PADDING * uiScale;
	const float paddingY = style.FramePadding.y * 2.0f;
	const float iconSpacing = ThemeManager::Constants::UNDOCKED_ICON_ITEM_SPACING * uiScale;
	const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * uiScale;

	ImFont* titleFont = menu->GetFont(Menu::FontRole::Title);
	if (!titleFont) {
		titleFont = ImGui::GetFont();
	}
	const float titleFontSize = (titleFont ? titleFont->LegacySize : currentFontSize) * textScaleFactor;
	const ImVec2 titleSize = titleFont ? titleFont->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, title.c_str()) :
	                                     ImGui::CalcTextSize(title.c_str());

	const float logoAspectRatio = showLogo && uiIcons.logo.size.y > 0.0f ? uiIcons.logo.size.x / uiIcons.logo.size.y : 1.0f;
	const float logoWidth = showLogo ? iconSize * logoAspectRatio : 0.0f;
	const float titleGroupWidth = logoWidth + (showLogo ? style.ItemSpacing.x : 0.0f) + titleSize.x;
	const float iconsWidth = actionIcons.empty() ? 0.0f :
	                         (static_cast<float>(actionIcons.size()) * iconSize) +
	                             (static_cast<float>(actionIcons.size() - 1) * iconSpacing);

	const float headerHeight = std::max(iconSize, titleFontSize) + paddingY * 2.0f;
	const ImVec2 cursorStart = ImGui::GetCursorPos();
	const ImVec2 screenStart = ImGui::GetCursorScreenPos();
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float rightLimit = screenStart.x + availableWidth;
	const float iconStartX = rightLimit - paddingX - iconsWidth;
	const float titleAreaWidth = std::max(0.0f, availableWidth - iconsWidth - paddingX * 3.0f);

	float titleX = screenStart.x + paddingX;
	if (menu->GetTheme().CenterHeader && titleGroupWidth < titleAreaWidth) {
		titleX = screenStart.x + paddingX + (titleAreaWidth - titleGroupWidth) * 0.5f;
	}
	titleX = std::min(titleX, std::max(screenStart.x + paddingX, iconStartX - style.ItemSpacing.x - titleGroupWidth));
	const float centerY = screenStart.y + headerHeight * 0.5f;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImU32 logoTint = IM_COL32_WHITE;
	if (menu->GetSettings().Theme.UseMonochromeLogo) {
		logoTint = ImGui::GetColorU32(menu->GetSettings().Theme.Palette.Text);
	}

	if (showLogo && uiIcons.logo.texture) {
		const ImVec2 logoMin(titleX, centerY - iconSize * 0.5f);
		const ImVec2 logoMax(logoMin.x + logoWidth, logoMin.y + iconSize);
		drawList->AddImage(uiIcons.logo.texture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), logoTint);
		titleX = logoMax.x + style.ItemSpacing.x;
	}

	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
	const float titleClipMaxX = std::max(titleX, iconStartX - style.ItemSpacing.x);
	drawList->PushClipRect(
		ImVec2(titleX, screenStart.y),
		ImVec2(titleClipMaxX, screenStart.y + headerHeight),
		true);
	drawList->AddText(titleFont, titleFontSize, ImVec2(titleX, centerY - titleSize.y * 0.5f), textColor, title.c_str());
	drawList->PopClipRect();

	float iconX = iconStartX;
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];
		if (!icon.texture)
			continue;

		const ImVec2 buttonMin(iconX, centerY - iconSize * 0.5f);
		const ImVec2 buttonMax(buttonMin.x + iconSize, buttonMin.y + iconSize);
		const ImVec2 imageMin(buttonMin.x + paddingReduction * 0.5f, buttonMin.y + paddingReduction * 0.5f);
		const ImVec2 imageMax(buttonMax.x - paddingReduction * 0.5f, buttonMax.y - paddingReduction * 0.5f);

		ImGui::SetCursorScreenPos(buttonMin);
		ImGui::PushID(static_cast<int>(i));
		const bool clicked = ImGui::InvisibleButton("##StableHeaderAction", ImVec2(iconSize, iconSize));
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
		drawList->AddImage(icon.texture, imageMin, imageMax, ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(tintColor));
		ImGui::PopID();

		iconX += iconSize + iconSpacing;
	}

	ImGui::SetCursorPos(ImVec2(cursorStart.x, cursorStart.y + headerHeight));
}
