#include "FeatureListRenderer.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/SettingsPage.h"
#include "Menu/StabilizerPage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <d3d11.h>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>

#include "Feature.h"
#include "FeatureConstraints.h"
#include "FeatureIssues.h"
#include "Features/CSEditor.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/Wetterness.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/ProfilingRenderer.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Util.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

namespace
{
	constexpr const char* PERFORMANCE_TUNING_MENU_NAME = "Performance Tuning";
	// Core built-in menu names that always appear before the feature list.
	constexpr std::array<const char*, 6> CORE_MENU_NAMES = { "Home", "General", "Advanced", "Profiling", PERFORMANCE_TUNING_MENU_NAME, "Display" };

	constexpr float sidebarTextScale = 14.0f / 12.0f;
	struct SidebarFontScope
	{
		MenuFonts::FontRoleGuard role{ Menu::FontRole::Subheading };
		SidebarFontScope() { ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * sidebarTextScale); }
		~SidebarFontScope() { ImGui::PopFont(); }
	};

	constexpr float featureDescriptionScale = 1.4f;
	constexpr float footerTextScale = 14.0f / 12.0f;
	constexpr float footerButtonPadding = 1.85f;
	constexpr float footerButtonHeight = 2.1f;
	constexpr float footerStatusHeight = 2.5f;
	constexpr float footerVerticalPadding = .4f;
	constexpr float footerStackGap = .5f;
	constexpr float footerOpacity = MenuUI::SettingsSurfaceOpacityScale;

	float SettingsActionTextWidth(const char* label)
	{
		return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * footerTextScale, FLT_MAX, 0, label).x;
	}

	std::array<const char*, 3> SettingsActionLabels(const MenuUI::SettingsFooter* external = nullptr, float availableWidth = 0)
	{
		const auto labels = external ? std::array{ external->actions[0].label, external->actions[1].label, external->actions[2].label } : std::array{ "Save settings", "Load saved", "Restore defaults" };
		const float fullWidth = SettingsActionTextWidth(labels[0]) + SettingsActionTextWidth(labels[1]) + SettingsActionTextWidth(labels[2]) +
		                        ImGui::GetFontSize() * footerButtonPadding * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
		if ((availableWidth > 0 ? availableWidth : ImGui::GetContentRegionAvail().x) >= fullWidth)
			return labels;
		if (!external)
			return { "Save", "Load", "Defaults" };
		auto compact = labels;
		for (size_t index = 0; index < compact.size(); ++index)
			if (external->actions[index].compactLabel)
				compact[index] = external->actions[index].compactLabel;
		return compact;
	}

	float SettingsActionButtonWidth(size_t index, const MenuUI::SettingsFooter* external = nullptr, float availableWidth = 0)
	{
		const float width = SettingsActionTextWidth(SettingsActionLabels(external, availableWidth)[index]) + ImGui::GetFontSize() * footerButtonPadding;
		return std::min(width, std::max(1.0f, availableWidth > 0 ? availableWidth : ImGui::GetContentRegionAvail().x));
	}

	bool SettingsActionButton(size_t a_index, const MenuUI::SettingsFooter* external = nullptr, float availableWidth = 0)
	{
		const auto* label = SettingsActionLabels(external, availableWidth)[a_index];
		const float font = ImGui::GetFontSize();
		const float width = SettingsActionButtonWidth(a_index, external, availableWidth);
		constexpr std::array glyphs{ Util::ActionGlyph::SaveSettings, Util::ActionGlyph::LoadSettings, Util::ActionGlyph::RestoreDefaults };
		ImGui::PushFont(ImGui::GetFont(), font * footerTextScale);
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 1, .5f });
		const SKSE::stl::scope_exit restoreAlign([] { ImGui::PopStyleVar(); ImGui::PopFont(); });
		const bool clicked = ImGui::Button(label, { width, font * footerButtonHeight });
		const auto minimum = ImGui::GetItemRectMin();
		const float extent = font;
		const float y = minimum.y + (ImGui::GetItemRectSize().y - extent) * .5f;
		Util::DrawActionGlyph(ImGui::GetWindowDrawList(), external && a_index == 2 ? Util::ActionGlyph::LoadSettings : glyphs[a_index],
			{ minimum.x + font * .3f, y }, { minimum.x + font * .3f + extent, y + extent }, ImGui::GetColorU32(ImGuiCol_Text));
		return clicked;
	}

	struct SettingsFooterLayout
	{
		float backgroundLeft;
		float backgroundRight;
		float contentWidth;
		float actionsLeft;
		float statusLeft;
		float statusWidth;
		float actionsHeight;
		int actionRows;
		std::array<ImVec2, 3> actionOffsets;
		bool stacked;
	};

	SettingsFooterLayout GetSettingsFooterLayout(const char* a_page, const MenuUI::SettingsFooter* external = nullptr)
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const float font = ImGui::GetFontSize();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		constexpr size_t count = 3;
		const auto cardInsets = MenuUI::SettingsPage::OverviewCardInsets(a_page, width);
		const float backgroundLeft = std::min(cardInsets.x, std::max(0.0f, width - cardInsets.y - font * 4));
		const float gutter = std::min(font * .75f, (width - backgroundLeft - cardInsets.y) * .05f);
		const float statusLeft = backgroundLeft + gutter;
		const float right = width - cardInsets.y - gutter;
		const float contentWidth = std::max(1.0f, right - statusLeft);
		std::array<float, 3> buttonWidths{};
		float actionsWidth = spacing * (count - 1);
		for (size_t index = 0; index < count; ++index) {
			buttonWidths[index] = SettingsActionButtonWidth(index, external, contentWidth);
			actionsWidth += buttonWidths[index];
		}
		const bool wrapActions = actionsWidth > contentWidth;
		const float inset = wrapActions ? statusLeft : std::clamp(MenuUI::SettingsPage::OverviewLastColumnInset(a_page, width), statusLeft, std::max(statusLeft, right - actionsWidth));
		const bool stacked = inset - statusLeft < font * 12;
		const float statusWidth = stacked ? contentWidth : inset - statusLeft - font;
		const float actionSpacing = spacing + (wrapActions ? 0 : std::max(0.0f, right - inset - actionsWidth) / (count - 1));
		std::array<ImVec2, 3> offsets{};
		float x = 0, y = 0;
		int rows = 1;
		for (size_t index = 0; index < count; ++index) {
			if (index && x + buttonWidths[index] > right - inset + .01f) {
				x = 0;
				y += font * footerButtonHeight + ImGui::GetStyle().ItemSpacing.y;
				++rows;
			}
			offsets[index] = { x, y };
			x += buttonWidths[index] + actionSpacing;
		}
		return { backgroundLeft, cardInsets.y, contentWidth, inset, statusLeft, std::max(1.0f, statusWidth), y + font * footerButtonHeight, rows, offsets, stacked };
	}

	float SettingsFooterContentHeight(const SettingsFooterLayout& layout)
	{
		const float statusHeight = ImGui::GetFontSize() * footerStatusHeight;
		return layout.stacked ? layout.actionsHeight + ImGui::GetFontSize() * footerStackGap + statusHeight : std::max(layout.actionsHeight, statusHeight);
	}

	float SettingsFooterHeight(const char* a_page, const MenuUI::SettingsFooter* external = nullptr)
	{
		return 1 + ImGui::GetFontSize() * footerVerticalPadding * 2 + SettingsFooterContentHeight(GetSettingsFooterLayout(a_page, external));
	}

	void DrawExternalAction(size_t index, const MenuUI::SettingsFooter& footer, float availableWidth)
	{
		const auto& action = footer.actions[index];
		ImGui::PushID(static_cast<int>(index));
		const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
		{
			const auto disabled = Util::DisableGuard(!action.enabled);
			if (SettingsActionButton(index, &footer, availableWidth)) {
				if (action.confirmation)
					ImGui::OpenPopup("Confirm INI action");
				else if (action.invoke)
					action.invoke();
			}
		}
		Util::AddTooltip(action.help);
		if (auto popup = Util::CenteredPopupModal("Confirm INI action")) {
			ImGui::TextWrapped("%s", action.confirmation ? action.confirmation : "Read the installed INI?");
			{
				const auto disabled = Util::DisableGuard(!action.enabled);
				if (ImGui::Button("Confirm")) {
					if (action.invoke)
						action.invoke();
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
		}
	}

	void DrawSettingsFooter(const char* a_page, const std::function<void(float)>& a_defaults = {}, const MenuUI::SettingsFooter* external = nullptr)
	{
		const auto footerStart = ImGui::GetCursorScreenPos();
		const auto layout = GetSettingsFooterLayout(a_page, external);
		const float font = ImGui::GetFontSize();
		const float height = SettingsFooterHeight(a_page, external);
		const auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const bool light = background.x + background.y + background.z > 1.5f;
		const ImVec4 lift = light ? ImVec4(-.02f, -.02f, -.02f, 0) : ImVec4(.09f, .105f, .11f, 0);
		const ImVec4 footerColor{ std::clamp(background.x + lift.x, 0.0f, 1.0f), std::clamp(background.y + lift.y, 0.0f, 1.0f), std::clamp(background.z + lift.z, 0.0f, 1.0f), footerOpacity };
		ImGui::GetWindowDrawList()->AddRectFilled({ footerStart.x + layout.backgroundLeft, footerStart.y },
			{ footerStart.x + ImGui::GetContentRegionAvail().x - layout.backgroundRight, footerStart.y + height }, ImGui::GetColorU32(footerColor));
		ImGui::GetWindowDrawList()->AddLine({ footerStart.x + layout.backgroundLeft, footerStart.y },
			{ footerStart.x + ImGui::GetContentRegionAvail().x - layout.backgroundRight, footerStart.y }, ImGui::GetColorU32(ImGuiCol_Separator));
		const float contentHeight = SettingsFooterContentHeight(layout);
		const ImVec2 start{ footerStart.x, footerStart.y + 1 + font * footerVerticalPadding };
		const float actionsY = start.y + (layout.stacked ? 0 : (contentHeight - layout.actionsHeight) * .5f);
		const ImVec2 statusStart{ start.x + layout.statusLeft, start.y + (layout.stacked ? layout.actionsHeight + font * footerStackGap : (contentHeight - font * footerStatusHeight) * .5f) };
		const auto& menu = *globals::menu;
		const auto& palette = menu.GetTheme().StatusPalette;
		const bool error = external ? external->error : menu.IsSettingsSaveMessageError() && !menu.GetSettingsSaveMessage().empty();
		const bool dirty = external ? external->dirty : menu.HasUnsavedSettings();
		const char* status = external ? external->status.c_str() : error ? "Settings not saved" :
		                                                       dirty     ? "Unsaved changes" :
		                                                                   "Settings saved";
		const std::string detail = external ? external->detail : error ? menu.GetSettingsSaveMessage() :
		                                                     dirty     ? "Save to keep your current choices." :
		                                                                 "Your choices are up to date.";
		const auto statusColor = error ? palette.Error : dirty ? palette.Warning :
		                                                         Util::Color::SecondaryText();
		auto* draw = ImGui::GetWindowDrawList();
		{
			draw->PushClipRect({ statusStart.x - font * .2f, statusStart.y }, { statusStart.x + layout.statusWidth, statusStart.y + font * footerStatusHeight }, true);
			const SKSE::stl::scope_exit restoreClip([draw] { draw->PopClipRect(); });
			const ImVec2 marker{ statusStart.x + font * .35f, statusStart.y + font * 1.25f };
			draw->AddCircleFilled(marker, font * .52f, ImGui::GetColorU32({ statusColor.x, statusColor.y, statusColor.z, .10f }));
			draw->AddCircleFilled(marker, font * .26f, ImGui::GetColorU32(statusColor));
			draw->AddText(nullptr, font * footerTextScale, { statusStart.x + font, statusStart.y + font * .1f }, ImGui::GetColorU32(statusColor), status);
			draw->AddText(nullptr, font * .95f, { statusStart.x + font, statusStart.y + font * 1.5f }, ImGui::GetColorU32(Util::Color::SecondaryText()), detail.c_str());
		}
		ImGui::SetCursorScreenPos(statusStart);
		ImGui::Dummy({ layout.statusWidth, font * footerStatusHeight });
		Util::AddTooltip(detail.c_str());
		const char* blockedHelp = external ? nullptr : PerformanceTuningRenderer::HasActiveMeasurements() ? "Finish or cancel the performance comparison before saving, loading or restoring settings." :
		                                           globals::state->IsPersistentMutationBlocked()          ? "Wait for the current game save or load to finish before changing settings." :
		                                                                                                    nullptr;
		const auto actionsGuard = Util::DisableGuard(blockedHelp != nullptr);
		const auto addActionTooltip = [&](const char* help) {
			Util::AddTooltip(blockedHelp ? blockedHelp : help, ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
		};
		ImGui::SetCursorScreenPos({ start.x + layout.actionsLeft, actionsY });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { font * .35f, font * .35f });
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, font * .3f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
		const SKSE::stl::scope_exit restoreStyle([] { ImGui::PopStyleVar(3); });
		{
			const auto primary = Util::StyledButtonWrapper(palette.InfoColor, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			const auto accent = palette.InfoColor;
			const float brightness = accent.x * .299f + accent.y * .587f + accent.z * .114f;
			ImGui::PushStyleColor(ImGuiCol_Text, brightness > .55f ? ImVec4(.08f, .08f, .08f, 1) : ImVec4(1, 1, 1, 1));
			const SKSE::stl::scope_exit restoreText([] { ImGui::PopStyleColor(); });
			if (external)
				DrawExternalAction(0, *external, layout.contentWidth);
			else if (SettingsActionButton(0, nullptr, layout.contentWidth))
				globals::state->Save();
		}
		if (!external)
			addActionTooltip("Save all your CSX settings.");
		ImGui::SetCursorScreenPos({ start.x + layout.actionsLeft + layout.actionOffsets[1].x, actionsY + layout.actionOffsets[1].y });
		{
			const float contrast = light ? -.04f : .05f;
			const ImVec4 secondary{ std::clamp(footerColor.x + contrast, 0.0f, 1.0f), std::clamp(footerColor.y + contrast, 0.0f, 1.0f), std::clamp(footerColor.z + contrast, 0.0f, 1.0f), footerOpacity };
			const auto secondaryStyle = Util::StyledButtonWrapper(secondary, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (external)
				DrawExternalAction(1, *external, layout.contentWidth);
			else if (SettingsActionButton(1, nullptr, layout.contentWidth)) {
				globals::state->Load();
				globals::features::llf::particleLights.GetConfigs();
			}
		}
		if (!external)
			addActionTooltip("Replace current changes with your saved CSX settings.");
		ImGui::SetCursorScreenPos({ start.x + layout.actionsLeft + layout.actionOffsets[2].x, actionsY + layout.actionOffsets[2].y });
		{
			const auto outlineStyle = Util::StyledButtonWrapper({ 0, 0, 0, 0 }, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (external)
				DrawExternalAction(2, *external, layout.contentWidth);
			else if (a_defaults) {
				a_defaults(layout.contentWidth);
				if (blockedHelp)
					addActionTooltip(blockedHelp);
			} else {
				if (SettingsActionButton(2, nullptr, layout.contentWidth)) {
					std::string restoreError;
					if (!globals::state->RestoreDefaultSettings(restoreError))
						globals::menu->ReportSettingsSaveResult(false, std::move(restoreError));
				}
				addActionTooltip("Restore installed CSX defaults for the menu, shaders and loaded features. Save to keep these changes.");
			}
		}
		ImGui::SetCursorScreenPos({ footerStart.x, footerStart.y + height });
		ImGui::Dummy({ 0, 0 });
	}

	struct FeatureBannerTexture
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		ImVec2 size = { 0.0f, 0.0f };
		std::filesystem::path sourcePath;
		bool loadAttempted = false;
	};

	FeatureBannerTexture* TryGetFeatureBannerTexture(Feature* feature)
	{
		if (!feature)
			return nullptr;

		const auto relativePath = feature->GetSettingsBannerAssetPath();
		if (relativePath.empty())
			return nullptr;

		static std::unordered_map<std::string, FeatureBannerTexture> bannerCache;
		auto& banner = bannerCache[feature->GetShortName()];

		const auto sourcePath = Util::PathHelpers::GetCommunityShaderPath() / std::string(relativePath);
		if (!banner.loadAttempted || banner.sourcePath != sourcePath) {
			if (!globals::d3d::device)
				return nullptr;

			banner = {};
			banner.sourcePath = sourcePath;
			banner.loadAttempted = true;

			ID3D11ShaderResourceView* loadedSrv = nullptr;
			ImVec2 loadedSize = {};
			const auto sourcePathString = sourcePath.string();
			if (Util::LoadTextureFromFile(globals::d3d::device, sourcePathString.c_str(), &loadedSrv, loadedSize)) {
				banner.srv.Attach(loadedSrv);
				banner.size = loadedSize;
			}
		}

		return banner.srv.Get() ? &banner : nullptr;
	}

	void DrawFeatureBanner(Feature* feature)
	{
		auto* banner = TryGetFeatureBannerTexture(feature);
		if (!banner || !banner->srv.Get() || banner->size.x <= 0.0f || banner->size.y <= 0.0f)
			return;

		const float availableWidth = ImGui::GetContentRegionAvail().x;
		if (availableWidth <= 0.0f)
			return;

		const float aspectRatio = banner->size.y / banner->size.x;
		const ImVec2 drawSize(availableWidth, std::round(availableWidth * aspectRatio));
		ImGui::Image(banner->srv.Get(), drawSize);
	}

	bool IsCoreMenu(const std::string& menuName)
	{
		return std::find(CORE_MENU_NAMES.begin(), CORE_MENU_NAMES.end(), menuName) != CORE_MENU_NAMES.end();
	}

	bool IsFeatureVisible(Feature* feature)
	{
		return feature &&
		       !feature->IsHiddenFromUserView() &&
		       feature->IsInMenu();
	}

	bool IsPerformanceMeasurementNavigationLocked(size_t listId, size_t selectedMenu)
	{
		return PerformanceTuningRenderer::HasActiveMeasurements() && listId != selectedMenu;
	}

	const char* BuiltInPageId(const FeatureListRenderer::BuiltInMenu& menu)
	{
		return menu.name == PERFORMANCE_TUNING_MENU_NAME ? "PerformanceTuning" : menu.name.c_str();
	}

	std::string GetSelectableMenuEntryId(const FeatureListRenderer::MenuFuncInfo& menuInfo)
	{
		if (const auto* menu = std::get_if<FeatureListRenderer::BuiltInMenu>(&menuInfo))
			return "builtin:" + menu->name;

		if (const auto* feature = std::get_if<Feature*>(&menuInfo); feature && *feature)
			return "feature:" + (*feature)->GetShortName();

		return {};
	}

	bool TrySelectMenuEntryById(
		const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList,
		const std::string& menuEntryId,
		size_t& selectedMenu)
	{
		if (menuEntryId.empty())
			return false;

		for (size_t i = 0; i < menuList.size(); ++i) {
			if (GetSelectableMenuEntryId(menuList[i]) == menuEntryId) {
				selectedMenu = i;
				return true;
			}
		}

		return false;
	}

	bool IsSelectableMenuEntry(const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList, size_t selectedMenu)
	{
		return selectedMenu < menuList.size() && !GetSelectableMenuEntryId(menuList[selectedMenu]).empty();
	}

	void SelectFallbackMenuEntry(const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList, size_t& selectedMenu)
	{
		if (IsSelectableMenuEntry(menuList, selectedMenu))
			return;

		if (TrySelectMenuEntryById(menuList, "builtin:Home", selectedMenu))
			return;

		for (size_t i = 0; i < menuList.size(); ++i) {
			if (!GetSelectableMenuEntryId(menuList[i]).empty()) {
				selectedMenu = i;
				return;
			}
		}

		selectedMenu = 0;
	}

	/**
	 * @brief Determines if the left feature panel should be visible based on auto-hide settings and mouse position
	 * @return true if panel should be visible, false if it should be hidden
	 */
	std::optional<bool> sidebarVisibilityOverride;
	bool sidebarVisible = true;

	bool ShouldShowLeftPanel()
	{
		if (sidebarVisibilityOverride.has_value())
			return *sidebarVisibilityOverride;

		bool autoHideEnabled = globals::menu->GetSettings().AutoHideFeatureList;
		static bool leftPanelVisible = true;
		static float hoverStartTime = 0.0f;
		static bool wasHovering = false;

		if (!autoHideEnabled) {
			leftPanelVisible = true;
			return true;
		}

		// Get mouse position and window bounds
		ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();
		float currentTime = static_cast<float>(ImGui::GetTime());

		// Use constants for auto-hide behavior
		const float activationZoneWidth = ThemeManager::Constants::AUTOHIDE_ACTIVATION_ZONE_WIDTH;
		const float expandDelay = ThemeManager::Constants::AUTOHIDE_EXPAND_DELAY;
		const float panelWidth = windowSize.x * ThemeManager::Constants::AUTOHIDE_PANEL_WIDTH_RATIO;

		// Calculate relative X position
		const float relativeX = mousePos.x - windowPos.x;

		// For activation: only check if mouse is at left edge (allow any Y position for easier triggering)
		// Prevent negative X from triggering, but don't restrict Y-axis for activation
		bool mouseInActivationZone = relativeX >= 0.0f && relativeX < activationZoneWidth;

		// For staying visible: check both X and Y to ensure mouse is actually over the panel area
		const bool mouseOverPanelX = relativeX >= 0.0f && relativeX < panelWidth;
		const bool mouseOverPanelY = mousePos.y >= windowPos.y && mousePos.y <= (windowPos.y + windowSize.y);
		bool mouseOverPanel = leftPanelVisible && mouseOverPanelX && mouseOverPanelY;

		// Track hover start time
		if (mouseInActivationZone && !wasHovering) {
			hoverStartTime = currentTime;
			wasHovering = true;
		} else if (!mouseInActivationZone) {
			wasHovering = false;
		}

		// Expand only after delay has elapsed
		bool shouldExpand = mouseInActivationZone && (currentTime - hoverStartTime >= expandDelay);

		// Update visibility: expand with delay, or stay visible while mouse is over panel
		if (shouldExpand || mouseOverPanel) {
			leftPanelVisible = true;
		} else if (!mouseOverPanel && !mouseInActivationZone) {
			leftPanelVisible = false;
		}

		return leftPanelVisible;
	}

	void SeparatorTextWithFont(const char* text, Menu::FontRole role)
	{
		MenuFonts::FontRoleGuard guard(role);
		ImGui::SeparatorText(text);
	}

	void SeparatorTextWithFont(const std::string& text, Menu::FontRole role)
	{
		SeparatorTextWithFont(text.c_str(), role);
	}

	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}

	/**
	 * @brief Draws a feature header with the feature name in large text
	 * @param featureName The display name of the feature
	 * @param description Short description shown below the title
	 * @return The height of just the title line (for button alignment)
	 */
	float DrawFeatureHeader(const std::string& featureName, const std::string& description = "", float reservedRightWidth = 0.0f)
	{
		auto& themeSettings = globals::menu->GetTheme();
		auto& featureHeading = themeSettings.FeatureHeading;
		const ImGuiStyle& style = ImGui::GetStyle();
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float maxReservedWidth = std::max(0.0f, availableWidth - ImGui::GetFrameHeight());
		const float reservedWidth = std::clamp(reservedRightWidth, 0.0f, maxReservedWidth);
		const float textWidth = std::max(ImGui::GetFrameHeight(), availableWidth - reservedWidth - style.ItemSpacing.x);
		const float wrapPosX = ImGui::GetCursorPosX() + textWidth;

		// Sanitize and clamp to UI slider range to prevent malformed theme JSON from destabilizing layout
		float titleScale = featureHeading.FeatureTitleScale;
		if (!std::isfinite(titleScale)) {
			titleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
		}
		titleScale = std::clamp(titleScale, 1.0f, 3.0f);

		const float titleStartY = ImGui::GetCursorPosY();
		{
			MenuFonts::FontRoleGuard titleGuard(Menu::FontRole::Title);
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * titleScale * (4.0f / 3.0f));
			ImGui::PushTextWrapPos(wrapPosX);
			const SKSE::stl::scope_exit restoreTitle([] { ImGui::PopTextWrapPos(); ImGui::PopFont(); });
			ImGui::TextUnformatted(featureName.c_str());
		}

		// Store the title-only height for return value
		float titleOnlyHeight = ImGui::GetCursorPosY() - titleStartY;

		// Draw description if provided (wrapped to content width)
		if (!description.empty()) {
			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * featureDescriptionScale);
			const SKSE::stl::scope_exit restoreDescriptionFont([] { ImGui::PopFont(); });
			const auto descColor = Util::Color::SecondaryText();
			ImGui::PushStyleColor(ImGuiCol_Text, descColor);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availableWidth);
			ImGui::TextUnformatted(description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		ImGui::Dummy({ 0, ImGui::GetTextLineHeight() * .7f });

		return titleOnlyHeight;
	}

	// ---------------------------------------------------------------------------
	// Persistent state for the reactive constraint warning popup.
	// DrawMenuVisitor is reconstructed every frame (it's a temporary passed to
	// std::visit), so member state is lost immediately.  These file-scope
	// variables survive across frames so the popup can actually render.
	// ---------------------------------------------------------------------------

	// Set of constraint keys we have already "seen" (and therefore warned about
	// or suppressed).  Keyed as "featureShortName|settingPath".
	std::unordered_set<std::string> g_knownConstraintKeys;
	bool g_knownConstraintKeysInitialised = false;

	// Pending popup state: non-empty when we have new constraints to show.
	bool g_reactiveWarningShow = false;
	std::vector<std::pair<FeatureConstraints::SettingId, FeatureConstraints::ConstraintResult>> g_reactiveWarningConstraints;

	// "Don't show again" checkbox state inside the modal (reset each time popup opens).
	bool g_dontShowAgainCheckbox = false;
}

std::vector<Feature*> FeatureListRenderer::GetMenuFeatures()
{
	auto features = Feature::GetFeatureList();
	if (REL::Module::IsVR())
		features.push_back(&MenuUI::StabilizerPage::Get());
	return features;
}

void FeatureListRenderer::SetSidebarVisible(bool a_visible)
{
	sidebarVisibilityOverride = a_visible;
	sidebarVisible = a_visible;
}

bool FeatureListRenderer::IsSidebarVisible() { return sidebarVisible; }

void FeatureListRenderer::ResetSidebarVisibility() { sidebarVisibilityOverride.reset(); }

void FeatureListRenderer::RenderFeatureList(
	float footerHeight,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	ImGui::BeginChild("Menus Table", ImVec2(0, -footerHeight));

	if (!pendingFeatureSelection.empty()) {
		featureSearch.clear();
		for (auto* feature : GetMenuFeatures()) {
			if (feature->GetShortName() == pendingFeatureSelection) {
				categoryExpansionStates[std::string(feature->GetCategory())] = true;
				break;
			}
		}
	}

	static std::string selectedMenuEntryId;
	auto menuList = BuildMenuList(featureSearch, categoryExpansionStates, drawGeneralSettings, drawAdvancedSettings);
	if (!selectedMenuEntryId.empty() && !TrySelectMenuEntryById(menuList, selectedMenuEntryId, selectedMenu))
		selectedMenu = menuList.size();

	HandlePendingFeatureSelection(pendingFeatureSelection, menuList, selectedMenu);
	SelectFallbackMenuEntry(menuList, selectedMenu);

	// Determine if left panel should be visible based on auto-hide settings
	const bool leftPanelVisible = sidebarVisible = ShouldShowLeftPanel();

	// Create the table with appropriate number of columns based on visibility
	int numColumns = leftPanelVisible ? 2 : 1;
	if (ImGui::BeginTable(leftPanelVisible ? "Menus Table" : "Menus Table Hidden", numColumns, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
		if (leftPanelVisible) {
			ImGui::TableSetupColumn("##ListOfMenus", 0, 2.4f);
			ImGui::TableSetupColumn("##MenuConfig", 0, 7.6f);
			RenderLeftColumn(menuList, selectedMenu, featureSearch, categoryExpansionStates);
			RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection);
		} else {
			// When left panel is hidden, right column takes full width
			ImGui::TableSetupColumn("##MenuConfig", 0, 1);
			RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection);
		}

		ImGui::EndTable();
	}

	selectedMenuEntryId = IsSelectableMenuEntry(menuList, selectedMenu) ? GetSelectableMenuEntryId(menuList[selectedMenu]) : std::string{};

	ImGui::EndChild();
}

