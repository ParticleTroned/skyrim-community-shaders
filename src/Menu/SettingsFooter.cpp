#include "SettingsFooter.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "SettingsActions.h"
#include "SettingsPage.h"
#include "Utils/UI.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <imgui.h>

namespace MenuUI
{
	namespace
	{
		constexpr float textScale = 14.0f / 12.0f;
		constexpr float buttonPadding = 1.85f;
		constexpr float buttonHeight = 2.1f;
		constexpr float verticalPadding = .15f;
		constexpr size_t actionCount = 3;

		float TextWidth(const char* label)
		{
			return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * textScale, FLT_MAX, 0, label).x;
		}

		float ActionSpacing(float availableWidth)
		{
			return std::clamp(ImGui::GetStyle().ItemSpacing.x, 0.0f, std::max(0.0f, (availableWidth - actionCount) / (actionCount - 1)));
		}

		std::array<const char*, actionCount> ActionLabels(float width)
		{
			const std::array full{ T("menu.footer.save_settings", "Save settings"), T("menu.footer.load_saved", "Load saved"), T("menu.footer.restore_defaults", "Restore defaults") };
			const std::array compact{ T("menu.footer.save", "Save"), T("menu.footer.load", "Load"), T("menu.footer.defaults", "Defaults") };
			const auto fits = [&](const auto& labels) {
				return TextWidth(labels[0]) + TextWidth(labels[1]) + TextWidth(labels[2]) +
				           ImGui::GetFontSize() * buttonPadding * actionCount + ActionSpacing(width) * (actionCount - 1) <=
				       width;
			};
			return fits(full) ? full : fits(compact) ? compact :
			                                           std::array{ "", "", "" };
		}

		float ActionWidth(size_t index, float availableWidth)
		{
			const auto* label = ActionLabels(availableWidth)[index];
			const float requested = TextWidth(label) + ImGui::GetFontSize() * buttonPadding;
			const float limit = *label ? availableWidth : (availableWidth - ActionSpacing(availableWidth) * (actionCount - 1)) / actionCount;
			return std::min(requested, std::max(1.0f, limit));
		}

