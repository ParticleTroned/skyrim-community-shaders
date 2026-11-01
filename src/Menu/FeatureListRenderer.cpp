#include "FeatureListRenderer.h"
#include "I18n/I18n.h"
#include "Menu/Layout.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/SettingsFooter.h"
#include "Menu/SettingsPage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <d3d11.h>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <ranges>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>

#include "Feature.h"
#include "FeatureConstraints.h"
#include "FeatureIssues.h"
#include "Features/CSEditor.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/ProfilingRenderer.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "WeatherManager.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
#include "WeatherVariableRegistry.h"

namespace
{
	constexpr const char* PERFORMANCE_TUNING_MENU_NAME = "Performance Tuning";
	constexpr float sidebarTextScale = 14.0f / 12.0f;
	constexpr float featureDescriptionScale = 1.4f;
	constexpr float featureTitleScale = (4.0f / 3.0f) * .82f;
	struct SidebarFontScope
	{
		MenuFonts::FontRoleGuard role{ Menu::FontRole::Subheading };
		SidebarFontScope() { ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * sidebarTextScale); }
		~SidebarFontScope() { ImGui::PopFont(); }
	};

	const char* BuiltInTitle(const std::string& name)
	{
		if (name == "Home")
			return T("menu.navigation.home", "Home");
		if (name == "General")
			return T("menu.navigation.general", "General");
		if (name == "Advanced")
			return T("menu.navigation.advanced", "Advanced");
		if (name == "Profiling")
			return T("menu.navigation.profiling", "Profiling");
		if (name == "Performance Tuning")
			return T("menu.navigation.performance_tuning", "Performance Tuning");
		if (name == "Feature Issues")
			return T("menu.navigation.feature_issues", "Feature Issues");
		return name.c_str();
	}

	// Core built-in menu names that always appear first in the menu list
	constexpr std::array<const char*, 6> CORE_MENU_NAMES = { "Home", "General", "Advanced", "Profiling", PERFORMANCE_TUNING_MENU_NAME, "Display" };
	constexpr float FEATURE_VERSION_TEXT_OPACITY = 0.6f;

	bool HasFeatureIni(Feature* feature)
	{
		if (!feature) {
			return false;
		}

		std::error_code ec;
		return std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feature->GetShortName()), ec);
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

	bool IsFeatureVisibleInMenuMode(Feature* feature, bool essentialsMode)
	{
		return feature &&
		       !feature->IsHiddenFromUserView() &&
		       !(essentialsMode && feature->IsHiddenInEssentialsMode()) &&
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

	int& GetFeatureUiModeValue(Feature* feature)
	{
		static std::unordered_map<std::string, int> featureUiModes;
		static int fallbackMode = 0;
		if (!feature)
			return fallbackMode;
		return featureUiModes[feature->GetShortName()];
	}

	bool IsFeatureAdvancedUiMode(Feature* feature)
	{
		if (!feature || !globals::menu || !globals::menu->IsPerformanceUiMode())
			return false;
		int& mode = GetFeatureUiModeValue(feature);
		mode = std::clamp(mode, 0, 1);
		return mode != 0;
	}

	void DrawFeatureUiModeToggle(Feature* feature)
	{
		if (!feature || !globals::menu || !globals::menu->IsPerformanceUiMode())
			return;
		int& mode = GetFeatureUiModeValue(feature);
		mode = std::clamp(mode, 0, 1);
		bool advanced = mode != 0;
		ImGui::PushID("FeatureUiMode");
		if (ImGui::Checkbox(T("menu.features.advanced", "Advanced"), &advanced))
			mode = advanced ? 1 : 0;
		ImGui::PopID();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("menu.features.advanced_tooltip", "Shows every setting for this feature."));
			ImGui::TextUnformatted(T("menu.features.essentials_tooltip", "Turn it off to return to the simpler Essentials view."));
		}
	}



	/**
	 * @brief Determines if the left feature panel should be visible based on auto-hide settings and mouse position
	 * @return true if panel should be visible, false if it should be hidden
	 */
	std::optional<bool> sidebarVisibilityOverride;
	bool sidebarVisible = true;
	bool resetSidebarLayout = false;

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



	/**
	 * @brief Draws a feature header with the feature name in large text and version in smaller text
	 * @param featureName The display name of the feature
	 * @param version The version string (can be empty)
	 * @param description Short description shown below the title (single line, truncated if too long)
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
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * titleScale * featureTitleScale);
			ImGui::PushTextWrapPos(wrapPosX);
			const SKSE::stl::scope_exit restoreTitle([] { ImGui::PopTextWrapPos(); ImGui::PopFont(); });
			ImGui::TextUnformatted(featureName.c_str());
		}

		// Store the title-only height for return value
		float titleOnlyHeight = ImGui::GetCursorPosY() - titleStartY;

		// Draw description if provided (wrapped to content width)
		if (!description.empty()) {
			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * featureDescriptionScale);
			const SKSE::stl::scope_exit restoreDescriptionFont([] { ImGui::PopFont(); });
			const auto descColor = Util::Color::SecondaryText();
			ImGui::PushStyleColor(ImGuiCol_Text, descColor);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availableWidth);
			ImGui::TextUnformatted(description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		ImGui::Dummy({ 0, ImGui::GetTextLineHeight() * .3f });

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
	return Feature::GetFeatureList();
}

void FeatureListRenderer::SetSidebarVisible(bool visible)
{
	sidebarVisibilityOverride = visible;
	sidebarVisible = visible;
}

bool FeatureListRenderer::IsSidebarVisible() { return sidebarVisible; }
void FeatureListRenderer::ResetSidebarVisibility() { sidebarVisibilityOverride.reset(); }
void FeatureListRenderer::ResetLayout()
{
	SetSidebarVisible(true);
	resetSidebarLayout = true;
}

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
	if (resetSidebarLayout) {
		selectedMenuEntryId.clear();
		selectedMenu = 0;
	}
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
			ImGui::TableSetupColumn("##ListOfMenus", 0, MenuUI::Layout::DefaultSidebarRatio);
			ImGui::TableSetupColumn("##MenuConfig", 0, 1.0f - MenuUI::Layout::DefaultSidebarRatio);
			MenuUI::Layout::ConstrainSidebar(resetSidebarLayout);
			resetSidebarLayout = false;
			RenderLeftColumn(menuList, selectedMenu, featureSearch, categoryExpansionStates);
		} else {
			// When left panel is hidden, right column takes full width
			ImGui::TableSetupColumn("##MenuConfig", 0, 1);
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto entry = IsSelectableMenuEntry(menuList, selectedMenu) ? GetSelectableMenuEntryId(menuList[selectedMenu]) : std::string{};
		if (entry != selectedMenuEntryId)
			MenuUI::DevBenchViewport::Invalidate();
#endif
		RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection);

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

	const bool essentialsMode = globals::menu && globals::menu->IsPerformanceUiMode();
	auto menuList = std::vector<MenuFuncInfo>{
		BuiltInMenu{ "Home", []() { HomePageRenderer::RenderHomePage(); } },
		BuiltInMenu{ PERFORMANCE_TUNING_MENU_NAME, []() { PerformanceTuningRenderer::Render(); } }
	};  // NOTE: The menu list is rebuilt every frame, so category expansion states
	// persist correctly. This is acceptable since the list is small and built
	// infrequently, but could be optimized if performance becomes an issue.

	if (!essentialsMode)
		menuList.insert(menuList.begin() + 1, BuiltInMenu{ "Profiling", []() {
			MenuUI::SettingsPage page("Profiling", { { "timings", T("menu.features.profiling.timings", "Timings"),
				T("menu.features.profiling.description", "Enable profiling, then inspect CPU and GPU work."),
				T("menu.features.profiling.summary", "Live CPU and GPU timings") } });
			if (page.Is("timings"))
				ProfilingRenderer::RenderStatistics();
		} });

	if (!essentialsMode) {
		menuList.insert(menuList.begin() + 1, BuiltInMenu{ "General", drawGeneralSettings });
		menuList.insert(menuList.begin() + 2, BuiltInMenu{ "Advanced", drawAdvancedSettings });
	}

	// Group features by category
	std::map<std::string, std::vector<Feature*>> categorizedFeatures;
	for (Feature* feat : sortedFeatureList) {
		if (IsFeatureVisibleInMenuMode(feat, essentialsMode) && feat->loaded) {
			std::string category(feat->GetCategory());
			categorizedFeatures[category].push_back(feat);
		}
	}

	// Sort features within each category
	for (auto& [category, features] : categorizedFeatures) {
		std::ranges::sort(features, [](Feature* a, Feature* b) {
			return a->GetDisplayName() < b->GetDisplayName();
		});
	}

	// Define category order
	std::vector<std::string> categoryOrder = { "Display", "Utility", "Characters", "Grass", "Lighting", "Materials", "Post-Processing", "Sky", "Landscape & Textures", "Water", "Other" };
	// Add categorized features to menu with collapsible headers
	for (const std::string& category : categoryOrder) {
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
		if (std::find(categoryOrder.begin(), categoryOrder.end(), category) == categoryOrder.end() && !features.empty()) {
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

	auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([essentialsMode](Feature* feat) {
		return IsFeatureVisibleInMenuMode(feat, essentialsMode) && !feat->loaded && HasFeatureIni(feat) &&
		       (!FeatureIssues::IsObsoleteFeature(feat->GetShortName()) || globals::state->IsDeveloperMode());
	});
	if (std::ranges::distance(unloadedFeatures) != 0) {
		menuList.push_back("Unloaded Features"s);
		std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
	}
	// Add top section for feature issues (rejected features, obsolete info, etc.)
	if (FeatureIssues::HasFeatureIssues()) {
		menuList.insert(menuList.begin(), BuiltInMenu{ "Feature Issues", []() {
														  MenuUI::SettingsPage page("Feature Issues", { { "review", T("menu.features.issues.review", "Review"),
					T("menu.features.issues.description", "Resolve unavailable or incompatible feature files."),
					T("menu.features.issues.summary", "Feature status and recovery") } });
				if (page.Is("review"))
					FeatureIssues::DrawFeatureIssuesUI();
													  } });
	}

	return menuList;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
std::string FeatureListRenderer::GetBuiltInPageMenuEntryId(const std::string& a_page)
{
	std::map<std::string, bool> categories;
	for (const auto& entry : BuildMenuList({}, categories, {}, {})) {
		const auto* menu = std::get_if<BuiltInMenu>(&entry);
		if (menu && a_page == BuiltInPageId(*menu))
			return GetSelectableMenuEntryId(entry);
	}
	return {};
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
		Util::DrawSectionHeader((globals::menu && globals::menu->IsPerformanceUiMode()) ? T("menu.features.essentials", "Essentials") : T("menu.features.features", "Features"), true);
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
		ImGui::TextDisabled(T("menu.features.select_item", "Please select an item on the left."));
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
	const auto* title = BuiltInTitle(menu.name);
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
		draw->AddText({ textLeft, middle - ImGui::GetTextLineHeight() * .5f }, ImGui::GetColorU32(color), title);
	}
	if (ImGui::CalcTextSize(title).x > maximum.x - textLeft)
		Util::AddTooltip(title);
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
	// Use Heading font for category headers
	{
		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
		Util::DrawCategoryHeader(header.name.c_str(), header.name.c_str(), isExpanded, header.count);
	}

	// Update expansion state
	categoryExpansionStates[header.name] = isExpanded;
}

void FeatureListRenderer::ListMenuVisitor::operator()(Feature* feat)
{
	SidebarFontScope fontGuard;

	const auto featureName = feat->GetShortName();
	bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
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
		std::error_code ec;
		if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()), ec)) {
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
	const float trailingSpace = std::max(ImGui::GetStyle().FramePadding.x, ImGui::GetFontSize() * .5f);
	const float labelWidth = std::max(1.0f, rowWidth - trailingSpace);
	const bool selected = selectedMenuRef == listId;
	if (selected) {
		auto* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(rowStart, { rowStart.x + rowWidth, rowStart.y + rowHeight }, ImGui::GetColorU32(ImGuiCol_Header));
		draw->AddRectFilled(rowStart, { rowStart.x + ImGui::GetStyle().FramePadding.x, rowStart.y + rowHeight }, ImGui::GetColorU32(themeSettings.StatusPalette.InfoColor), ImGui::GetStyle().FrameRounding);
	}

	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	{
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0, .5f });
		const SKSE::stl::scope_exit restoreAlign([] { ImGui::PopStyleVar(); });
		if (feat == &globals::features::csEditor) {
			globals::features::csEditor.DrawLauncherRow(labelWidth, rowHeight);
		} else if (ImGui::Selectable(fmt::format("    {} ", feat->GetDisplayName()).c_str(), false, ImGuiSelectableFlags_None, { labelWidth, rowHeight })) {
			selectedMenuRef = listId;
		}
	}
	ImGui::PopStyleColor();
	if (globals::menu->HasUnsavedFeatureSettings(feat->GetName())) {
		const float radius = ImGui::GetFontSize() * .12f;
		ImGui::GetWindowDrawList()->AddCircleFilled({ rowStart.x + rowWidth - trailingSpace * .5f, rowStart.y + rowHeight * .5f }, radius, ImGui::GetColorU32(themeSettings.StatusPalette.Warning));
	}

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
		const char* description = menu.name == "Home"           ? T("menu.navigation.home_description", "Welcome to Community Shaders Expanded.") :
		                          menu.name == "General"        ? T("menu.navigation.general_description", "Choose how the menu looks and responds.") :
		                          menu.name == "Advanced"       ? T("menu.navigation.advanced_description", "Manage shader compilation, compatibility and diagnostics.") :
		                          menu.name == "Profiling"      ? T("menu.navigation.profiling_description", "Inspect CPU and GPU timings in the current scene.") :
		                          menu.name == "Feature Issues" ? T("menu.navigation.issues_description", "Review feature compatibility and available recovery actions.") :
		                                                          T("menu.navigation.performance_description", "Choose runtime features, then measure their cost in your scene.");
		{
			const float inset = MenuUI::SettingsPage::OverviewTextInset(pageId, ImGui::GetContentRegionAvail().x);
			ImGui::Indent(inset);
			const SKSE::stl::scope_exit restoreIndent([inset] { ImGui::Unindent(inset); });
			if (menu.name == "Profiling") {
				const float font = ImGui::GetFontSize();
				const float available = ImGui::GetContentRegionAvail().x;
				const float toggleSize = Util::Widgets::CheckboxSize();
				ImFont* controlFont;
				float controlFontSize, controlFontBaseSize, width;
				{
					MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
					controlFont = ImGui::GetFont();
					controlFontSize = ImGui::GetFontSize() * featureDescriptionScale;
					controlFontBaseSize = ImGui::GetStyle().FontSizeBase * featureDescriptionScale;
					width = toggleSize + ImGui::GetStyle().ItemInnerSpacing.x + controlFont->CalcTextSizeA(controlFontSize, FLT_MAX, 0, T("feature.common.enabled", "Enabled")).x;
				}
				const bool stacked = available < width + font * 15;
				const auto start = ImGui::GetCursorScreenPos();
				const float titleHeight = DrawFeatureHeader(BuiltInTitle(menu.name), description, stacked ? 0 : width + font);
				const auto afterTitle = ImGui::GetCursorScreenPos();
				if (!stacked)
					ImGui::SetCursorScreenPos({ start.x + available - width, start.y + std::max(0.0f, (titleHeight - toggleSize) * .5f) });
				{
					ImGui::PushFont(controlFont, controlFontBaseSize);
					const SKSE::stl::scope_exit restoreFont([] { ImGui::PopFont(); });
					ProfilingRenderer::RenderEnabledControl();
				}
				if (!stacked)
					ImGui::SetCursorScreenPos(afterTitle);
			} else {
				DrawFeatureHeader(BuiltInTitle(menu.name), description);
			}
		}
		if (ImGui::BeginChild("##BuiltInBody", { 0, -MenuUI::SettingsFooterHeight(pageId) - ImGui::GetStyle().ItemSpacing.y }, ImGuiChildFlags_None)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			MenuUI::DevBenchViewport viewport(pageId, "legacy", !PerformanceTuningRenderer::HasActiveMeasurements());
#endif
			menu.func();
		}
		ImGui::EndChild();
		MenuUI::DrawSettingsFooter(pageId);
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
	ImGui::TextDisabled(T("menu.features.please_select_a_feature_from_the_left", "Please select a feature from the left."));
}

void FeatureListRenderer::DrawMenuVisitor::operator()(Feature* feat)
{
	const auto name = feat->GetShortName();
	const bool isDisabled = globals::state->IsFeatureDisabled(name);
	const bool isLoaded = feat->loaded;
	const bool hasFailedMessage = !feat->failedLoadedMessage.empty();
	ImGui::PushID(name.c_str());
	if (ImGui::BeginChild("##FeatureConfigFrame", { MenuHeaderRenderer::GetFeaturePanelWidth(), 0 }, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		auto* sceneManager = globals::sceneSettingsManager;
		const bool sceneControlled = sceneManager && sceneManager->HasActiveSettingsForFeature(name) && !sceneManager->IsFeaturePaused(name);
		json before;
		if (isLoaded)
			feat->SaveSettings(before);
		const bool canMutate = globals::menu->PrepareSettingsMutation();
		const auto mutationGuard = Util::DisableGuard(!canMutate);
		RenderFeatureHeader(feat, isDisabled, isLoaded, sceneControlled);
		const auto requirement = feat->GetSettingsFooterText();
		const float requirementHeight = requirement.empty() ? 0.0f : ImGui::CalcTextSize(requirement.data(), requirement.data() + requirement.size(), false, std::max(1.0f, ImGui::GetContentRegionAvail().x)).y + ImGui::GetStyle().ItemSpacing.y;
		const float footerHeight = MenuUI::SettingsFooterHeight(name.c_str()) + requirementHeight + ImGui::GetStyle().ItemSpacing.y;
		MenuUI::FeatureScope featureScope(feat);
		if (ImGui::BeginChild("##FeatureBody", { 0, -footerHeight })) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			MenuUI::DevBenchViewport viewport(name, "legacy", !PerformanceTuningRenderer::HasActiveMeasurements());
#endif
			RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled);
		}
		ImGui::EndChild();
		if (!requirement.empty()) {
			ImGui::PushTextWrapPos(0);
			ImGui::TextUnformatted(requirement.data(), requirement.data() + requirement.size());
			ImGui::PopTextWrapPos();
		}
		MenuUI::DrawSettingsFooter(name.c_str(), [&](float width) { RenderRestoreDefaultsButton(feat, isDisabled || sceneControlled, isLoaded, width); });
		json after;
		if (isLoaded) {
			feat->SaveSettings(after);
			if (before != after) {
				globals::menu->RequestSettingsDirtyCheck();
				PerformanceTuningRenderer::NotifyFeatureSettingsChanged(feat);
			}
		}
	}
	ImGui::EndChild();
	ImGui::PopID();
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
		status = { T("menu.features.unavailable", "Unavailable"), true };
	else if (isDisabled)
		status = { T("menu.features.restart_pending", "Restart pending"), true };
	else if (sceneControlled)
		status = { T("menu.features.scene_settings", "Scene settings"), true };
	const auto& palette = globals::menu->GetTheme().StatusPalette;
	const auto color = status.needsAttention ? palette.Warning : palette.SuccessColor;
	ImFont* statusFont;
	float statusFontSize, statusWidth, enabledTextWidth;
	{
		MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
		statusFont = ImGui::GetFont();
		statusFontSize = ImGui::GetStyle().FontSizeBase * featureDescriptionScale;
		ImGui::PushFont(statusFont, statusFontSize);
		const SKSE::stl::scope_exit restoreMeasureFont([] { ImGui::PopFont(); });
		statusWidth = ImGui::CalcTextSize(status.label.data(), status.label.data() + status.label.size()).x + font * .75f;
		enabledTextWidth = Util::Widgets::VisibleTextEnd(T("menu.features.enabled", "Enabled")).x;
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
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, std::max(0.0f, (toggleSize - ImGui::GetFontSize()) * .5f) });
		const SKSE::stl::scope_exit restoreTogglePadding([] { ImGui::PopStyleVar(); });
		const auto statusStart = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddCircleFilled({ statusStart.x + font * .25f, statusStart.y + ImGui::GetFrameHeight() * .5f }, font * .2f, ImGui::GetColorU32(color));
		ImGui::Dummy({ font * .75f, ImGui::GetFrameHeight() });
		ImGui::SameLine(0, 0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(color, "%.*s", static_cast<int>(status.label.size()), status.label.data());
		Util::AddTooltip(T("menu.features.status_help", "Shows whether this feature is ready. Requirements and problems are shown below."));
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
		const char* label = T("menu.features.apply_override", "Apply Override");
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
							 T("menu.features.pause_scene_override_help", "Pause scene settings before restoring settings supplied by a mod.") :
							 T("menu.features.restore_override_help", "Restore settings supplied by an installed mod, replacing your changes for this feature."));
	}

	if (!isDisabled && isLoaded) {
		auto guard = Util::DisableGuard(sceneControlled);
		feat->DrawSettingsHeaderControls();
	}
	if (isLoaded && globals::menu->IsPerformanceUiMode())
		DrawFeatureUiModeToggle(feat);
	DrawFeatureBanner(feat);
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureSettings(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage, bool sceneControlled)
{
	MenuUI::FeatureScope featureScope(feat);
	auto& themeSettings = globals::menu->GetSettings().Theme;

	if (isDisabled) {
		ImGui::TextColored(themeSettings.StatusPalette.Disable, "%s", T("menu.feature_list_renderer.feature_settings_are_hidden_because_this_feat_3df54bb5", "Feature settings are hidden because this feature is disabled at boot."));
		ImGui::Spacing();
		ImGui::TextWrapped("%s", T("menu.features.enable_in_startup_help", "Enable this feature in Advanced > Startup, save settings and restart the game to access its configuration options."));
	} else {
		if (isLoaded) {
			const bool globalEssentialsMode = globals::menu && globals::menu->IsPerformanceUiMode();
			const bool featureAdvancedMode = IsFeatureAdvancedUiMode(feat);
			const bool essentialsFeatureMode = globalEssentialsMode && !featureAdvancedMode;
			const bool hasEssentialSettings = essentialsFeatureMode && feat->HasEssentialSettings();
			auto weatherRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
			if (!essentialsFeatureMode && weatherRegistry->HasWeatherSupport(feat->GetShortName())) {
				bool paused = weatherRegistry->IsFeaturePaused(feat->GetShortName());
				if (ImGui::Checkbox(T("menu.features.pause_weather_overrides", "Pause Weather Overrides"), &paused)) {
					WeatherManager::GetSingleton()->SetFeaturePaused(feat->GetShortName(), paused);
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						T("menu.feature_list_renderer.temporarily_disable_weather_based_setting_adj_3eba0dae", "Temporarily disable weather-based setting adjustments for this feature.\n"
						"This state is not saved."));
				}
				ImGui::Separator();
			}

			// Scene-specific settings toggle (Interior Only / TimeOfDay / Weather-Specific)
			// Show toggle whenever scene entries exist for this feature, even if feature-paused
			{
				const auto& featureShortName = feat->GetShortName();
				auto* sceneMgr = globals::sceneSettingsManager;
				bool scenePaused = sceneMgr->IsFeaturePaused(featureShortName);
				if (!essentialsFeatureMode && (sceneControlled || scenePaused)) {
					bool active = !scenePaused;
					if (Util::FeatureToggle("##PauseSceneSettings", &active))
						sceneMgr->SetFeaturePaused(featureShortName, !active);
					ImGui::SameLine();
					ImGui::Text(T("menu.features.scene_specific_settings", "Scene Specific Settings"));
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(scenePaused ? T("menu.features.scene_paused", "Paused - click to resume") : T("menu.features.scene_active", "Active - click to pause"));
					}
					ImGui::Separator();
				}
			}

			// Disable feature settings while scene overrides are actively applied (not paused)
			const bool settingsLocked = sceneControlled || PerformanceTuningRenderer::HasActiveMeasurements();
			if (settingsLocked)
				ImGui::BeginDisabled();

			ImVec2 cursorPosBefore = ImGui::GetCursorPos();
			if (hasEssentialSettings) {
				feat->DrawEssentialSettings();
			} else if (essentialsFeatureMode) {
				ImGui::TextDisabled(T("menu.features.toggle_advanced_in_the_header_to_expose_this_feature_s_full_ui", "Toggle Advanced in the header to expose this feature's full UI"));
			} else {
				feat->DrawSettings();
			}

			if (!globalEssentialsMode && feat != &globals::features::csEditor && ProfilingRenderer::HasFeatureTimers(feat->GetShortName())) {
				ImGui::SeparatorText(T("menu.features.profiling", "Profiling"));
				ProfilingRenderer::RenderFeatureTimers(feat->GetShortName());
			}

			ImVec2 cursorPosAfter = ImGui::GetCursorPos();

			if (settingsLocked)
				ImGui::EndDisabled();

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
				ImGui::TextColored(themeSettings.StatusPalette.Disable, T("menu.feature_list_renderer.there_are_no_settings_available_for_this_feature", "There are no settings available for this feature."));
			}

			ImGui::Spacing();
		} else {
			if (FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
				feat->DrawUnloadedUI();
			} else if (hasFailedMessage) {
				// A compatibility or version failure is not a pending-restart state.
			} else if (IsFeatureInstalled(feat->GetShortName())) {
				ImGui::Text(T("menu.features.this_feature_will_be_available_after_restart", "This feature will be available after restart."));
			} else {
				feat->DrawUnloadedUI();
				if (!feat->GetFeatureModLink().empty()) {
					ImGui::Spacing();
					const auto downloadText = fmt::format("Click here to download this feature ({})", feat->GetFeatureModLink());
					if (ImGui::Selectable(downloadText.c_str())) {
						ShellExecuteA(NULL, "open", feat->GetFeatureModLink().c_str(), NULL, NULL, SW_SHOWNORMAL);
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(T("menu.features.download_the_feature_from_the_mod_page", "Download the feature from the mod page."));
					}
				}
			}
		}
	}

	if (hasFailedMessage && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
		ImGui::Spacing();
		SeparatorTextWithFont("Error", Menu::FontRole::Subheading);
		ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", feat->failedLoadedMessage.c_str());
	}
}

void FeatureListRenderer::DrawMenuVisitor::RenderRestoreDefaultsButton(Feature* feat, bool isDisabled, bool isLoaded, float availableWidth)
{
	const auto guard = Util::DisableGuard(isDisabled || !isLoaded || PerformanceTuningRenderer::HasActiveMeasurements());
	if (MenuUI::DrawSettingsDefaultButton(availableWidth)) {
		feat->RestoreDefaultSettings();
		auto* weatherRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		const auto name = feat->GetShortName();
		if (weatherRegistry->HasWeatherSupport(name)) {
			weatherRegistry->CaptureFeatureUserSettings(name);
			auto* weather = WeatherManager::GetSingleton();
			weather->NotifyUserSettingsChanged();
			weather->RefreshFeatureOverrides();
		}
		globals::menu->RequestSettingsDirtyCheck();
	}
	Util::AddTooltip(T("menu.features.restore_defaults_tooltip", "Restore default settings for this feature."));
}

void FeatureListRenderer::DrawMenuVisitor::RenderReactiveConstraintWarningDialog()
{
	if (!g_reactiveWarningShow) {
		return;
	}

	constexpr const char* popupId = "###SettingChangeWarning";
	const std::string popupTitle = fmt::format("{}{}", T("menu.features.setting_change_warning_title", "Setting Change Warning"), popupId);

	// OpenPopup is idempotent while the popup is already open, so calling it
	// every frame while the flag is set is safe and ensures we don't miss the
	// one-frame window where ImGui expects it.
	ImGui::OpenPopup(popupId);

	// Center the popup (ImGuiCond_Always matches the Clear Cache dialog pattern)
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	if (Util::BeginPopupModalWithRoundedClose(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("%s", T("menu.features.settings_adjusted_warning", "Some of your settings have been automatically adjusted due to feature incompatibilities."));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Table columns: Impacted Feature | Setting | Constrained By | Forced To
		if (ImGui::BeginTable("##ReactiveConstraintTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn(T("menu.features.impacted_feature", "Impacted Feature"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.setting", "Setting"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.constrained_by", "Constrained By"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.forced_to", "Forced To"), ImGuiTableColumnFlags_WidthStretch);
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
						ImGui::Text(T("menu.features.click_to_navigate_to_s", "Click to navigate to %s"), targetDisplayName.c_str());
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
						ImGui::Text(T("menu.features.click_to_navigate_to_s", "Click to navigate to %s"), result.sources[0].featureName.c_str());
						if (result.sources.size() > 1) {
							ImGui::Separator();
							for (size_t i = 1; i < result.sources.size(); ++i) {
								ImGui::Text(T("menu.features.also_s", "Also: %s"), result.sources[i].featureName.c_str());
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

			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextWrapped(
			T("menu.feature_list_renderer.these_settings_are_disabled_in_their_respecti_e8af7e69", "These settings are disabled in their respective feature menus while the constraints are active. "
			"Adjust the constraining features to remove them."));

		ImGui::Spacing();

		// "Don't show again" checkbox -- same pattern as Clear Cache dialog
		ImGui::Checkbox(T("menu.features.don_t_show_this_warning_again", "Don't show this warning again"), &g_dontShowAgainCheckbox);

		ImGui::Spacing();

		// Centered OK button
		constexpr float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH;
		const float windowWidth = ImGui::GetWindowWidth();
		const float offset = (windowWidth - buttonWidth) * 0.5f;
		if (offset > 0)
			ImGui::SetCursorPosX(offset);

		if (ImGui::Button(T("menu.features.ok", "OK"), ImVec2(buttonWidth, 0))) {
			if (g_dontShowAgainCheckbox) {
				if (auto* menu = globals::menu) {
					menu->GetSettings().SkipConstraintWarning = true;
				}
			}
			g_reactiveWarningShow = false;
			g_reactiveWarningConstraints.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	} else {
		// Popup was closed externally (e.g. clicked outside), reset state
		g_reactiveWarningShow = false;
		g_reactiveWarningConstraints.clear();
	}
}