std::vector<FeatureListRenderer::MenuFuncInfo> FeatureListRenderer::BuildMenuList(
	const std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	// Build the menu list
	auto sortedFeatureList = GetMenuFeatures();  // need a copy so the load order is not lost
	std::ranges::sort(sortedFeatureList, [](Feature* a, Feature* b) {
		return a->GetDisplayName() < b->GetDisplayName();
	});

	// Filter features by search string
	if (!featureSearch.empty()) {
		auto it = std::remove_if(sortedFeatureList.begin(), sortedFeatureList.end(),
			[&featureSearch](Feature* feat) { return !Util::FeatureMatchesSearch(feat, featureSearch); });
		sortedFeatureList.erase(it, sortedFeatureList.end());
	}

	auto menuList = std::vector<MenuFuncInfo>{
		BuiltInMenu{ "Home", []() { HomePageRenderer::RenderHomePage(); } },
		BuiltInMenu{ PERFORMANCE_TUNING_MENU_NAME, []() {
						PerformanceTuningRenderer::Render();
					} }
	};  // NOTE: The menu list is rebuilt every frame, so category expansion states
	// persist correctly. This is acceptable since the list is small and built
	// infrequently, but could be optimized if performance becomes an issue.

	menuList.insert(menuList.begin() + 1, BuiltInMenu{ "Profiling", []() {
														  MenuUI::SettingsPage page("Profiling", { { "timings", "Timings", "Enable profiling, then inspect CPU and GPU work.", "Live CPU and GPU timings", true, true, "Choose a timing view" } });
														  if (page.Is("timings"))
															  ProfilingRenderer::RenderStatistics(true, true, false);
													  } });

	{
		menuList.insert(menuList.begin() + 1, BuiltInMenu{ "General", drawGeneralSettings });
		menuList.insert(menuList.begin() + 2, BuiltInMenu{ "Advanced", drawAdvancedSettings });
	}

	// Group features by category
	std::map<std::string, std::vector<Feature*>> categorizedFeatures;
	for (Feature* feat : sortedFeatureList) {
		if (IsFeatureVisible(feat) && (feat->loaded || dynamic_cast<MenuUI::ExternalSettingsPage*>(feat))) {
			std::string category(feat->GetCategory());
			categorizedFeatures[category].push_back(feat);
		}
	}

	const std::string utilityCategory(FeatureCategories::kUtility);
	if (categoryExpansionStates.find(utilityCategory) == categoryExpansionStates.end()) {
		categoryExpansionStates[utilityCategory] = true;
	}

	// Sort features within each category
	for (auto& [category, features] : categorizedFeatures) {
		std::ranges::sort(features, [](Feature* a, Feature* b) {
			return a->GetDisplayName() < b->GetDisplayName();
		});
	}

	// Add categorized features to menu with collapsible headers
	for (const auto categoryName : FeatureCategories::kMenuOrder) {
		const std::string category(categoryName);
		if (categorizedFeatures.find(category) != categorizedFeatures.end() && !categorizedFeatures[category].empty()) {
			// Initialize expansion state if not exists
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = true;  // Default to expanded
			}

			// Add category header
			menuList.push_back(CategoryHeader{ category, static_cast<int>(categorizedFeatures[category].size()) });

			// Add features only if category is expanded
			if (categoryExpansionStates[category]) {
				std::ranges::copy(categorizedFeatures[category], std::back_inserter(menuList));
			}
		}
	}

	// Add any categories not in the predefined order
	for (const auto& [category, features] : categorizedFeatures) {
		const bool isKnownCategory =
			std::ranges::find(FeatureCategories::kMenuOrder, std::string_view{ category }) !=
			FeatureCategories::kMenuOrder.end();
		if (!isKnownCategory && !features.empty()) {
			// Initialize expansion state if not exists
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = true;  // Default to expanded
			}

			// Add category header
			menuList.push_back(CategoryHeader{ category, static_cast<int>(features.size()) });

			// Add features only if category is expanded
			if (categoryExpansionStates[category]) {
				std::ranges::copy(features, std::back_inserter(menuList));
			}
		}
	}

	auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
		return IsFeatureVisible(feat) && !feat->loaded && !dynamic_cast<MenuUI::ExternalSettingsPage*>(feat) &&
		       (!FeatureIssues::IsObsoleteFeature(feat->GetShortName()) || globals::state->IsDeveloperMode());
	});
	if (std::ranges::distance(unloadedFeatures) != 0) {
		menuList.push_back("Unloaded Features"s);
		std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
	}
	// Add top section for feature issues (rejected features, obsolete info, etc.)
	if (FeatureIssues::HasFeatureIssues()) {
		menuList.insert(menuList.begin(), BuiltInMenu{ "Feature Issues", []() {
														  MenuUI::SettingsPage page("Feature Issues", { { "review", "Review", "Resolve unavailable or incompatible feature files.", "Feature status and recovery", true, true, "Review and resolve issues" } });
														  if (page.Is("review"))
															  FeatureIssues::DrawFeatureIssuesUI();
													  } });
	}

	return menuList;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