		bool ActionButton(size_t index, float availableWidth)
		{
			const auto* label = ActionLabels(availableWidth)[index];
			const float font = ImGui::GetFontSize();
			const float width = ActionWidth(index, availableWidth);
			constexpr std::array glyphs{ Util::ActionGlyph::SaveSettings, Util::ActionGlyph::LoadSettings, Util::ActionGlyph::RestoreDefaults };
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * textScale);
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 1, .5f });
			ImGui::PushID(static_cast<int>(index));
			const SKSE::stl::scope_exit restore([] { ImGui::PopID(); ImGui::PopStyleVar(); ImGui::PopFont(); });
			const bool clicked = ImGui::Button((std::string(label) + "###SettingsAction").c_str(), { width, font * buttonHeight });
			const auto minimum = ImGui::GetItemRectMin();
			const float extent = std::min(font, std::max(1.0f, width - font * .6f));
			const float x = minimum.x + (*label ? font * .3f : (width - extent) * .5f);
			const float y = minimum.y + (ImGui::GetItemRectSize().y - extent) * .5f;
			Util::DrawActionGlyph(ImGui::GetWindowDrawList(), glyphs[index], { x, y }, { x + extent, y + extent }, ImGui::GetColorU32(ImGuiCol_Text));
			return clicked;
		}

		std::array<const char*, 3> StatusLabels()
		{
			return { T("menu.footer.save_failed", "CSX settings not saved"), T("menu.footer.unsaved", "Unsaved CSX settings"), T("menu.footer.saved", "CSX settings saved") };
		}

		struct FooterLayout
		{
			float left, right, statusLeft, statusWidth, actionsLeft, actionsWidth;
		};

		FooterLayout Layout(const char* page)
		{
			const float width = ImGui::GetContentRegionAvail().x;
			const float font = ImGui::GetFontSize();
			const auto insets = SettingsPage::OverviewCardInsets(page, width);
			const float left = std::min(insets.x, std::max(0.0f, width - insets.y - font * 4));
			const float gutter = std::max(0.0f, std::min(font * .75f, (width - left - insets.y) * .05f));
			const float statusLeft = left + gutter;
			const float right = std::max(statusLeft + 1.0f, width - insets.y - gutter);
			const float contentWidth = right - statusLeft;
			const auto labels = StatusLabels();
			const float statusWidth = font + std::max({ TextWidth(labels[0]), TextWidth(labels[1]), TextWidth(labels[2]) });
			const float minimumActions = font * buttonPadding * actionCount + ImGui::GetStyle().ItemSpacing.x * (actionCount - 1);
			const float reserve = std::min(statusWidth, std::max(font, contentWidth - minimumActions - font));
			const float actionWidth = std::max(static_cast<float>(actionCount), contentWidth - reserve - font);
			float actions = ActionSpacing(actionWidth) * (actionCount - 1);
			for (size_t index = 0; index < actionCount; ++index)
				actions += ActionWidth(index, actionWidth);
			return { left, width - insets.y, statusLeft, std::max(1.0f, right - actions - statusLeft - font), right - actions, actionWidth };
		}
	}

	float SettingsFooterHeight(const char*)
	{
		return 1 + ImGui::GetFontSize() * (verticalPadding * 2 + buttonHeight);
	}

	bool DrawSettingsDefaultButton(float availableWidth)
	{
		return ActionButton(2, availableWidth);
	}

	void DrawSettingsFooter(const char* page, const std::function<void(float)>& defaults)
	{
		const auto start = ImGui::GetCursorScreenPos();
		const auto layout = Layout(page);
		const float font = ImGui::GetFontSize();
		const float height = SettingsFooterHeight(page);
		const auto& menu = *globals::menu;
		const auto& palette = menu.GetTheme().StatusPalette;
		const bool error = menu.IsSettingsSaveMessageError() && !menu.GetSettingsSaveMessage().empty();
		const bool dirty = menu.HasUnsavedSettings();
		const auto* status = StatusLabels()[error ? 0 : dirty ? 1 :
																2];
		const std::string detail = error ? menu.GetSettingsSaveMessage() : dirty ? T("menu.footer.save_choices", "Save to keep your current choices.") :
		                                                                           T("menu.footer.choices_current", "Your choices are up to date.");
		const auto color = error ? palette.Error : dirty ? palette.Warning :
		                                                   Util::Color::SecondaryText();
		const auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const bool light = background.x + background.y + background.z > 1.5f;
		const ImVec4 lift = light ? ImVec4(-.02f, -.02f, -.02f, 0) : ImVec4(.09f, .105f, .11f, 0);
		const ImVec4 surface{ std::clamp(background.x + lift.x, 0.0f, 1.0f), std::clamp(background.y + lift.y, 0.0f, 1.0f), std::clamp(background.z + lift.z, 0.0f, 1.0f), SettingsSurfaceOpacityScale };
		auto* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled({ start.x + layout.left, start.y }, { start.x + layout.right, start.y + height }, ImGui::GetColorU32(surface));
		draw->AddLine({ start.x + layout.left, start.y }, { start.x + layout.right, start.y }, ImGui::GetColorU32(ImGuiCol_Separator));
		const ImVec2 statusStart{ start.x + layout.statusLeft, start.y + (height - font * textScale) * .5f };
		{
			draw->PushClipRect(statusStart, { statusStart.x + layout.statusWidth, start.y + height }, true);
			const SKSE::stl::scope_exit restoreClip([draw] { draw->PopClipRect(); });
			draw->AddCircleFilled({ statusStart.x + font * .35f, start.y + height * .5f }, font * .26f, ImGui::GetColorU32(color));
			draw->AddText(nullptr, font * textScale, { statusStart.x + font, statusStart.y }, ImGui::GetColorU32(color), status);
		}
		ImGui::SetCursorScreenPos(statusStart);
		ImGui::Dummy({ layout.statusWidth, font * textScale });
		Util::AddTooltip((std::string(status) + "\n" + detail).c_str());
		const float actionsY = start.y + 1 + font * verticalPadding;
		float x = start.x + layout.actionsLeft;
		constexpr std::array actions{ GlobalSettingsAction::Save, GlobalSettingsAction::Load, GlobalSettingsAction::RestoreDefaults };
		for (size_t index = 0; index < actions.size(); ++index) {
			ImGui::SetCursorScreenPos({ x, actionsY });
			const auto* reason = GetGlobalSettingsActionBlockReason(actions[index]);
			{
				const auto disabled = Util::DisableGuard(reason != nullptr);
				if (index == 2 && defaults)
					defaults(layout.actionsWidth);
				else if (ActionButton(index, layout.actionsWidth))
					InvokeGlobalSettingsAction(actions[index]);
			}
			if (index != 2 || !defaults || reason)
				Util::AddTooltip(reason ? reason : GetGlobalSettingsActionHelp(actions[index]), ImGuiHoveredFlags_AllowWhenDisabled);
			x += ActionWidth(index, layout.actionsWidth) + ActionSpacing(layout.actionsWidth);
		}
		ImGui::SetCursorScreenPos({ start.x, start.y + height });
		ImGui::Dummy({ 0, 0 });
	}
}
