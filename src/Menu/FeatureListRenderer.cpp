#include "FeatureListRenderer.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/Layout.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/SettingsActions.h"
#include "Menu/SettingsNavigation.h"
#include "Menu/SettingsPage.h"
#include "Menu/SettingsSearch.h"
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
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/PerformanceOverlay.h"
#include "Features/RenderDoc.h"
#include "Features/ScreenshotFeature.h"
#include "Features/TextureStreaming.h"
#include "Features/VR.h"
#include "Features/Wetterness.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/PerformancePage.h"
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
	constexpr const char* PERFORMANCE_PRESETS_MENU_NAME = "Performance Presets";
	constexpr const char* PERFORMANCE_TUNING_MENU_NAME = "Performance Tuning";
	// Core built-in menu names that always appear before the feature list.
	constexpr std::array<const char*, 3> CORE_MENU_NAMES = { "Performance", "Settings", "Dev Tools" };

	bool searchVisible = false;
	bool focusSearch = false;

	constexpr float sidebarTextScale = 14.0f / 12.0f;
	constexpr float sidebarIconTextInset = 1.9f;
	struct SidebarFontScope
	{
		MenuFonts::FontRoleGuard role{ Menu::FontRole::Subheading };
		SidebarFontScope() { ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * sidebarTextScale); }
		~SidebarFontScope() { ImGui::PopFont(); }
	};

	void DrawSidebarLabel(const char* label, Util::ActionGlyph glyph, ImVec2 minimum, ImVec2 maximum, ImVec4 color)
	{
		const float font = ImGui::GetFontSize();
		const float middle = (minimum.y + maximum.y) * .5f;
		const float textLeft = minimum.x + font * sidebarIconTextInset;
		auto* draw = ImGui::GetWindowDrawList();
		Util::DrawActionGlyph(draw, glyph, { minimum.x + font * .15f, middle - font * .75f }, { minimum.x + font * 1.65f, middle + font * .75f }, ImGui::GetColorU32(color));
		if (textLeft < maximum.x) {
			draw->PushClipRect({ textLeft, minimum.y }, maximum, true);
			const SKSE::stl::scope_exit restoreClip([draw] { draw->PopClipRect(); });
			draw->AddText({ textLeft, middle - ImGui::GetTextLineHeight() * .5f }, ImGui::GetColorU32(color), label);
		}
		if (ImGui::CalcTextSize(label).x > maximum.x - textLeft)
			Util::AddTooltip(label);
	}

	void DrawSidebarSelection(ImVec2 minimum, ImVec2 maximum)
	{
		auto* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Header));
		draw->AddRectFilled(minimum, { minimum.x + ImGui::GetStyle().FramePadding.x, maximum.y },
			ImGui::GetColorU32(globals::menu->GetTheme().StatusPalette.InfoColor), ImGui::GetStyle().FrameRounding);
	}

	constexpr float featureDescriptionScale = 1.4f;
	constexpr float featureTitleScale = (4.0f / 3.0f) * .82f;
	constexpr float footerTextScale = 1.0f;
	constexpr size_t footerActionCount = 3;
	constexpr float footerButtonPadding = 1.85f;
	constexpr float footerDetailTextScale = .95f;
	constexpr float footerStatusLineGap = .05f;
	constexpr float footerStatusHeight = footerTextScale + footerStatusLineGap + footerDetailTextScale;
	constexpr float footerVerticalPadding = .15f;
	constexpr float footerStackGap = .5f;
	constexpr std::array footerStatusLabels{ "CSX settings not saved", "Unsaved CSX settings", "CSX settings saved" };

	float SettingsActionButtonHeight()
	{
		return MenuUI::SettingsBarHeight() - ImGui::GetFontSize() * footerVerticalPadding * 2;
	}
	constexpr std::array featureStatusLabels{ "Feature action failed", "Unsaved feature settings", "Feature settings saved" };

	float SettingsActionTextWidth(const char* label)
	{
		return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * footerTextScale, FLT_MAX, 0, label).x;
	}

	float SettingsActionSpacing(const MenuUI::SettingsFooter* external, float availableWidth)
	{
		const float requested = ImGui::GetStyle().ItemSpacing.x;
		const float limit = std::max(0.0f, (availableWidth - footerActionCount) / (footerActionCount - 1));
		return external ? requested : std::clamp(requested, 0.0f, limit);
	}

	std::array<const char*, footerActionCount> SettingsActionLabels(const MenuUI::SettingsFooter* external = nullptr, float availableWidth = 0)
	{
		const auto labels = external ? std::array{ external->actions[0].label, external->actions[1].label, external->actions[2].label } : std::array{ "Save settings", "Load saved", "Restore defaults" };
		const float width = availableWidth > 0 ? availableWidth : ImGui::GetContentRegionAvail().x;
		const auto fits = [&](const auto& candidates) {
			return SettingsActionTextWidth(candidates[0]) + SettingsActionTextWidth(candidates[1]) + SettingsActionTextWidth(candidates[2]) +
			           ImGui::GetFontSize() * footerButtonPadding * footerActionCount + SettingsActionSpacing(external, width) * (footerActionCount - 1) <=
			       width;
		};
		if (fits(labels))
			return labels;
		if (!external) {
			const std::array compact{ "Save", "Load", "Defaults" };
			return fits(compact) ? compact : std::array{ "", "", "" };
		}
		auto compact = labels;
		for (size_t index = 0; index < compact.size(); ++index)
			if (external->actions[index].compactLabel)
				compact[index] = external->actions[index].compactLabel;
		return compact;
	}

	float SettingsActionButtonWidth(size_t index, const MenuUI::SettingsFooter* external = nullptr, float availableWidth = 0)
	{
		const auto* label = SettingsActionLabels(external, availableWidth)[index];
		const float width = SettingsActionTextWidth(label) + ImGui::GetFontSize() * footerButtonPadding;
		const float available = availableWidth > 0 ? availableWidth : ImGui::GetContentRegionAvail().x;
		const float limit = !external && !*label ? (available - SettingsActionSpacing(external, available) * (footerActionCount - 1)) / footerActionCount : available;
		return std::min(width, std::max(1.0f, limit));
	}

	bool SettingsActionButton(size_t a_index, const MenuUI::SettingsFooter* external = nullptr, float availableWidth = 0)
	{
		const auto* label = SettingsActionLabels(external, availableWidth)[a_index];
		const float font = ImGui::GetFontSize();
		const float width = SettingsActionButtonWidth(a_index, external, availableWidth);
		constexpr std::array glyphs{ Util::ActionGlyph::SaveSettings, Util::ActionGlyph::LoadSettings, Util::ActionGlyph::RestoreDefaults };
		ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * footerTextScale);
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 1, .5f });
		const SKSE::stl::scope_exit restoreAlign([] { ImGui::PopStyleVar(); ImGui::PopFont(); });
		ImGui::PushID(static_cast<int>(a_index));
		const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
		const std::string buttonLabel = std::string(label) + "###SettingsAction";
		const bool clicked = ImGui::Button(buttonLabel.c_str(), { width, SettingsActionButtonHeight() });
		const auto minimum = ImGui::GetItemRectMin();
		const float extent = std::min(font, std::max(1.0f, width - font * .6f));
		const float x = minimum.x + (*label ? font * .3f : (width - extent) * .5f);
		const float y = minimum.y + (ImGui::GetItemRectSize().y - extent) * .5f;
		Util::DrawActionGlyph(ImGui::GetWindowDrawList(), external && a_index == 2 ? Util::ActionGlyph::LoadSettings : glyphs[a_index],
			{ x, y }, { x + extent, y + extent }, ImGui::GetColorU32(ImGuiCol_Text));
		return clicked;
	}

	struct SettingsFooterLayout
	{
		float backgroundLeft;
		float backgroundRight;
		float availableActionWidth;
		float actionsLeft;
		float statusLeft;
		float statusWidth;
		float actionsHeight;
		int actionRows;
		std::array<ImVec2, footerActionCount> actionOffsets;
		bool stacked;
	};

	SettingsFooterLayout GetSettingsFooterLayout(const char* a_page, const MenuUI::SettingsFooter* external = nullptr)
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const float font = ImGui::GetFontSize();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		constexpr size_t count = footerActionCount;
		const auto cardInsets = MenuUI::SettingsPage::OverviewCardInsets(a_page, width);
		const float backgroundLeft = std::min(cardInsets.x, std::max(0.0f, width - cardInsets.y - font * 4));
		const float gutter = std::min(font * .75f, (width - backgroundLeft - cardInsets.y) * .05f);
		const float statusLeft = backgroundLeft + gutter;
		const float right = width - cardInsets.y - gutter;
		const float contentWidth = std::max(1.0f, right - statusLeft);
		float availableActionWidth = contentWidth;
		if (!external) {
			const float statusWidth = font + std::max({ SettingsActionTextWidth(footerStatusLabels[0]), SettingsActionTextWidth(footerStatusLabels[1]), SettingsActionTextWidth(footerStatusLabels[2]), SettingsActionTextWidth(featureStatusLabels[1]) });
			const float minimumActionsWidth = font * footerButtonPadding * count + spacing * (count - 1);
			const float reservedStatusWidth = std::min(statusWidth, std::max(font, contentWidth - minimumActionsWidth - font));
			availableActionWidth = std::max(static_cast<float>(count), contentWidth - reservedStatusWidth - font);
		}
		std::array<float, footerActionCount> buttonWidths{};
		const float buttonSpacing = SettingsActionSpacing(external, availableActionWidth);
		float actionsWidth = buttonSpacing * (count - 1);
		for (size_t index = 0; index < count; ++index) {
			buttonWidths[index] = SettingsActionButtonWidth(index, external, availableActionWidth);
			actionsWidth += buttonWidths[index];
		}
		const bool wrapActions = external && actionsWidth > contentWidth;
		const float inset = !external ? right - actionsWidth : wrapActions ? statusLeft :
		                                                                     std::clamp(MenuUI::SettingsPage::OverviewLastColumnInset(a_page, width), statusLeft, std::max(statusLeft, right - actionsWidth));
		const bool stacked = external && inset - statusLeft < font * 12;
		const float statusWidth = stacked ? contentWidth : inset - statusLeft - font;
		const float actionSpacing = buttonSpacing + (wrapActions ? 0 : std::max(0.0f, right - inset - actionsWidth) / (count - 1));
		std::array<ImVec2, footerActionCount> offsets{};
		float x = 0, y = 0;
		int rows = 1;
		for (size_t index = 0; index < count; ++index) {
			if (external && index && x + buttonWidths[index] > right - inset + .01f) {
				x = 0;
				y += SettingsActionButtonHeight() + ImGui::GetStyle().ItemSpacing.y;
				++rows;
			}
			offsets[index] = { x, y };
			x += buttonWidths[index] + actionSpacing;
		}
		return { backgroundLeft, cardInsets.y, availableActionWidth, inset, statusLeft, std::max(1.0f, statusWidth), y + SettingsActionButtonHeight(), rows, offsets, stacked };
	}

	float SettingsFooterContentHeight(const SettingsFooterLayout& layout, bool external)
	{
		const float statusHeight = ImGui::GetFontSize() * (external ? footerStatusHeight : footerTextScale);
		return layout.stacked ? layout.actionsHeight + ImGui::GetFontSize() * footerStackGap + statusHeight : std::max(layout.actionsHeight, statusHeight);
	}

	float SettingsFooterHeight(const char* a_page, const MenuUI::SettingsFooter* external = nullptr)
	{
		return std::max(MenuUI::SettingsBarHeight(), ImGui::GetFontSize() * footerVerticalPadding * 2 + SettingsFooterContentHeight(GetSettingsFooterLayout(a_page, external), external != nullptr));
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

	void DrawSettingsFooter(const char* a_page, const std::function<void(float)>& a_defaults = {}, const MenuUI::SettingsFooter* external = nullptr, Feature* feature = nullptr)
	{
		const auto footerStart = ImGui::GetCursorScreenPos();
		const auto layout = GetSettingsFooterLayout(a_page, external);
		const float font = ImGui::GetFontSize();
		const float height = SettingsFooterHeight(a_page, external);
		const auto footerColor = MenuUI::SettingsBarColor(ImGuiCol_FrameBg, true);
		ImGui::GetWindowDrawList()->AddRectFilled({ footerStart.x + layout.backgroundLeft, footerStart.y },
			{ footerStart.x + ImGui::GetContentRegionAvail().x - layout.backgroundRight, footerStart.y + height }, ImGui::GetColorU32(footerColor));
		const float contentHeight = SettingsFooterContentHeight(layout, external != nullptr);
		const ImVec2 start{ footerStart.x, footerStart.y + font * footerVerticalPadding };
		const float actionsY = start.y + (layout.stacked ? 0 : (contentHeight - layout.actionsHeight) * .5f);
		const float statusHeight = font * (external ? footerStatusHeight : footerTextScale);
		const ImVec2 statusStart{ start.x + layout.statusLeft, start.y + (layout.stacked ? layout.actionsHeight + font * footerStackGap : (contentHeight - statusHeight) * .5f) };
		const auto& menu = *globals::menu;
		const auto& palette = menu.GetTheme().StatusPalette;
		const auto* featureResult = feature && !external ? &MenuUI::GetFeatureSettingsFeedback(feature) : nullptr;
		const bool error = external ? external->error : featureResult ? featureResult->error :
		                                                                menu.IsSettingsSaveMessageError() && !menu.GetSettingsSaveMessage().empty();
		const bool dirty = external ? external->dirty : feature ? menu.HasUnsavedFeatureSettings(feature->GetName()) :
		                                                          menu.HasUnsavedSettings();
		const size_t statusIndex = error ? 0 : dirty ? 1 :
		                                               2;
		const char* status = external ? external->status.c_str() : feature ? featureStatusLabels[statusIndex] :
		                                                                     footerStatusLabels[statusIndex];
		const std::string detail = external ? external->detail : error ? (featureResult ? featureResult->message : menu.GetSettingsSaveMessage()) :
		                                                     dirty     ? "Save to keep your current choices." :
		                                                                 "Your choices are up to date.";
		const auto statusColor = error ? Util::Color::Readiness(false) : dirty ? palette.Warning :
		                                                                         Util::Color::SecondaryText();
		auto* draw = ImGui::GetWindowDrawList();
		{
			draw->PushClipRect({ statusStart.x - font * .2f, statusStart.y }, { statusStart.x + layout.statusWidth, statusStart.y + statusHeight }, true);
			const SKSE::stl::scope_exit restoreClip([draw] { draw->PopClipRect(); });
			const float markerScale = external ? font : std::min(font, layout.statusWidth);
			const ImVec2 marker{ statusStart.x + markerScale * .35f, statusStart.y + statusHeight * .5f };
			draw->AddCircleFilled(marker, markerScale * .52f, ImGui::GetColorU32({ statusColor.x, statusColor.y, statusColor.z, .10f }));
			draw->AddCircleFilled(marker, markerScale * .26f, ImGui::GetColorU32(statusColor));
			draw->AddText(nullptr, font * footerTextScale, { statusStart.x + font, statusStart.y }, ImGui::GetColorU32(statusColor), status);
			if (external)
				draw->AddText(nullptr, font * footerDetailTextScale, { statusStart.x + font, statusStart.y + font * (footerTextScale + footerStatusLineGap) }, ImGui::GetColorU32(Util::Color::SecondaryText()), detail.c_str());
		}
		ImGui::SetCursorScreenPos(statusStart);
		ImGui::Dummy({ layout.statusWidth, statusHeight });
		std::string statusHelp = external ? detail : std::string(status) + "\n" + detail;
		if (feature) {
			const auto help = feature->GetSettingsFooterText();
			if (!help.empty())
				statusHelp += "\n" + std::string(help);
		}
		Util::AddTooltip(statusHelp.c_str());
		const char* blockedHelp = external ? nullptr : feature ? MenuUI::GetFeatureSettingsActionBlockReason(feature) :
		                                                         MenuUI::GetGlobalSettingsActionBlockReason(MenuUI::GlobalSettingsAction::Save);
		const auto invoke = [&](MenuUI::GlobalSettingsAction action) {
			if (feature)
				MenuUI::InvokeFeatureSettingsAction(action, feature);
			else
				MenuUI::InvokeGlobalSettingsAction(action);
		};
		const auto actionsGuard = Util::DisableGuard(blockedHelp != nullptr);
		const auto addActionTooltip = [&](const char* help) {
			Util::AddTooltip(blockedHelp ? blockedHelp : help, ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
		};
		ImGui::SetCursorScreenPos({ start.x + layout.actionsLeft, actionsY });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { font * .35f, font * .35f });
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
		const SKSE::stl::scope_exit restoreStyle([] { ImGui::PopStyleVar(3); });
		const ImVec4 secondary{ 0, 0, 0, 0 };
		const auto hovered = MenuUI::SettingsBarColor(ImGuiCol_TabHovered);
		const auto active = MenuUI::SettingsBarColor(ImGuiCol_Header);
		{
			const auto primary = Util::StyledButtonWrapper(dirty ? palette.Warning : secondary, hovered, active);
			const auto accent = palette.Warning;
			const float brightness = accent.x * .299f + accent.y * .587f + accent.z * .114f;
			ImGui::PushStyleColor(ImGuiCol_Text, dirty ? (brightness > .55f ? ImVec4(.08f, .08f, .08f, 1) : ImVec4(1, 1, 1, 1)) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
			const SKSE::stl::scope_exit restoreText([] { ImGui::PopStyleColor(); });
			if (external)
				DrawExternalAction(0, *external, layout.availableActionWidth);
			else if (SettingsActionButton(0, nullptr, layout.availableActionWidth))
				invoke(MenuUI::GlobalSettingsAction::Save);
		}
		if (!external)
			addActionTooltip(feature ? "Save only this feature's settings." : MenuUI::GetGlobalSettingsActionHelp(MenuUI::GlobalSettingsAction::Save));
		ImGui::SetCursorScreenPos({ start.x + layout.actionsLeft + layout.actionOffsets[1].x, actionsY + layout.actionOffsets[1].y });
		{
			const auto secondaryStyle = Util::StyledButtonWrapper(secondary, hovered, active);
			if (external)
				DrawExternalAction(1, *external, layout.availableActionWidth);
			else if (SettingsActionButton(1, nullptr, layout.availableActionWidth))
				invoke(MenuUI::GlobalSettingsAction::Load);
		}
		if (!external)
			addActionTooltip(feature ? "Restore only this feature's last successfully loaded or saved settings." : MenuUI::GetGlobalSettingsActionHelp(MenuUI::GlobalSettingsAction::Load));
		ImGui::SetCursorScreenPos({ start.x + layout.actionsLeft + layout.actionOffsets[2].x, actionsY + layout.actionOffsets[2].y });
		{
			const auto outlineStyle = Util::StyledButtonWrapper(secondary, hovered, active);
			if (external)
				DrawExternalAction(2, *external, layout.availableActionWidth);
			else if (a_defaults) {
				a_defaults(layout.availableActionWidth);
				if (blockedHelp)
					addActionTooltip(blockedHelp);
			} else {
				if (SettingsActionButton(2, nullptr, layout.availableActionWidth))
					invoke(MenuUI::GlobalSettingsAction::RestoreDefaults);
				addActionTooltip(feature ? "Restore default settings for this feature." : MenuUI::GetGlobalSettingsActionHelp(MenuUI::GlobalSettingsAction::RestoreDefaults));
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

	bool IsTopLevelFeature(const Feature* feature)
	{
		return feature == &globals::features::adaptiveBrightness || feature == &globals::features::screenshotFeature;
	}

	bool IsPerformanceFeature(const Feature* feature)
	{
		return feature == &globals::features::performanceOverlay || feature == &TextureStreaming::Instance();
	}

	bool IsFeatureVisible(Feature* feature)
	{
		return feature &&
		       !IsTopLevelFeature(feature) && feature != &globals::features::renderDoc &&
		       !IsPerformanceFeature(feature) &&
		       !feature->IsHiddenFromUserView() &&
		       feature->IsInMenu();
	}

	float MeasureSidebarWidth(const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList)
	{
		SidebarFontScope fontGuard;
		const float font = ImGui::GetFontSize();
		const float trailingSpace = std::max(ImGui::GetStyle().FramePadding.x, font * .5f);
		float width = 0.0f;
		for (const auto& entry : menuList) {
			if (const auto* menu = std::get_if<FeatureListRenderer::BuiltInMenu>(&entry)) {
				if (IsCoreMenu(menu->name) || menu->name == "Feature Issues")
					width = std::max(width, ImGui::CalcTextSize(menu->name.c_str()).x + font * sidebarIconTextInset + trailingSpace);
			} else if (const auto* category = std::get_if<FeatureListRenderer::CategoryHeader>(&entry)) {
				// Include the icon, a short separator, count and expansion arrow.
				width = std::max(width, ImGui::CalcTextSize(category->name.c_str()).x +
											ImGui::CalcTextSize(std::to_string(category->count).c_str()).x + font * 5.2f);
			} else if (const auto* label = std::get_if<std::string>(&entry)) {
				width = std::max(width, ImGui::CalcTextSize(label->c_str()).x + trailingSpace);
			}
		}
		// Collapsed groups must not narrow the sidebar and clip names when reopened.
		for (auto* feature : FeatureListRenderer::GetMenuFeatures()) {
			if (!feature || (!IsTopLevelFeature(feature) && (!IsFeatureVisible(feature) ||
																(!feature->loaded && FeatureIssues::IsObsoleteFeature(feature->GetShortName()) && !globals::state->IsDeveloperMode()))))
				continue;
			const float inset = IsTopLevelFeature(feature) ? font * sidebarIconTextInset : ImGui::CalcTextSize("     ").x;
			width = std::max(width, ImGui::CalcTextSize(feature->GetDisplayName().c_str()).x + inset + trailingSpace);
		}
		return width + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ScrollbarSize;
	}

	void DrawWeatherOverrideControl(Feature* feature)
	{
		const auto name = feature->GetShortName();
		bool paused = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->IsFeaturePaused(name);
		if (Util::Widgets::Checkbox("Pause Weather Overrides", &paused))
			WeatherManager::GetSingleton()->SetFeaturePaused(name, paused);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::Text("Temporarily disable weather-based setting adjustments for this feature.\nThis state is not saved.");
	}

	bool IsPerformanceMeasurementNavigationLocked(size_t listId, size_t selectedMenu)
	{
		return PerformanceTuningRenderer::HasActiveMeasurements() && listId != selectedMenu;
	}

	const char* BuiltInPageId(const FeatureListRenderer::BuiltInMenu& menu)
	{
		return menu.name == PERFORMANCE_PRESETS_MENU_NAME ? "PerformancePresets" : menu.name == PERFORMANCE_TUNING_MENU_NAME ? "PerformanceTuning" :
		                                                                       menu.name == "Dev Tools"                      ? "DevTools" :
		                                                                                                                       menu.name.c_str();
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
		MenuUI::SectionHeading(text);
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
	auto features = Feature::GetFeatureList();
	if (REL::Module::IsVR())
		features.push_back(&MenuUI::StabilizerPage::Get());
	return features;
}

void FeatureListRenderer::RenderPerformancePage(const char* a_page, const std::function<void()>& a_draw, const char* a_section)
{
	const auto current = a_section ? std::string_view(a_section) : MenuUI::SettingsNavigation::PerformanceSection(a_page);
	const MenuUI::FeatureScope builtinScope(nullptr);
	MenuUI::SettingsPage page(a_page, { { "presets", "Presets", "Choose Performance, Balanced, Quality or MyPerformance.", "Shared performance choices" }, { "streaming", "Texture Streaming", "Control texture detail and graphics-memory use.", "Texture detail, coverage and streaming activity" }, { "timings", "Profiling", "Inspect live CPU and GPU timings for the active scene.", "Live CPU and GPU timings" }, { "tuning", "Performance Tuning", "Scan feature timings or measure individual on/off savings.", "Feature costs and comparisons" }, { "overlay", "Performance Overlay", "Choose in-game counters, graphs, and their appearance.", "Counters, graphs and appearance" } }, "Performance", "Choose presets, manage texture streaming, inspect timings, compare feature costs, or configure the overlay.", [current = std::string(current)](std::string_view section) { return section == current || !PerformanceTuningRenderer::HasActiveMeasurements(); }, a_section ? a_section : "");
	if (page.Is(current)) {
		a_draw();
		return;
	}
	for (const auto& [section, destination] : { std::pair{ "overview", "Performance" }, std::pair{ "presets", "PerformancePresets" },
			 std::pair{ "streaming", "TextureStreaming" }, std::pair{ "timings", "Profiling" }, std::pair{ "tuning", "PerformanceTuning" }, std::pair{ "overlay", "PerformanceOverlay" } }) {
		if (!page.Is(section))
			continue;
		if (!MenuUI::SettingsPage::Select(std::string_view(section) == "streaming" ? "Performance" : destination, section))
			return;
		const auto entry = GetBuiltInPageMenuEntryId(destination);
		globals::menu->SelectFeatureMenu(entry.empty() ? destination : entry);
		break;
	}
}

void FeatureListRenderer::SetSidebarVisible(bool a_visible)
{
	sidebarVisibilityOverride = a_visible;
	sidebarVisible = a_visible;
}

bool FeatureListRenderer::IsSidebarVisible() { return sidebarVisible; }

void FeatureListRenderer::ToggleSearch()
{
	searchVisible = !searchVisible;
	focusSearch = searchVisible;
	if (searchVisible)
		SetSidebarVisible(true);
}

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
	if (auto* feature = MenuUI::PerformancePage::EditedFeature()) {
		DrawMenuVisitor{ pendingFeatureSelection }(feature);
		ImGui::EndChild();
		return;
	}

	if (!pendingFeatureSelection.empty()) {
		featureSearch.clear();
		for (auto* feature : GetMenuFeatures()) {
			if (IsFeatureVisible(feature) && feature->GetShortName() == pendingFeatureSelection) {
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
	auto menuList = BuildMenuList({}, categoryExpansionStates, drawGeneralSettings, drawAdvancedSettings);
	if (!selectedMenuEntryId.empty() && !TrySelectMenuEntryById(menuList, selectedMenuEntryId, selectedMenu))
		selectedMenu = menuList.size();

	HandlePendingFeatureSelection(pendingFeatureSelection, menuList, selectedMenu);
	SelectFallbackMenuEntry(menuList, selectedMenu);

	// Determine if left panel should be visible based on auto-hide settings
	const bool leftPanelVisible = sidebarVisible = ShouldShowLeftPanel();

	// Create the table with appropriate number of columns based on visibility
	int numColumns = leftPanelVisible ? 2 : 1;
	if (ImGui::BeginTable(leftPanelVisible ? "Menus Table" : "Menus Table Hidden", numColumns, ImGuiTableFlags_SizingStretchProp)) {
		if (leftPanelVisible) {
			ImGui::TableSetupColumn("##ListOfMenus", 0, MenuUI::Layout::DefaultSidebarRatio);
			ImGui::TableSetupColumn("##MenuConfig", 0, 1.0f - MenuUI::Layout::DefaultSidebarRatio);
			MenuUI::Layout::ConstrainSidebar(resetSidebarLayout, MeasureSidebarWidth(menuList));
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

	auto menuList = std::vector<MenuFuncInfo>{
		BuiltInMenu{ "Home", [] { HomePageRenderer::RenderHomePage(); } },
		BuiltInMenu{ "Settings", drawGeneralSettings },
		&globals::features::adaptiveBrightness,
		BuiltInMenu{ "Performance", [] { FeatureListRenderer::RenderPerformancePage("Performance", [] {}); } },
		BuiltInMenu{ "Dev Tools", drawAdvancedSettings },
		&globals::features::screenshotFeature,
		BuiltInMenu{ PERFORMANCE_PRESETS_MENU_NAME, [] { FeatureListRenderer::RenderPerformancePage("PerformancePresets", [] { MenuUI::PerformancePage::RenderPresets(); }); } },
		&TextureStreaming::Instance(),
		BuiltInMenu{ "Profiling", [] { FeatureListRenderer::RenderPerformancePage("Profiling", [] { ProfilingRenderer::RenderStatistics(true, true, false); }); } },
		BuiltInMenu{ PERFORMANCE_TUNING_MENU_NAME, [] { PerformanceTuningRenderer::Render(); } },
		&globals::features::renderDoc,
		&globals::features::performanceOverlay
	};

	// Group features by category
	std::map<std::string, std::vector<Feature*>> categorizedFeatures;
	for (Feature* feat : sortedFeatureList) {
		if (IsFeatureVisible(feat) && (feat->loaded || dynamic_cast<MenuUI::ExternalSettingsPage*>(feat))) {
			std::string category(feat->GetCategory());
			categorizedFeatures[category].push_back(feat);
		}
	}

	for (const auto category : FeatureCategories::kMenuOrder)
		if (FeatureCategories::AlwaysExpanded(category))
			categoryExpansionStates[std::string(category)] = true;

	// Add categorized features to menu with collapsible headers
	for (const auto categoryName : FeatureCategories::kMenuOrder) {
		const std::string category(categoryName);
		if (categorizedFeatures.find(category) != categorizedFeatures.end() && !categorizedFeatures[category].empty()) {
			// Initialize expansion state if not exists
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = false;  // Secondary categories start collapsed.
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
				categoryExpansionStates[category] = false;  // Secondary categories start collapsed.
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

void FeatureListRenderer::HandlePendingFeatureSelection(
	std::string& pendingFeatureSelection,
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu)
{
	if (!pendingFeatureSelection.empty()) {
		const char* performancePage = pendingFeatureSelection == "builtin:Performance Presets" ? "PerformancePresets" :
		                              pendingFeatureSelection == "builtin:Profiling"           ? "Profiling" :
		                              pendingFeatureSelection == "builtin:Performance Tuning"  ? "PerformanceTuning" :
		                              pendingFeatureSelection == "PerformanceOverlay"          ? "PerformanceOverlay" :
		                                                                                         nullptr;
		if (performancePage)
			MenuUI::SettingsPage::Select(performancePage, MenuUI::SettingsNavigation::PerformanceSection(performancePage).data());
		if (pendingFeatureSelection == "TextureStreaming")
			MenuUI::SettingsPage::Select("Performance", "streaming");
		if (pendingFeatureSelection == globals::features::renderDoc.GetShortName())
			MenuUI::SettingsPage::Select("DevTools", "renderdoc");
		if (pendingFeatureSelection.starts_with("builtin:") && TrySelectMenuEntryById(menuList, pendingFeatureSelection, selectedMenu)) {
			pendingFeatureSelection.clear();
			return;
		}
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
		for (size_t i = 0; i < menuList.size(); ++i) {
			const auto* menu = std::get_if<BuiltInMenu>(&menuList[i]);
			const auto* feature = std::get_if<Feature*>(&menuList[i]);
			if ((menu && IsCoreMenu(menu->name)) || (feature && IsTopLevelFeature(*feature))) {
				const auto* selectedBuiltIn = selectedMenu < menuList.size() ? std::get_if<BuiltInMenu>(&menuList[selectedMenu]) : nullptr;
				const auto* selectedFeature = selectedMenu < menuList.size() ? std::get_if<Feature*>(&menuList[selectedMenu]) : nullptr;
				const bool groupSelected = menu &&
				                           ((menu->name == "Performance" && ((selectedBuiltIn && MenuUI::SettingsNavigation::IsPerformancePage(BuiltInPageId(*selectedBuiltIn))) ||
																				(selectedFeature && IsPerformanceFeature(*selectedFeature)))) ||
											   (menu->name == "Dev Tools" && selectedFeature && *selectedFeature == &globals::features::renderDoc));
				std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates, groupSelected }, menuList[i]);
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
		if (searchVisible) {
			if (focusSearch) {
				ImGui::SetKeyboardFocusHere();
				focusSearch = false;
			}
			Util::DrawFeatureSearchBar(featureSearch);
		} else {
			featureSearch.clear();
		}

		if (!MenuUI::SettingsSearch::Draw(featureSearch)) {
			for (size_t i = 0; i < menuList.size(); i++) {
				if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
					const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
					if (IsCoreMenu(menu.name) || menu.name == "Home" || MenuUI::SettingsNavigation::IsPerformancePage(BuiltInPageId(menu))) {
						continue;
					}
				}
				if (const auto* feature = std::get_if<Feature*>(&menuList[i]); feature &&
																			   (IsTopLevelFeature(*feature) || *feature == &globals::features::renderDoc || IsPerformanceFeature(*feature)))
					continue;
				std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates }, menuList[i]);
			}
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
		                       MenuUI::SettingsPage::Selected("PerformanceTuning") == "tuning" :
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
		std::pair{ "Settings", Util::ActionGlyph::General },
		std::pair{ "Dev Tools", Util::ActionGlyph::Advanced },
		std::pair{ "Performance", Util::ActionGlyph::Performance },
		std::pair{ "Profiling", Util::ActionGlyph::Profiling },
		std::pair{ "Performance Tuning", Util::ActionGlyph::PerformanceTuning }
	};
	const auto icon = std::ranges::find_if(icons, [&](const auto& entry) { return menu.name == entry.first; });
	const bool navigationLocked = IsPerformanceMeasurementNavigationLocked(listId, selectedMenuRef);
	const auto guard = Util::DisableGuard(navigationLocked);
	const auto start = ImGui::GetCursorScreenPos();
	if (selectedMenuRef == listId || groupSelected)
		DrawSidebarSelection(start, { start.x + ImGui::GetContentRegionAvail().x, start.y + ImGui::GetTextLineHeight() * 2 });
	if (ImGui::Selectable(("##" + menu.name).c_str(), false, ImGuiSelectableFlags_SpanAllColumns, { 0, ImGui::GetTextLineHeight() * 2 })) {
		selectedMenuRef = listId;
		if (menu.name == "Performance")
			MenuUI::SettingsPage::Open("Performance");
		else if (menu.name == "Dev Tools")
			MenuUI::SettingsPage::Open("DevTools");
	}
	const auto maximum = ImGui::GetItemRectMax();
	const auto color = menu.name == "Feature Issues" ?
	                       Util::Color::Readiness(false) :
	                       ImGui::GetStyleColorVec4(ImGuiCol_Text);
	DrawSidebarLabel(menu.name.c_str(), icon != icons.end() ? icon->second : Util::ActionGlyph::Texture, start, maximum, color);
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
		Util::DrawCategoryHeader(header.name.c_str(), isExpanded, header.count, !FeatureCategories::AlwaysExpanded(header.name));
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
		textColor = feat->version.empty() ? themeSettings.StatusPalette.Disable : Util::Colors::GetError();
	} else if (!feat->conflictMessage.empty()) {
		textColor = Util::Colors::GetWarning();
	} else {
		// No failed message but not loaded - check if INI file exists
		if (!std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(feat->GetShortName()))) {
			// INI file missing - treat as missing feature (grey)
			textColor = themeSettings.StatusPalette.Disable;
		} else {
			// Installed features awaiting restart need attention.
			textColor = Util::Colors::GetWarning();
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
	if (selected)
		DrawSidebarSelection(rowStart, { rowStart.x + rowWidth, rowStart.y + rowHeight });

	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	{
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0, .5f });
		const SKSE::stl::scope_exit restoreAlign([] { ImGui::PopStyleVar(); });
		if (feat == &globals::features::csEditor) {
			globals::features::csEditor.DrawLauncherRow(labelWidth, rowHeight);
		} else if (IsTopLevelFeature(feat)) {
			if (ImGui::Selectable("##TopLevelFeature", false, ImGuiSelectableFlags_None, { labelWidth, rowHeight }))
				selectedMenuRef = listId;
			DrawSidebarLabel(feat->GetDisplayName().c_str(), feat == &globals::features::screenshotFeature ? Util::ActionGlyph::Camera : Util::ActionGlyph::Visuals, rowStart, ImGui::GetItemRectMax(), textColor);
		} else if (ImGui::Selectable(fmt::format("    {} ", feat->GetDisplayName()).c_str(), false, ImGuiSelectableFlags_None, { labelWidth, rowHeight })) {
			selectedMenuRef = listId;
		}
	}
	ImGui::PopStyleColor();
	if (external ? external->HasUnsavedChanges() : globals::menu->HasUnsavedFeatureSettings(feat->GetName())) {
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
		const bool isHome = menu.name == "Home";
		std::optional<MenuUI::PageNoticeScope> notices;
		if (!isHome)
			notices.emplace();
		const char* description = isHome                                     ? "" :
		                          menu.name == "Settings"                    ? "Configure startup features, controls, display options, and menu appearance." :
		                          menu.name == "Dev Tools"                   ? "Manage RenderDoc captures, shaders, caches, logging, and renderer tests." :
		                          menu.name == "Performance"                 ? "Choose performance presets, inspect timings, compare feature costs, and configure the overlay." :
		                          menu.name == "Profiling"                   ? "Inspect live CPU and GPU timings in the current scene." :
		                          menu.name == PERFORMANCE_PRESETS_MENU_NAME ? "Choose a performance preset or build MyPerformance from your saved choices." :
		                          menu.name == PERFORMANCE_TUNING_MENU_NAME  ? "Scan rendering costs or compare enabled features in the current scene." :
		                          menu.name == "Feature Issues"              ? "Review feature compatibility and available recovery actions." :
		                                                                       "";
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
					width = toggleSize + ImGui::GetStyle().ItemInnerSpacing.x + controlFont->CalcTextSizeA(controlFontSize, FLT_MAX, 0, "Enabled").x;
				}
				const bool stacked = available < width + font * 15;
				const auto start = ImGui::GetCursorScreenPos();
				const float titleHeight = DrawFeatureHeader(menu.name, description, stacked ? 0 : width + font);
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
				DrawFeatureHeader(menu.name, description);
			}
		}
		if (notices)
			notices->Reserve(MenuUI::SettingsPage::OverviewTextInset(pageId, ImGui::GetContentRegionAvail().x));
		const float footerSpace = isHome ? 0.0f : SettingsFooterHeight(pageId) + ImGui::GetStyle().ItemSpacing.y;
		if (ImGui::BeginChild("##BuiltInBody", { 0, -footerSpace }, ImGuiChildFlags_None)) {
			const MenuUI::SettingsSearch::PageScope searchScope(pageId);
			menu.func();
		}
		ImGui::EndChild();
		if (!isHome)
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
		featureInstalled = isLoaded || IsFeatureInstalled(featureName);
		MenuUI::PageNoticeScope notices;
		RenderFeatureHeader(feat, isDisabled, isLoaded, sceneControlled);
		notices.Reserve(MenuUI::SettingsPage::OverviewTextInset(featureName.c_str(), ImGui::GetContentRegionAvail().x));

		const bool profileEdit = MenuUI::PerformancePage::EditedFeature() == feat;
		auto externalFooter = profileEdit ? std::optional(MenuUI::PerformancePage::FeatureEditFooter()) : external ? std::optional(external->GetSettingsFooter()) :
		                                                                                                             std::nullopt;
		const float textInset = MenuUI::SettingsPage::OverviewTextInset(featureName.c_str(), ImGui::GetContentRegionAvail().x);
		weatherControlInFooter = feat == &globals::features::adaptiveBrightness && isLoaded && !isDisabled &&
		                         MenuUI::SettingsPage::Selected(featureName.c_str()) == "overview" &&
		                         WeatherVariables::GlobalWeatherRegistry::GetSingleton()->HasWeatherSupport(featureName);
		const float weatherControlHeight = weatherControlInFooter ? std::max(ImGui::GetFrameHeight(), Util::Widgets::CheckboxSize()) + ImGui::GetStyle().ItemSpacing.y : 0.0f;
		{
			MenuUI::FeatureScope featureScope(feat);
			if (ImGui::BeginChild("##FeatureBody", { 0, -SettingsFooterHeight(featureName.c_str(), externalFooter ? &*externalFooter : nullptr) - weatherControlHeight - ImGui::GetStyle().ItemSpacing.y }, ImGuiChildFlags_None)) {
				const MenuUI::SettingsSearch::PageScope searchScope(featureName);
#ifdef DEVBENCH_BRIDGE_ENABLED
				MenuUI::DevBenchViewport viewport(featureName, "legacy", !PerformanceTuningRenderer::HasActiveMeasurements());
#endif
				if (external)
					external->DrawSettings();
				else if (feat == &TextureStreaming::Instance())
					FeatureListRenderer::RenderPerformancePage("Performance", [&] { RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled); }, "streaming");
				else if (feat == &globals::features::performanceOverlay)
					FeatureListRenderer::RenderPerformancePage("PerformanceOverlay", [&] { RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled); });
				else if (feat == &globals::features::renderDoc)
					AdvancedSettingsRenderer::RenderRenderDocSettings([&] { RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled); });
				else
					RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled);
			}
			ImGui::EndChild();
		}

		if (weatherControlInFooter) {
			ImGui::Indent(textInset);
			const SKSE::stl::scope_exit restoreIndent([textInset] { ImGui::Unindent(textInset); });
			DrawWeatherOverrideControl(feat);
		}
		// Keep save, load and feature defaults together in the fixed footer.
		if (profileEdit) {
			externalFooter = MenuUI::PerformancePage::FeatureEditFooter();
			DrawSettingsFooter(featureName.c_str(), {}, &*externalFooter, feat);
		} else if (external) {
			externalFooter = external->GetSettingsFooter();
			DrawSettingsFooter(featureName.c_str(), {}, &*externalFooter, feat);
		} else if (feat == &globals::features::adaptiveBrightness && MenuUI::SettingsPage::Selected(featureName.c_str()) == "presets") {
			DrawSettingsFooter(featureName.c_str());
		} else {
			DrawSettingsFooter(featureName.c_str(), [&](float width) { RenderRestoreDefaultsButton(feat, isDisabled || sceneControlled, isLoaded, width); }, nullptr, feat);
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
	if (!feat->failedLoadedMessage.empty() && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(featureName))
		status = { "Unavailable", true, feat->failedLoadedMessage };
	else if (isDisabled && featureName == "WetnessEffects" && globals::features::wetterness.loaded)
		status = { "Conflict", true, "Wetness Effects cannot run with Wetterness and is automatically disabled while Wetterness is active." };
	else if (isDisabled)
		status = isLoaded ? Feature::SettingsHeaderStatus{ "Restart pending", true, "Save settings and restart the game to unload this feature.", true } :
		                    Feature::SettingsHeaderStatus{ "Off", true, "Enable this feature in Settings > Startup, save settings and restart the game." };
	else if (!isLoaded) {
		if (!feat->conflictMessage.empty())
			status = { "Conflict", true, feat->conflictMessage };
		else if (featureInstalled)
			status = { "Restart required", true, "This feature is installed but not loaded. Save settings and restart the game.", true };
		else
			status = { "Not installed", true, std::format("Install {}.ini version {} or later, then restart the game.", featureName, Feature::GetFeatureRequiredVersion(featureName)) };
	} else if (!feat->conflictMessage.empty())
		status = { "Conflict", true, feat->conflictMessage };
	else if (sceneControlled)
		status = { "Scene settings", true, "Scene-specific settings are controlling this feature. Pause Scene Specific Settings below to edit it." };
	else if (const auto* failure = PerformanceTuningRenderer::GetFeatureToggleFailureReason(feat))
		status = { "Attention", true, failure };
	const auto color = status.pendingChange ? Util::Colors::GetWarning() : Util::Color::Readiness(!status.needsAttention);
	ImFont* statusFont;
	float statusFontSize, statusWidth, enabledTextWidth;
	{
		MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
		statusFont = ImGui::GetFont();
		statusFontSize = ImGui::GetStyle().FontSizeBase * featureDescriptionScale;
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
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, std::max(0.0f, (toggleSize - ImGui::GetFontSize()) * .5f) });
		const SKSE::stl::scope_exit restoreTogglePadding([] { ImGui::PopStyleVar(); });
		const auto statusStart = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddCircleFilled({ statusStart.x + font * .25f, statusStart.y + ImGui::GetFrameHeight() * .5f }, font * .2f, ImGui::GetColorU32(color));
		ImGui::Dummy({ font * .75f, ImGui::GetFrameHeight() });
		ImGui::SameLine(0, 0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(color, "%.*s", static_cast<int>(status.label.size()), status.label.data());
		Util::AddTooltip(status.message.empty() ? "This feature is ready." : status.message.c_str());
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
	if (const auto* failure = PerformanceTuningRenderer::GetFeatureToggleFailureReason(feat))
		status = { "Attention", true, failure };
	if (status.needsAttention && !status.message.empty())
		MenuUI::PageNotice(status.message, status.pendingChange ? MenuUI::NoticeKind::Pending : MenuUI::NoticeKind::Unavailable);
}

bool FeatureListRenderer::DrawMenuVisitor::RenderFeatureContentControls(Feature* feat, bool sceneControlled)
{
	if (feat == &globals::features::adaptiveBrightness && MenuUI::SettingsPage::Selected("AdaptiveBrightness") == "presets")
		return false;
	const auto featureName = feat->GetShortName();
	auto weatherRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	if (!weatherControlInFooter && weatherRegistry->HasWeatherSupport(featureName)) {
		DrawWeatherOverrideControl(feat);
		ImGui::Spacing();
	}

	// Keep the pause control editable while scene overrides own the feature.
	{
		const auto& featureShortName = feat->GetShortName();
		auto* sceneMgr = SceneSettingsManager::GetSingleton();
		bool scenePaused = sceneMgr->IsFeaturePaused(featureShortName);
		if (sceneControlled || scenePaused) {
			bool active = !scenePaused;
			if (Util::Widgets::Checkbox("Scene Specific Settings##PauseSceneSettings", &active))
				sceneMgr->SetFeaturePaused(featureShortName, !active);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(scenePaused ? "Paused - click to resume" : "Active - click to pause");
			}
			ImGui::Spacing();
		}
		sceneControlled = sceneMgr->HasActiveSettingsForFeature(featureShortName) && !sceneMgr->IsFeaturePaused(featureShortName);
	}

	auto* overrideManager = SettingsOverrideManager::GetSingleton();
	if (overrideManager && overrideManager->HasFeatureOverrides(featureName)) {
		const char* label = "Apply Override";
		const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
		const float left = ImGui::GetCursorScreenPos().x + std::max(0.0f, ImGui::GetContentRegionAvail().x - width);
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

	{
		auto guard = Util::DisableGuard(sceneControlled);
		feat->DrawSettingsHeaderControls();
	}
	DrawFeatureBanner(feat);
	return sceneControlled;
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureSettings(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage, bool sceneControlled)
{
	if (isDisabled) {
		return;
	} else {
		if (isLoaded) {
			MenuUI::FeatureScope contentScope(feat, [&] { return RenderFeatureContentControls(feat, sceneControlled); });
			const ImVec2 cursorPosBefore = ImGui::GetCursorPos();
			if (feat == &globals::features::renderDoc || feat == &globals::features::performanceOverlay) {
				// These tools already live inside their parent section's tab content.
				const auto disabled = Util::DisableGuard(contentScope.DrawContentControls());
				feat->DrawSettings();
			} else {
				feat->DrawSettings();
			}
			const ImVec2 cursorPosAfter = ImGui::GetCursorPos();

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
				MenuUI::PageNotice(MenuUI::NoAdditionalSettingsMessage);
			}

		} else {
			if (!hasFailedMessage && feat->conflictMessage.empty() && !featureInstalled) {
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
}

void FeatureListRenderer::DrawMenuVisitor::RenderRestoreDefaultsButton(Feature* feat, bool isDisabled, bool isLoaded, float availableWidth)
{
	const auto guard = Util::DisableGuard(isDisabled || !isLoaded);
	const bool restoreDefaults = SettingsActionButton(2, nullptr, availableWidth);

	if (restoreDefaults) {
		MenuUI::InvokeFeatureSettingsAction(MenuUI::GlobalSettingsAction::RestoreDefaults, feat);
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
		ImGui::TextWrapped("Some settings were adjusted automatically to resolve feature incompatibilities.");
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
			"Affected controls stay disabled in their feature pages while these constraints apply. Change the constraining features to release them.");

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