bool FeatureListRenderer::TryQueueBuiltInPage(const std::string& a_page)
{
	std::map<std::string, bool> categories;
	for (const auto& entry : BuildMenuList({}, categories, {}, {})) {
		const auto* menu = std::get_if<BuiltInMenu>(&entry);
		if (menu && a_page == BuiltInPageId(*menu)) {
			globals::menu->SelectFeatureMenu(GetSelectableMenuEntryId(entry));
			return true;
		}
	}
	return false;
}
#endif

void FeatureListRenderer::HandlePendingFeatureSelection(
	std::string& pendingFeatureSelection,
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu)
{
	if (!pendingFeatureSelection.empty()) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (pendingFeatureSelection.starts_with("builtin:") && TrySelectMenuEntryById(menuList, pendingFeatureSelection, selectedMenu)) {
			pendingFeatureSelection.clear();
			return;
		}
#endif
		for (size_t i = 0; i < menuList.size(); ++i) {
			if (std::holds_alternative<Feature*>(menuList[i])) {
				Feature* feature = std::get<Feature*>(menuList[i]);
				if (feature->GetShortName() == pendingFeatureSelection) {
					if (feature == &globals::features::csEditor) {
						if (feature->loaded)
							CSEditor::OpenEditorWindow();
						break;
					}
					selectedMenu = i;
					logger::info("Navigated to {} feature menu", pendingFeatureSelection);
					break;
				}
			}
		}
		pendingFeatureSelection.clear();  // Clear after processing
	}
}

void FeatureListRenderer::RenderLeftColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates)
{
	ImGui::TableNextColumn();
	// Draw the feature list
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
	if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
		// First render the core built-in menus above the feature search.
		size_t renderedCoreMenus = 0;
		for (size_t i = 0; i < menuList.size() && renderedCoreMenus < CORE_MENU_NAMES.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (IsCoreMenu(menu.name)) {
					std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates }, menuList[i]);
					renderedCoreMenus++;
				}
			}
		}

		// Add Features header and search bar after built-in settings
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const float searchLeft = ImGui::GetCursorPosX() + [] {
			SidebarFontScope fontGuard;
			return ImGui::GetFontSize() * .3f;
		}();
		ImGui::SetCursorPosX(searchLeft);
		ImGui::TextColored(Util::Color::SecondaryText(), "FEATURES");
		ImGui::SetCursorPosX(searchLeft);
		Util::DrawFeatureSearchBar(featureSearch);

		// Then render the rest (features and categories, but skip already rendered core menus)
		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
				if (IsCoreMenu(menu.name)) {
					continue;  // Skip, already rendered
				}
			}
			std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates }, menuList[i]);
		}

		ImGui::EndListBox();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void FeatureListRenderer::RenderRightColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t selectedMenu,
	std::string& pendingFeatureSelection)
{
	ImGui::TableNextColumn();

	const auto* builtInMenu = selectedMenu < menuList.size() ? std::get_if<BuiltInMenu>(&menuList[selectedMenu]) : nullptr;
	const auto* feature = selectedMenu < menuList.size() ? std::get_if<Feature*>(&menuList[selectedMenu]) : nullptr;
	bool performancePanel = false;
	if (builtInMenu) {
		performancePanel = builtInMenu->name == PERFORMANCE_TUNING_MENU_NAME ?
		                       MenuUI::SettingsPage::Selected("PerformanceTuning") == "compare" :
		                       MenuUI::SettingsPage::Selected(builtInMenu->name.c_str()) == "performance";
	} else if (feature && *feature && (*feature)->loaded && (*feature)->SupportsPerformanceCostMeasurement()) {
		const auto name = (*feature)->GetShortName();
		performancePanel = !globals::state->IsFeatureDisabled(name) && MenuUI::SettingsPage::Selected(name.c_str()) == "performance";
	}
	if (!performancePanel)
		PerformanceTuningRenderer::NotifyOverviewInactive();

	if (selectedMenu < menuList.size()) {
		std::visit(DrawMenuVisitor{ pendingFeatureSelection }, menuList[selectedMenu]);
	} else {
		ImGui::TextDisabled("Please select an item on the left.");
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const BuiltInMenu& menu)
{
	SidebarFontScope fontGuard;
	constexpr std::array icons{
		std::pair{ "Home", Util::ActionGlyph::Home },
		std::pair{ "General", Util::ActionGlyph::General },
		std::pair{ "Advanced", Util::ActionGlyph::Advanced },
		std::pair{ "Profiling", Util::ActionGlyph::Profiling },
		std::pair{ "Performance Tuning", Util::ActionGlyph::PerformanceTuning }
	};
	const auto icon = std::ranges::find_if(icons, [&](const auto& entry) { return menu.name == entry.first; });
	const bool navigationLocked = IsPerformanceMeasurementNavigationLocked(listId, selectedMenuRef);
	const auto guard = Util::DisableGuard(navigationLocked);
	const float font = ImGui::GetFontSize();
	const auto start = ImGui::GetCursorScreenPos();
	if (ImGui::Selectable(("##" + menu.name).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns, { 0, ImGui::GetTextLineHeight() * 2 }))
		selectedMenuRef = listId;
	const auto maximum = ImGui::GetItemRectMax();
	const float middle = (start.y + maximum.y) * .5f;
	const float textLeft = start.x + font * 1.9f;
	const auto color = menu.name == "Feature Issues" ? globals::menu->GetTheme().StatusPalette.Error : ImGui::GetStyleColorVec4(ImGuiCol_Text);
	auto* draw = ImGui::GetWindowDrawList();
	if (icon != icons.end())
		Util::DrawActionGlyph(draw, icon->second, { start.x + font * .15f, middle - font * .75f }, { start.x + font * 1.65f, middle + font * .75f }, ImGui::GetColorU32(ImGuiCol_Text));
	{
		draw->PushClipRect({ textLeft, start.y }, maximum, true);
		const SKSE::stl::scope_exit restoreClip([draw] { draw->PopClipRect(); });
		draw->AddText({ textLeft, middle - ImGui::GetTextLineHeight() * .5f }, ImGui::GetColorU32(color), menu.name.c_str());
	}
	if (ImGui::CalcTextSize(menu.name.c_str()).x > maximum.x - textLeft)
		Util::AddTooltip(menu.name.c_str());
}

void FeatureListRenderer::ListMenuVisitor::operator()(const std::string& label)
{
	// Style "Unloaded Features" to match category headers
	if (label == "Unloaded Features") {
		Util::DrawSectionHeader(label.c_str(), true);
	} else {
		// Use default separator text for other labels - should be themed via ImGuiCol_Separator
		SeparatorTextWithFont(label, Menu::FontRole::Subheading);
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const CategoryHeader& header)
{
	// Get expansion state from static map
	bool isExpanded = categoryExpansionStates[header.name];

	// Draw category header with custom styling using util:UI function
	// Keep category labels at the same size as navigation and feature rows.
	{
		SidebarFontScope fontGuard;
		Util::DrawCategoryHeader(header.name.c_str(), isExpanded, header.count);
	}

	// Update expansion state
	categoryExpansionStates[header.name] = isExpanded;
}

void FeatureListRenderer::ListMenuVisitor::operator()(Feature* feat)
{
	SidebarFontScope fontGuard;

	auto* external = dynamic_cast<MenuUI::ExternalSettingsPage*>(feat);
	const auto featureName = feat->GetShortName();
	bool isDisabled = external ? !external->IsAvailable() : globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = external ? external->IsAvailable() : feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();
	auto& themeSettings = globals::menu->GetSettings().Theme;

	ImVec4 textColor;

	// Determine the text color based on the state
	if (isDisabled) {
		textColor = themeSettings.StatusPalette.Disable;
	} else if (isLoaded) {
		textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	} else if (hasFailedMessage) {
		textColor = feat->version.empty() ? themeSettings.StatusPalette.Disable : themeSettings.StatusPalette.Error;
	} else {
		// No failed message but not loaded - check if INI file exists
		if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
			// INI file missing - treat as missing feature (grey)
			textColor = themeSettings.StatusPalette.Disable;
		} else {
			// INI file exists but feature not loaded - truly pending restart (green)
			textColor = themeSettings.StatusPalette.RestartNeeded;
		}
	}

	ImGui::PushID(featureName.c_str());
	const bool navigationLocked = IsPerformanceMeasurementNavigationLocked(listId, selectedMenuRef);
	ImGui::BeginDisabled(navigationLocked);
	const auto rowStart = ImGui::GetCursorScreenPos();
	const float rowWidth = ImGui::GetContentRegionAvail().x;
	const float rowHeight = ImGui::GetTextLineHeight() * 2;
	const auto switchSize = Util::FeatureToggleSize();
	const float switchWidth = switchSize.x;
	const float switchLeft = rowStart.x + std::max(0.0f, rowWidth - switchWidth - ImGui::GetStyle().FramePadding.x);
	const float labelWidth = std::max(1.0f, switchLeft - rowStart.x - ImGui::GetStyle().ItemSpacing.x);
	const bool selected = selectedMenuRef == listId;
	if (selected) {
		auto* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(rowStart, { rowStart.x + rowWidth, rowStart.y + rowHeight }, ImGui::GetColorU32(ImGuiCol_Header));
		draw->AddRectFilled(rowStart, { rowStart.x + ImGui::GetStyle().FramePadding.x, rowStart.y + rowHeight }, ImGui::GetColorU32(themeSettings.StatusPalette.InfoColor), ImGui::GetStyle().FrameRounding);
	}

	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	if (feat == &globals::features::csEditor) {
		ImGui::SetCursorScreenPos({ rowStart.x, rowStart.y + std::max(0.0f, (rowHeight - ImGui::GetFrameHeight()) * .5f) });
		globals::features::csEditor.DrawLauncherButton(labelWidth);
	} else {
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0, .5f });
		const SKSE::stl::scope_exit restoreAlign([] { ImGui::PopStyleVar(); });
		if (ImGui::Selectable(fmt::format("    {} ", feat->GetDisplayName()).c_str(), false, ImGuiSelectableFlags_None, { labelWidth, rowHeight }))
			selectedMenuRef = listId;
	}
	ImGui::PopStyleColor();
	if (external ? external->HasUnsavedChanges() : globals::menu->HasUnsavedFeatureSettings(feat->GetName())) {
		const float radius = ImGui::GetFontSize() * .12f;
		ImGui::GetWindowDrawList()->AddCircleFilled({ switchLeft - ImGui::GetStyle().ItemSpacing.x * .5f, rowStart.y + rowHeight * .5f }, radius, ImGui::GetColorU32(themeSettings.StatusPalette.Warning));
	}

	ImGui::SetCursorScreenPos({ switchLeft, rowStart.y + (rowHeight - std::max(switchSize.y, ImGui::GetTextLineHeight())) * .5f });
	bool bootEnabled = !isDisabled;
	{
		ImGui::PushStyleColor(ImGuiCol_CheckMark, themeSettings.Palette.Text);
		const SKSE::stl::scope_exit restoreColor([] { ImGui::PopStyleColor(); });
		const auto readOnly = Util::DisableGuard(external != nullptr);
		if (Util::FeatureToggle("##BootToggleList", &bootEnabled, switchSize))
			feat->ToggleAtBootSetting();
	}
	Util::AddTooltip(external ? "Detection status. Enable the companion mod and its INI in your mod manager, then restart Skyrim VR." : "Turn this feature on or off at startup. Changes take effect after restarting the game.");
	ImGui::SetCursorScreenPos({ rowStart.x, rowStart.y + rowHeight });
	ImGui::Dummy({ 0, 0 });
	ImGui::EndDisabled();
	ImGui::PopID();
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const BuiltInMenu& menu)
{
	ImGui::PushID(menu.name.c_str());
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	if (ImGui::BeginChild("##FeatureConfigFrame", { MenuHeaderRenderer::GetFeaturePanelWidth(), 0 }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const char* pageId = BuiltInPageId(menu);
		const char* description = menu.name == "Home"           ? "Welcome to Community Shaders Expanded." :
		                          menu.name == "General"        ? "Choose how the menu looks and responds." :
		                          menu.name == "Advanced"       ? "Manage shader compilation, compatibility and diagnostics." :
		                          menu.name == "Profiling"      ? "Inspect CPU and GPU timings in the current scene." :
		                          menu.name == "Feature Issues" ? "Review feature compatibility and available recovery actions." :
		                                                          "Choose runtime features, then measure their cost in your scene.";
		{
			const float inset = MenuUI::SettingsPage::OverviewTextInset(pageId, ImGui::GetContentRegionAvail().x);
			ImGui::Indent(inset);
			const SKSE::stl::scope_exit restoreIndent([inset] { ImGui::Unindent(inset); });
			if (menu.name == "Profiling") {
				const float font = ImGui::GetFontSize();
				const float available = ImGui::GetContentRegionAvail().x;
				const float toggleSize = Util::Widgets::CheckboxSize();
				ImFont* controlFont;
				float controlFontSize, width;
				{
					MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
					controlFont = ImGui::GetFont();
					controlFontSize = ImGui::GetFontSize() * featureDescriptionScale;
					width = toggleSize + ImGui::GetStyle().ItemInnerSpacing.x + controlFont->CalcTextSizeA(controlFontSize, FLT_MAX, 0, "Enabled").x;
				}
				const bool stacked = available < width + font * 15;
				const auto start = ImGui::GetCursorScreenPos();
				const float titleHeight = DrawFeatureHeader(menu.name, description, stacked ? 0 : width + font);
				const auto afterTitle = ImGui::GetCursorScreenPos();
				if (!stacked)
					ImGui::SetCursorScreenPos({ start.x + available - width, start.y + std::max(0.0f, (titleHeight - toggleSize) * .5f) });
				{
					ImGui::PushFont(controlFont, controlFontSize);
					const SKSE::stl::scope_exit restoreFont([] { ImGui::PopFont(); });
					ProfilingRenderer::RenderEnabledControl();
				}
				if (!stacked)
					ImGui::SetCursorScreenPos(afterTitle);
			} else {
				DrawFeatureHeader(menu.name, description);
			}
		}
		if (ImGui::BeginChild("##BuiltInBody", { 0, -SettingsFooterHeight(pageId) - ImGui::GetStyle().ItemSpacing.y }, ImGuiChildFlags_None))
			menu.func();
		ImGui::EndChild();
		DrawSettingsFooter(pageId);
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopID();
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const std::string&)
{
	// std::unreachable() from c++23
	// you are not supposed to have selected a label!
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const CategoryHeader&)
{
	// Category headers are not selectable in the right panel
	ImGui::TextDisabled("Please select a feature from the left.");
}

void FeatureListRenderer::DrawMenuVisitor::operator()(Feature* feat)
{
	if (feat == &globals::features::csEditor)
		return;

	auto* external = dynamic_cast<MenuUI::ExternalSettingsPage*>(feat);
	const auto featureName = feat->GetShortName();
	bool isDisabled = !external && globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = external || feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();

	ImGui::PushID(featureName.c_str());
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	if (ImGui::BeginChild("##FeatureConfigFrame", { MenuHeaderRenderer::GetFeaturePanelWidth(), 0 }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		// Compute scene-controlled state once for both header and settings
		auto* sceneManager = SceneSettingsManager::GetSingleton();
		bool sceneControlled = !external && sceneManager->HasActiveSettingsForFeature(featureName) && !sceneManager->IsFeaturePaused(featureName);

		// Render feature header with integrated action buttons
		json performanceSettingsBefore;
		feat->SaveSettings(performanceSettingsBefore);
		RenderFeatureHeader(feat, isDisabled, isLoaded, sceneControlled);

		auto externalFooter = external ? std::optional(external->GetSettingsFooter()) : std::nullopt;
		const auto requirement = feat->GetSettingsFooterText();
		const float textInset = MenuUI::SettingsPage::OverviewTextInset(featureName.c_str(), ImGui::GetContentRegionAvail().x);
		const float requirementWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - textInset);
		const float requirementHeight = requirement.empty() ? 0 : ImGui::CalcTextSize(requirement.data(), requirement.data() + requirement.size(), false, requirementWidth).y + ImGui::GetStyle().ItemSpacing.y;
		{
			MenuUI::FeatureScope featureScope(feat);
			if (ImGui::BeginChild("##FeatureBody", { 0, -SettingsFooterHeight(featureName.c_str(), externalFooter ? &*externalFooter : nullptr) - requirementHeight - ImGui::GetStyle().ItemSpacing.y }, ImGuiChildFlags_None)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				MenuUI::DevBenchViewport viewport(featureName, "legacy", !PerformanceTuningRenderer::HasActiveMeasurements());
#endif
				if (external)
					external->DrawSettings();
				else
					RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled);
			}
			ImGui::EndChild();
		}

		if (!requirement.empty()) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textInset);
			const auto color = Util::Color::SecondaryText();
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::PushTextWrapPos(0);
			ImGui::TextUnformatted(requirement.data(), requirement.data() + requirement.size());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		// Keep save, load and feature defaults together in the fixed footer.
		if (external) {
			externalFooter = external->GetSettingsFooter();
			DrawSettingsFooter(featureName.c_str(), {}, &*externalFooter);
		} else {
			DrawSettingsFooter(featureName.c_str(), [&](float width) { RenderRestoreDefaultsButton(feat, isDisabled || sceneControlled, isLoaded, width); });
		}
		json performanceSettingsAfter;
		feat->SaveSettings(performanceSettingsAfter);
		if (performanceSettingsBefore != performanceSettingsAfter)
			PerformanceTuningRenderer::NotifyFeatureSettingsChanged(feat);
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopID();
	// Render reactive constraint warning outside the child window so it can appear as a top-level popup
	RenderReactiveConstraintWarningDialog();
}

bool FeatureListRenderer::DrawMenuVisitor::IsFeatureInstalled(const std::string& featureName)
{
	const auto path = Util::PathHelpers::GetFeatureIniPath(featureName);
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureHeader(Feature* feat, bool isDisabled, bool isLoaded, bool sceneControlled)
{
	const auto featureName = feat->GetShortName();
	const float textInset = MenuUI::SettingsPage::OverviewTextInset(featureName.c_str(), ImGui::GetContentRegionAvail().x);
	ImGui::Indent(textInset);
	const SKSE::stl::scope_exit restoreIndent([textInset] { ImGui::Unindent(textInset); });
	const float font = ImGui::GetFontSize();
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float availableWidth = ImGui::GetContentRegionAvail().x;

	auto status = feat->GetSettingsHeaderStatus();
	if (!isLoaded)
		status = { "Unavailable", true };
	else if (isDisabled)
		status = { "Restart pending", true };
	else if (sceneControlled)
		status = { "Scene settings", true };
	const auto& palette = globals::menu->GetTheme().StatusPalette;
	const auto color = status.needsAttention ? palette.Warning : palette.SuccessColor;
	ImFont* statusFont;
	float statusFontSize, statusWidth, enabledTextWidth;
	{
		MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
		statusFont = ImGui::GetFont();
		statusFontSize = ImGui::GetFontSize() * featureDescriptionScale;
		ImGui::PushFont(statusFont, statusFontSize);
		const SKSE::stl::scope_exit restoreMeasureFont([] { ImGui::PopFont(); });
		statusWidth = ImGui::CalcTextSize(status.label.data(), status.label.data() + status.label.size()).x + font * .75f;
		enabledTextWidth = Util::Widgets::VisibleTextEnd("Enabled").x;
	}
	const float statusGap = spacing + font;
	const float toggleSize = Util::Widgets::CheckboxSize();
	const float controlsWidth = statusWidth + statusGap + toggleSize + ImGui::GetStyle().ItemInnerSpacing.x + enabledTextWidth;
	const bool stacked = availableWidth < controlsWidth + font * 15;
	const auto titleStart = ImGui::GetCursorScreenPos();
	auto [description, keyFeatures] = feat->GetFeatureSummary();
	description = description.substr(0, description.find_first_of("\r\n"));
	const float titleHeight = DrawFeatureHeader(feat->GetDisplayName(), description, stacked ? 0 : controlsWidth + spacing);
	const auto afterTitle = ImGui::GetCursorScreenPos();
	if (!stacked)
		ImGui::SetCursorScreenPos({ titleStart.x + availableWidth - controlsWidth, titleStart.y + std::max(0.0f, (titleHeight - toggleSize) * .5f) });
	{
		ImGui::PushFont(statusFont, statusFontSize);
		const SKSE::stl::scope_exit restoreStatusFont([] { ImGui::PopFont(); });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, std::max(0.0f, (toggleSize - statusFontSize) * .5f) });
		const SKSE::stl::scope_exit restoreTogglePadding([] { ImGui::PopStyleVar(); });
		const auto statusStart = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddCircleFilled({ statusStart.x + font * .25f, statusStart.y + ImGui::GetFrameHeight() * .5f }, font * .2f, ImGui::GetColorU32(color));
		ImGui::Dummy({ font * .75f, ImGui::GetFrameHeight() });
		ImGui::SameLine(0, 0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(color, "%.*s", static_cast<int>(status.label.size()), status.label.data());
		Util::AddTooltip("Shows whether this feature is ready. Requirements and problems are shown below.");
		ImGui::SameLine(0, statusGap);
		{
			auto guard = Util::DisableGuard(isDisabled || !isLoaded || sceneControlled || PerformanceTuningRenderer::HasActiveMeasurements());
			feat->DrawSettingsEnabledControl();
		}
	}
	if (!stacked)
		ImGui::SetCursorScreenPos(afterTitle);
	else
		ImGui::Spacing();
	auto* overrideManager = SettingsOverrideManager::GetSingleton();
	if (!isDisabled && isLoaded && overrideManager && overrideManager->HasFeatureOverrides(featureName)) {
		const char* label = "Apply Override";
		const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
		const float left = ImGui::GetCursorScreenPos().x + std::max(0.0f, availableWidth - width);
		ImGui::SetCursorScreenPos({ left, ImGui::GetCursorScreenPos().y });
		{
			auto guard = Util::DisableGuard(sceneControlled || PerformanceTuningRenderer::HasActiveMeasurements());
			if (ImGui::Button(label)) {
				if (feat->ReapplyOverrideSettings())
					logger::info("Successfully reapplied override settings for {}", featureName);
				else
					logger::warn("Failed to reapply override settings for {}", featureName);
			}
		}
		Util::AddTooltip(sceneControlled ?
							 "Pause scene settings before restoring settings supplied by a mod." :
							 "Restore settings supplied by an installed mod, replacing your changes for this feature.");
	}

	if (!isDisabled && isLoaded) {
		auto guard = Util::DisableGuard(sceneControlled);
		feat->DrawSettingsHeaderControls();
	}
	DrawFeatureBanner(feat);
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureSettings(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage, bool sceneControlled)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	if (isDisabled) {
		ImGui::TextColored(themeSettings.StatusPalette.Disable, "Feature settings are hidden because this feature is disabled until restart.");
		ImGui::Spacing();
		ImGui::Text("Turn on this feature using its sidebar switch, then restart the game.");
		if (feat->GetShortName() == "WetnessEffects" && globals::features::wetterness.loaded) {
			ImGui::Spacing();
			ImGui::TextColored(
				themeSettings.StatusPalette.Error,
				"Wetness Effects and Wetterness cannot run together. Wetness Effects will be auto-disabled when Wetterness is active.");
		}
	} else {
		if (isLoaded) {
			auto weatherRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
			const bool showWeatherPause = weatherRegistry->HasWeatherSupport(feat->GetShortName());
			const bool moveWeatherPauseToBottom = showWeatherPause && feat->GetShortName() == "LODBlending";
			auto drawWeatherPauseToggle = [&]() {
				bool paused = weatherRegistry->IsFeaturePaused(feat->GetShortName());
				if (Util::Widgets::Checkbox("Pause Weather Overrides", &paused)) {
					WeatherManager::GetSingleton()->SetFeaturePaused(
						feat->GetShortName(), paused);
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Temporarily disable weather-based setting adjustments for this feature.\n"
						"This state is not saved.");
				}
			};
			if (showWeatherPause && !moveWeatherPauseToBottom) {
				drawWeatherPauseToggle();
				ImGui::Separator();
			}

			// Scene-specific settings toggle (Interior Only / TimeOfDay / Weather-Specific)
			// Show toggle whenever scene entries exist for this feature, even if feature-paused
			{
				const auto& featureShortName = feat->GetShortName();
				auto* sceneMgr = SceneSettingsManager::GetSingleton();
				bool scenePaused = sceneMgr->IsFeaturePaused(featureShortName);
				if (sceneControlled || scenePaused) {
					bool active = !scenePaused;
					if (Util::FeatureToggle("##PauseSceneSettings", &active))
						sceneMgr->SetFeaturePaused(featureShortName, !active);
					ImGui::SameLine();
					ImGui::Text("Scene Specific Settings");
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(scenePaused ? "Paused - click to resume" : "Active - click to pause");
					}
					ImGui::Separator();
				}
			}

			// Disable feature settings while scene overrides are actively applied (not paused)
			if (sceneControlled)
				ImGui::BeginDisabled();

			ImVec2 cursorPosBefore = ImGui::GetCursorPos();
			feat->DrawSettings();
			ImVec2 cursorPosAfter = ImGui::GetCursorPos();

			if (sceneControlled)
				ImGui::EndDisabled();

			if (moveWeatherPauseToBottom) {
				ImGui::Separator();
				drawWeatherPauseToggle();
			}

			// --- Reactive constraint detection ---
			// Compare the current full constraint set against g_knownConstraintKeys.
			// On the very first frame we just seed the set (no popup); after that
			// any key that wasn't previously known triggers the warning.
			// This catches both same-frame changes (e.g. TerrainBlending toggle)
			// and next-frame changes (e.g. Upscaling, whose resolutionScale is
			// updated in the render loop, not in DrawSettings).
			if (!g_reactiveWarningShow) {  // don't overwrite a pending popup
				auto currentConstraints = FeatureConstraints::GetAllActiveConstraints();

				if (!g_knownConstraintKeysInitialised) {
					// First time: seed known set, no popup
					for (const auto& [settingId, result] : currentConstraints) {
						g_knownConstraintKeys.insert(settingId.featureShortName + "|" + settingId.settingPath);
					}
					g_knownConstraintKeysInitialised = true;
				} else {
					// Diff: find keys present now but not previously known
					std::vector<std::pair<FeatureConstraints::SettingId, FeatureConstraints::ConstraintResult>> newConstraints;
					std::unordered_set<std::string> currentKeys;
					for (const auto& [settingId, result] : currentConstraints) {
						std::string key = settingId.featureShortName + "|" + settingId.settingPath;
						currentKeys.insert(key);
						if (g_knownConstraintKeys.find(key) == g_knownConstraintKeys.end()) {
							newConstraints.emplace_back(settingId, result);
						}
					}
					// Update known set to current (removes keys for constraints that went away)
					g_knownConstraintKeys = std::move(currentKeys);

					if (!newConstraints.empty() && !globals::menu->GetSettings().SkipConstraintWarning) {
						logger::info("Reactive constraint detection: {} new constraints", newConstraints.size());
						for (const auto& [settingId, result] : newConstraints) {
							logger::info("  - {}.{} forced to {} by {}", settingId.featureShortName, settingId.settingPath, FeatureConstraints::FormatConstraintValue(result.forcedValue), result.sources.empty() ? "?" : result.sources[0].featureName);
						}
						g_reactiveWarningShow = true;
						g_reactiveWarningConstraints = std::move(newConstraints);
						g_dontShowAgainCheckbox = false;
					}
				}
			}

			const float cursorEpsilon = 0.1f;
			bool cursorMoved = (std::abs(cursorPosAfter.x - cursorPosBefore.x) > cursorEpsilon ||
								std::abs(cursorPosAfter.y - cursorPosBefore.y) > cursorEpsilon);
			if (!cursorMoved) {
				ImGui::TextColored(themeSettings.StatusPalette.Disable, "There are no settings available for this feature.");
			}

		} else {
			if (FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
				feat->DrawUnloadedUI();
			} else if (hasFailedMessage) {
				// Conflict/version failures are rendered below. Do not present them as pending restart.
			} else if (IsFeatureInstalled(feat->GetShortName())) {
				ImGui::Text("This feature will be available after restart.");
			} else {
				feat->DrawUnloadedUI();
				if (!feat->GetFeatureModLink().empty()) {
					ImGui::Spacing();
					const auto downloadText = fmt::format("Click here to download this feature ({})", feat->GetFeatureModLink());
					if (ImGui::Selectable(downloadText.c_str())) {
						ShellExecuteA(NULL, "open", feat->GetFeatureModLink().c_str(), NULL, NULL, SW_SHOWNORMAL);
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Download the feature from the mod page.");
					}
				}
			}
		}
	}

	if (hasFailedMessage && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
		ImGui::Spacing();
		SeparatorTextWithFont("Error", Menu::FontRole::Subheading);
		Util::Text::WrappedError("%s", feat->failedLoadedMessage.c_str());
	}
}

void FeatureListRenderer::DrawMenuVisitor::RenderRestoreDefaultsButton(Feature* feat, bool isDisabled, bool isLoaded, float availableWidth)
{
	const auto guard = Util::DisableGuard(isDisabled || !isLoaded);
	const bool restoreDefaults = SettingsActionButton(2, nullptr, availableWidth);

	if (restoreDefaults) {
		feat->RestoreDefaultSettings();
		auto* weatherRegistry =
			WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		const auto& featureName = feat->GetShortName();
		if (weatherRegistry->HasWeatherSupport(featureName)) {
			weatherRegistry->CaptureFeatureUserSettings(featureName);
			auto* weatherManager = WeatherManager::GetSingleton();
			weatherManager->NotifyUserSettingsChanged();
			weatherManager->RefreshFeatureOverrides();
		}
		globals::menu->RequestSettingsDirtyCheck();
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Restore default settings for this feature");
	}
}

void FeatureListRenderer::DrawMenuVisitor::RenderReactiveConstraintWarningDialog()
{
	if (!g_reactiveWarningShow) {
		return;
	}

	// OpenPopup is idempotent while the popup is already open, so calling it
	// every frame while the flag is set is safe and ensures we don't miss the
	// one-frame window where ImGui expects it.
	ImGui::OpenPopup("Setting Change Warning");

	// Center the popup (ImGuiCond_Always matches the Clear Cache dialog pattern)
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	const auto viewportSize = ImGui::GetMainViewport()->WorkSize;
	const float margin = ImGui::GetFontSize() * 2;
	const ImVec2 maximumSize{ std::max(1.0f, viewportSize.x - margin), std::max(1.0f, viewportSize.y - margin) };
	ImGui::SetNextWindowSize({ std::min(ImGui::GetFontSize() * 48, maximumSize.x), 0 }, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints({ 0, 0 }, maximumSize);

	if (ImGui::BeginPopupModal("Setting Change Warning", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		const auto endPopup = SKSE::stl::scope_exit([] { ImGui::EndPopup(); });
		ImGui::TextWrapped("Some of your settings have been automatically adjusted due to feature incompatibilities.");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Table columns: Impacted Feature | Setting | Constrained By | Forced To
		if (ImGui::BeginTable("##ReactiveConstraintTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			const auto endTable = SKSE::stl::scope_exit([] { ImGui::EndTable(); });
			ImGui::TableSetupColumn("Impacted Feature", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Constrained By", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Forced To", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			size_t rowIndex = 0;
			for (const auto& [settingId, result] : g_reactiveWarningConstraints) {
				ImGui::TableNextRow();

				// --- Column 0: Impacted Feature (clickable -> navigate to that feature) ---
				ImGui::TableSetColumnIndex(0);
				{
					// Look up the display name of the target feature from its short name
					std::string targetDisplayName = settingId.featureShortName;
					for (auto* f : Feature::GetFeatureList()) {
						if (f->GetShortName() == settingId.featureShortName) {
							targetDisplayName = f->GetDisplayName();
							break;
						}
					}
					if (ImGui::Selectable(fmt::format("{}##imp{}", targetDisplayName, rowIndex).c_str())) {
						pendingFeatureSelection = settingId.featureShortName;
						ImGui::CloseCurrentPopup();
						g_reactiveWarningShow = false;
						g_reactiveWarningConstraints.clear();
						return;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Click to navigate to %s", targetDisplayName.c_str());
					}
				}

				// --- Column 1: Setting name ---
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", settingId.settingPath.c_str());

				// --- Column 2: Constrained By (source features, clickable) ---
				ImGui::TableSetColumnIndex(2);
				if (!result.sources.empty()) {
					if (ImGui::Selectable(fmt::format("{}##src{}", result.sources[0].featureName, rowIndex).c_str())) {
						pendingFeatureSelection = result.sources[0].featureShortName;
						ImGui::CloseCurrentPopup();
						g_reactiveWarningShow = false;
						g_reactiveWarningConstraints.clear();
						return;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("Click to navigate to %s", result.sources[0].featureName.c_str());
						if (result.sources.size() > 1) {
							ImGui::Separator();
							for (size_t i = 1; i < result.sources.size(); ++i) {
								ImGui::Text("Also: %s", result.sources[i].featureName.c_str());
							}
						}
						ImGui::Separator();
						ImGui::Text("%s", result.sources[0].reason.c_str());
					}
				}

				// --- Column 3: Forced value ---
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%s", FeatureConstraints::FormatConstraintValue(result.forcedValue).c_str());

				rowIndex++;
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextWrapped(
			"These settings are disabled in their respective feature menus while the constraints are active. "
			"Adjust the constraining features to remove them.");

		ImGui::Spacing();

		// "Don't show again" checkbox -- same pattern as Clear Cache dialog
		Util::Widgets::Checkbox("Don't show this warning again", &g_dontShowAgainCheckbox);

		ImGui::Spacing();

		// Centered OK button
		constexpr float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH;
		const float windowWidth = ImGui::GetWindowWidth();
		const float offset = (windowWidth - buttonWidth) * 0.5f;
		if (offset > 0)
			ImGui::SetCursorPosX(offset);

		if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
			if (g_dontShowAgainCheckbox) {
				if (auto* menu = globals::menu) {
					menu->GetSettings().SkipConstraintWarning = true;
				}
			}
			g_reactiveWarningShow = false;
			g_reactiveWarningConstraints.clear();
			ImGui::CloseCurrentPopup();
		}

	} else {
		// Popup was closed externally (e.g. clicked outside), reset state
		g_reactiveWarningShow = false;
		g_reactiveWarningConstraints.clear();
	}
}
