#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "NumericEntry.h"
#include "UI.h"
#include "UISmoothing.h"
#include <array>
#include <cstring>
#include <format>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

namespace Util::Widgets
{
	namespace
	{
		thread_local int controlLayout = 0;
		constexpr float controlTextScale = 14.0f / 12.0f;
		constexpr float controlPadding = .35f;
		constexpr float numberTargetWidth = 4.5f;
		constexpr float handleRadius = .6f;

		struct ControlStyle
		{
			explicit ControlStyle(bool scaleText)
			{
				ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * (scaleText ? controlTextScale : 1.0f));
				const float font = ImGui::GetFontSize();
				const float grab = std::max(ImGui::GetStyle().GrabMinSize, font * handleRadius * 2);
				const float padding = std::max({ ImGui::GetStyle().FramePadding.y, font * controlPadding, (grab - font) * .5f });
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, padding });
				ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, grab);
			}
			~ControlStyle()
			{
				ImGui::PopStyleVar(2);
				ImGui::PopFont();
			}
		};

		struct ControlRow
		{
			float indent = 0;
			bool active;
			explicit ControlRow(const char* label) : active(controlLayout != 0 && ImGui::FindRenderedTextEnd(label) != label)
			{
				if (!active)
					return;
				ImGui::BeginGroup();
				const float startX = ImGui::GetCursorPosX();
				const float font = ImGui::GetFontSize();
				const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
				const bool stacked = available < font * (controlLayout == 2 ? 16 : 24);
				const float labelWidth = controlLayout == 2 ? available * .30f : available * .20f;
				const float fieldWidth = controlLayout == 2 ? available - labelWidth - font : std::min(available - labelWidth - font, std::max(font * 18, available * .50f));
				ImGui::AlignTextToFramePadding();
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (stacked ? available : labelWidth));
				ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
				ImGui::PopTextWrapPos();
				if (!stacked) {
					ImGui::SameLine(0, 0);
					const float offset = labelWidth + font;
					indent = offset;
					ImGui::Indent(indent);
					ImGui::SetCursorPosX(startX + offset);
				}
				ImGui::PushItemWidth(std::max(1.0f, stacked ? available : fieldWidth));
			}
			~ControlRow()
			{
				if (!active)
					return;
				ImGui::PopItemWidth();
				if (indent > 0)
					ImGui::Unindent(indent);
				const auto control = GImGui->LastItemData;
				ImGui::EndGroup();
				const auto group = GImGui->LastItemData;
				GImGui->LastItemData = control;
				GImGui->LastItemData.Rect = group.Rect;
				GImGui->LastItemData.StatusFlags |= group.StatusFlags & ImGuiItemStatusFlags_HoveredRect;
			}
		};

		template <class T>
		float SliderPosition(ImGuiDataType type, T value, T minimum, T maximum, bool logarithmic, float epsilon, float deadzone)
		{
			if constexpr (std::is_floating_point_v<T>)
				return ImGui::ScaleRatioFromValueT<T, T, T>(type, value, minimum, maximum, logarithmic, epsilon, deadzone);
			else if constexpr (sizeof(T) <= sizeof(int))
				return ImGui::ScaleRatioFromValueT<T, std::make_signed_t<T>, float>(type, value, minimum, maximum, logarithmic, epsilon, deadzone);
			else
				return ImGui::ScaleRatioFromValueT<T, std::make_signed_t<T>, double>(type, value, minimum, maximum, logarithmic, epsilon, deadzone);
		}

		template <class T>
		T RoundEntry(T value, T minimum, T maximum, ImGuiDataType type, const char* format, ImGuiSliderFlags flags)
		{
			if constexpr (std::is_floating_point_v<T>)
				if (!(flags & ImGuiSliderFlags_NoRoundToFormat))
					value = ImGui::RoundScalarWithFormatT<T>(format, type, value);
			return std::clamp(value, minimum, maximum);
		}

		template <class T>
		T SliderValue(ImGuiDataType type, float ratio, T minimum, T maximum, bool logarithmic, float epsilon, float deadzone)
		{
			if constexpr (std::is_floating_point_v<T>)
				return ImGui::ScaleValueFromRatioT<T, T, T>(type, ratio, minimum, maximum, logarithmic, epsilon, deadzone);
			else if constexpr (sizeof(T) <= sizeof(int))
				return ImGui::ScaleValueFromRatioT<T, std::make_signed_t<T>, float>(type, ratio, minimum, maximum, logarithmic, epsilon, deadzone);
			else
				return ImGui::ScaleValueFromRatioT<T, std::make_signed_t<T>, double>(type, ratio, minimum, maximum, logarithmic, epsilon, deadzone);
		}

		struct SliderGeometry
		{
			float start, finish, epsilon, deadzone;
		};

		template <class T>
		SliderGeometry GetSliderGeometry(T minimum, T maximum, const char* format)
		{
			const auto& style = ImGui::GetStyle();
			const auto trackMin = ImGui::GetItemRectMin();
			const auto trackMax = ImGui::GetItemRectMax();
			const float span = trackMax.x - trackMin.x - 4;
			float grab = std::min(style.GrabMinSize, span);
			if constexpr (std::is_integral_v<T>) {
				const auto range = static_cast<long double>(maximum) - static_cast<long double>(minimum);
				grab = std::min(span, std::max(grab, static_cast<float>(span / (range + 1))));
			}
			const float start = trackMin.x + 2 + grab * .5f;
			const float finish = trackMax.x - 2 - grab * .5f;
			const float epsilon = std::pow(.1f, static_cast<float>(std::max(0, ImParseFormatPrecision(format, 3))));
			const float deadzone = style.LogSliderDeadzone * .5f / std::max(1.0f, finish - start);
			return { start, finish, epsilon, deadzone };
		}

		template <class T>
		bool DrawSlider(const char* label, ImGuiDataType type, T* value, T minimum, T maximum, const char* format, ImGuiSliderFlags flags)
		{
			if (ImGui::GetCurrentWindow()->SkipItems)
				return false;
			if (!std::isfinite(static_cast<double>(*value)) || !std::isfinite(static_cast<double>(minimum)) ||
				!std::isfinite(static_cast<double>(maximum)) || minimum > maximum) {
				ImGui::TextDisabled("%s: unavailable", label);
				Util::AddTooltip(T("ui.slider.this_value_is_unavailable_load_saved_settings_or_restore_defaults", "This value is unavailable. Load saved settings or restore defaults."));
				return false;
			}

			const ControlStyle controlStyle(controlLayout != 0);
			const ControlRow row(label);
			ImGui::PushID(label);
			const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
			ImGui::BeginGroup();
			SKSE::stl::scope_exit endGroup([] { ImGui::EndGroup(); });
			const bool readOnly = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0 ||
			                      (flags & static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_ReadOnly)) != 0;
			const bool locked = readOnly || (flags & ImGuiSliderFlags_NoInput) != 0;
			const float requestedWidth = ImGui::CalcItemWidth();
			const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
			const auto* digit = ImGui::GetFontBaked()->FindGlyph('0');
			const float numberFontSize = ImGui::GetFontSize() * (ImGui::GetFontSize() * handleRadius * 2 * .75f) / std::max(1.0f, digit->Y1 - digit->Y0);
			char formatted[128]{};
			ImGui::DataTypeFormatString(formatted, IM_ARRAYSIZE(formatted), type, value, format);
			const float valueWidth = std::min(requestedWidth * 0.45f, std::max(ImGui::GetFontSize() * numberTargetWidth, ImGui::GetFont()->CalcTextSizeA(numberFontSize, FLT_MAX, 0, formatted).x + gap * 2));
			const char* inputFormat = std::strchr(format, '%') ? format : ImGui::DataTypeGetInfo(type)->PrintFmt;
			ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 3, requestedWidth - valueWidth - gap));
			const auto trackId = ImGui::GetID("##Track");
			const bool textHidden = !ImGui::TempInputIsActive(trackId);
			const T previousValue = *value;
			T requestedValue = *value;
			bool entryCommitted = false;
			{
				if (textHidden) {
					for (auto color : { ImGuiCol_Text, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border })
						ImGui::PushStyleColor(color, ImVec4(0, 0, 0, 0));
				}
				const SKSE::stl::scope_exit restoreText([textHidden] { if (textHidden) ImGui::PopStyleColor(7); });
				ImGui::SliderScalar("##Track", type, &requestedValue, &minimum, &maximum, format, flags);
			}
			const auto geometry = GetSliderGeometry(minimum, maximum, format);
			*value = requestedValue;
			bool changed = *value != previousValue;
			if (changed)
				ImGui::MarkItemEdited(trackId);
			const auto trackItem = GImGui->LastItemData;
			if (!ImGui::TempInputIsActive(trackId)) {
				const auto trackMin = ImGui::GetItemRectMin();
				const auto trackMax = ImGui::GetItemRectMax();
				const float ratio = SliderPosition(type, *value, minimum, maximum, (flags & ImGuiSliderFlags_Logarithmic) != 0, geometry.epsilon, geometry.deadzone);
				const float centre = geometry.start + (geometry.finish - geometry.start) * ratio;
				const float y = (trackMin.y + trackMax.y) * .5f;
				const float radius = std::min(ImGui::GetStyle().GrabMinSize, std::max(1.0f, trackMax.x - trackMin.x - 4.0f)) * .5f;
				const float thickness = std::max(2.0f, ImGui::GetFontSize() * .3f);
				auto* draw = ImGui::GetWindowDrawList();
				draw->AddLine({ geometry.start, y }, { geometry.finish, y }, ImGui::GetColorU32(ImGuiCol_Text), thickness);
				draw->AddLine({ geometry.start, y }, { centre, y }, ImGui::GetColorU32(ImGuiCol_SliderGrab), thickness);
				auto handle = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
				handle.w = 1;
				draw->AddCircleFilled({ centre, y }, radius, ImGui::GetColorU32(handle));
				draw->AddCircle({ centre, y }, radius, ImGui::GetColorU32(ImGuiCol_Text), 0, std::max(1.0f, thickness * .35f));
			}

			ImGui::SameLine(0, gap);
			ImGui::DataTypeFormatString(formatted, IM_ARRAYSIZE(formatted), type, value, format);
			const auto color = globals::menu->GetTheme().StatusPalette.InfoColor;
			ImGui::InvisibleButton("##Number", { valueWidth, ImGui::GetFrameHeight() });
			const auto numberOrigin = ImGui::GetItemRectMin();
			ImGui::GetWindowDrawList()->PushClipRect(numberOrigin, { numberOrigin.x + valueWidth, numberOrigin.y + ImGui::GetFrameHeight() }, true);
			const auto* numberDigit = ImGui::GetFont()->GetFontBaked(numberFontSize)->FindGlyph('0');
			const float numberY = numberOrigin.y + ImGui::GetFrameHeight() * .5f - (numberDigit->Y0 + numberDigit->Y1) * .5f;
			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), numberFontSize, { numberOrigin.x, numberY }, ImGui::GetColorU32(color), formatted);
			ImGui::GetWindowDrawList()->PopClipRect();
			Util::AddTooltip(T("ui.slider.double_click_to_enter_a_value", "Double-click to enter a value."));
			const bool edit = !locked && ImGui::IsItemHovered() &&
			                  ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

			if (edit)
				ImGui::GetStateStorage()->SetBool(ImGui::GetID("##DirectEdit"), true);
			if (!controlLayout) {
				ImGui::SameLine(0, gap);
				ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
			}
			const bool directEdit = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##DirectEdit"));
			if (directEdit && !locked) {
				ImGui::SetNextItemWidth(requestedWidth);
				if (edit)
					ImGui::SetKeyboardFocusHere();
				T draft = *value;
				if (ImGui::InputScalar("##Value", type, &draft, nullptr, nullptr, inputFormat, ImGuiInputTextFlags_EnterReturnsTrue)) {
					if (std::isfinite(static_cast<double>(draft)) && draft >= minimum && draft <= maximum) {
						const T accepted = RoundEntry(draft, minimum, maximum, type, inputFormat, flags);
						entryCommitted = *value != accepted;
						changed |= entryCommitted;
						*value = accepted;
					}
					ImGui::GetStateStorage()->SetBool(ImGui::GetID("##DirectEdit"), false);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
					ImGui::GetStateStorage()->SetBool(ImGui::GetID("##DirectEdit"), false);
			}
			changed |= entryCommitted;
			ImGui::EndGroup();
			endGroup.release();
			const auto groupItem = GImGui->LastItemData;
			GImGui->LastItemData = trackItem;
			GImGui->LastItemData.Rect = groupItem.Rect;
			GImGui->LastItemData.StatusFlags |= groupItem.StatusFlags & ImGuiItemStatusFlags_HoveredRect;
			if (entryCommitted) {
				// Existing handlers consume the same completion event as a released slider drag.
				ImGui::MarkItemEdited(trackId);
				GImGui->LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasDeactivated | ImGuiItemStatusFlags_Deactivated;
				GImGui->DeactivatedItemData = { trackId, GImGui->FrameCount, true, true };
			}
			const std::string name(label, ImGui::FindRenderedTextEnd(label));
			if (!name.empty())
				Util::AddTooltip(std::format("Adjust {}. {}", name, "Double-click the value to enter a number.").c_str());
			return changed;
		}
	}

	ImVec2 VisibleTextEnd(const char* text)
	{
		auto* font = ImGui::GetFontBaked();
		const float scale = ImGui::GetFontSize() / font->Size;
		const char* end = ImGui::FindRenderedTextEnd(text);
		ImVec2 visible{};
		float advance = 0;
		while (text < end && *text != '\n') {
			unsigned int codepoint;
			const int count = ImTextCharFromUtf8(&codepoint, text, end);
			if (count == 0)
				break;
			text += count;
			const auto* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint));
			if (glyph->Visible) {
				visible.x = std::max(visible.x, advance + glyph->X1 * scale);
				visible.y = std::max(visible.y, glyph->Y1 * scale);
			}
			advance += glyph->AdvanceX * scale;
		}
		return visible;
	}

	ControlLayout::ControlLayout(bool fullWidth) : previous(controlLayout)
	{
		controlLayout = fullWidth ? 2 : 1;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, ImGui::GetFontSize() * controlPadding });
	}
	ControlLayout::~ControlLayout()
	{
		ImGui::PopStyleVar();
		controlLayout = previous;
	}

	float CheckboxSize()
	{
		constexpr float checkboxBodyScale = 185.0f / 144.0f;
		const float bodySize = ImGui::GetDefaultFont()->LegacySize * GImGui->FontSize / GImGui->FontSizeBase;
		return bodySize * checkboxBodyScale;
	}

	bool Checkbox(const char* label, bool* value)
	{
		auto& g = *GImGui;
		auto* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;
		std::optional<ControlStyle> controlStyle;
		if (controlLayout)
			controlStyle.emplace(true);
		const auto& style = ImGui::GetStyle();
		const auto id = window->GetID(label);
		const auto labelSize = ImGui::CalcTextSize(label, nullptr, true);
		const float square = CheckboxSize();
		const float height = std::max(square, labelSize.y);
		const auto origin = window->DC.CursorPos;
		const ImRect bounds(origin, { origin.x + square + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0), origin.y + height });
		ImGui::ItemSize(bounds, (height - labelSize.y) * .5f);
		if (!ImGui::ItemAdd(bounds, id))
			return false;
		bool hovered, held;
		const bool changed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
		if (changed) {
			*value = !*value;
			ImGui::MarkItemEdited(id);
		}
		ImGui::RenderNavCursor(bounds, id);
		// Label fonts can exceed the shared square without changing its hit area.
		const ImRect box({ origin.x, origin.y + (height - square) * .5f }, { origin.x + square, origin.y + (height + square) * .5f });
		const auto accent = style.Colors[ImGuiCol_CheckMark];
		const float luminance = accent.x * .2126f + accent.y * .7152f + accent.z * .0722f;
		const ImVec4 checkedTick = luminance > .5f ? ImVec4(.12f, .12f, .10f, 1) : ImVec4(1, 1, 1, 1);
		const ImU32 tick = ImGui::GetColorU32(*value ? checkedTick : accent);
		const ImU32 background = *value ? ImGui::GetColorU32(accent) : ImGui::GetColorU32(held && hovered ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered :
																																			   ImGuiCol_FrameBg);
		ImGui::RenderFrame(box.Min, box.Max, background, true, style.FrameRounding);
		const bool mixed = (g.LastItemData.ItemFlags & ImGuiItemFlags_MixedValue) != 0;
		if (mixed) {
			const float padding = std::max(1.0f, std::floor(square / 3.6f));
			window->DrawList->AddRectFilled({ box.Min.x + padding, box.Min.y + padding }, { box.Max.x - padding, box.Max.y - padding }, tick, style.FrameRounding);
		} else if (*value) {
			const float padding = std::max(1.0f, std::floor(square / 6));
			ImGui::RenderCheckMark(window->DrawList, { box.Min.x + padding, box.Min.y + padding }, tick, square - padding * 2);
		}
		if (g.LogEnabled)
			ImGui::LogRenderedText(&origin, mixed ? "[~]" : *value ? "[x]" :
																	 "[ ]");
		if (labelSize.x > 0)
			ImGui::RenderText({ box.Max.x + style.ItemInnerSpacing.x, origin.y + (height - labelSize.y) * .5f }, label);
		IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*value ? ImGuiItemStatusFlags_Checked : 0));
		const std::string name(label, ImGui::FindRenderedTextEnd(label));
		if (!name.empty())
			Util::AddTooltip(std::format("Turn {} on or off.", name).c_str());
		return changed;
	}

	namespace
	{
		template <class Draw>
		bool DrawEntry(const char* label, Draw draw)
		{
			ControlStyle style(controlLayout != 0);
			ControlRow row(label);
			ImGui::PushID(label);
			const SKSE::stl::scope_exit restore([] { ImGui::PopID(); });
			return draw(row.active ? "##Entry" : label);
		}
	}

	bool InputText(const char* label, char* text, size_t size, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputText(id, text, size, flags); });
	}

	bool InputText(const char* label, std::string* text, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputText(id, text, flags); });
	}

	bool InputTextWithHint(const char* label, const char* hint, char* text, size_t size, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputTextWithHint(id, hint, text, size, flags); });
	}

	bool InputTextWithHint(const char* label, const char* hint, std::string* text, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputTextWithHint(id, hint, text, flags); });
	}

	bool InputTextMultiline(const char* label, char* text, size_t size, const ImVec2& fieldSize, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputTextMultiline(id, text, size, fieldSize, flags); });
	}

	bool InputTextMultiline(const char* label, std::string* text, const ImVec2& fieldSize, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputTextMultiline(id, text, fieldSize, flags); });
	}

	bool InputDouble(const char* label, double* value, double step, double fastStep, const char* format, ImGuiInputTextFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::InputDouble(id, value, step, fastStep, format, flags); });
	}

	bool ColorEdit3(const char* label, float color[3], ImGuiColorEditFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::ColorEdit3(id, color, flags); });
	}

	bool ColorEdit4(const char* label, float color[4], ImGuiColorEditFlags flags)
	{
		return DrawEntry(label, [&](const char* id) { return ImGui::ColorEdit4(id, color, flags); });
	}

	bool CheckboxFlags(const char* label, unsigned int* flags, unsigned int mask)
	{
		bool enabled = (*flags & mask) == mask;
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, !enabled && (*flags & mask) != 0);
		const SKSE::stl::scope_exit restore([] { ImGui::PopItemFlag(); });
		if (!Checkbox(label, &enabled))
			return false;
		*flags = enabled ? *flags | mask : *flags & ~mask;
		return true;
	}

	struct ComboBox::State
	{
		ControlStyle style{ controlLayout != 0 };
		ControlRow row;
		std::string label;
		std::string preview;
		ImVec2 minimum;
		ImVec2 maximum;
		ImGuiLastItemData item;
		bool open;

		State(const char* a_label, const char* a_preview, ImGuiComboFlags flags, bool openImmediately) : row(a_label), label(a_label), preview(a_preview ? a_preview : "")
		{
			ImGui::PushID(a_label);
			minimum = ImGui::GetCursorScreenPos();
			maximum = { minimum.x + ImGui::CalcItemWidth(), minimum.y + ImGui::GetFrameHeight() };
			if (!(flags & ImGuiComboFlags_HeightMask_))
				flags |= ImGuiComboFlags_HeightLarge;
			const char* comboId = row.active ? "##Choice" : a_label;
			if (openImmediately && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
				ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID(comboId)));
			open = ImGui::BeginCombo(comboId, "", flags | ImGuiComboFlags_NoArrowButton);
			item = GImGui->LastItemData;
			if (open)
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, ImGui::GetFontSize() });
		}
		~State()
		{
			if (open) {
				ImGui::PopStyleVar();
				ImGui::EndCombo();
			}
			auto* draw = ImGui::GetWindowDrawList();
			const float font = ImGui::GetFontSize();
			const float middle = (minimum.y + maximum.y) * .5f;
			const auto color = ImGui::GetColorU32(ImGuiCol_Text);
			draw->PushClipRect(minimum, { maximum.x - font * 1.5f, maximum.y }, true);
			draw->AddText({ minimum.x + font * .6f, middle - font * .5f }, color, preview.c_str());
			draw->PopClipRect();
			const float arrow = maximum.x - font * .7f;
			draw->AddLine({ arrow - font * .2f, middle - font * .1f }, { arrow, middle + font * .1f }, color, font * .07f);
			draw->AddLine({ arrow, middle + font * .1f }, { arrow + font * .2f, middle - font * .1f }, color, font * .07f);
			GImGui->LastItemData = item;
			Util::AddTooltip(std::format("Choose {}.", label).c_str());
			ImGui::PopID();
		}
	};

	ComboBox::ComboBox(const char* label, const char* preview, ImGuiComboFlags flags, bool openImmediately)
	{
		if (!ImGui::GetCurrentWindow()->SkipItems)
			state = std::make_unique<State>(label, preview, flags, openImmediately);
	}
	ComboBox::~ComboBox() = default;
	ComboBox::operator bool() const { return state && state->open; }

	bool Combo(const char* label, int* selected, const char* const items[], int count)
	{
		if (count <= 0)
			return false;
		const char* preview = *selected >= 0 && *selected < count ? items[*selected] : "";
		bool changed = false;
		if (auto combo = ComboBox(label, preview)) {
			for (int index = 0; index < count; ++index) {
				ImGui::PushID(index);
				const SKSE::stl::scope_exit restoreItem([] { ImGui::PopID(); });
				if (ImGui::Selectable(items[index], *selected == index, 0, { 0, ImGui::GetFrameHeight() })) {
					changed = *selected != index;
					*selected = index;
				}
				if (*selected == index)
					ImGui::SetItemDefaultFocus();
				Util::AddTooltip(std::format("Select {} for {}.", items[index], label).c_str());
			}
		}
		return changed;
	}

	bool Combo(const char* label, int* selected, const char* items)
	{
		std::vector<const char*> choices;
		for (auto* item = items; item && *item; item += std::strlen(item) + 1)
			choices.push_back(item);
		return Combo(label, selected, choices.data(), static_cast<int>(choices.size()));
	}

	bool RadioButton(const char* label, bool active)
	{
		const ControlStyle style(controlLayout != 0);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, std::max(0.0f, (CheckboxSize() - ImGui::GetFontSize()) * .5f) });
		const SKSE::stl::scope_exit restore([] { ImGui::PopStyleVar(); });
		return ImGui::RadioButton(label, active);
	}
	bool RadioButton(const char* label, int* selected, int value)
	{
		if (!RadioButton(label, *selected == value))
			return false;
		*selected = value;
		return true;
	}

	bool SliderScalar(const char* label, ImGuiDataType type, void* value, const void* minimum, const void* maximum, const char* format, ImGuiSliderFlags flags)
	{
		if (!format)
			format = ImGui::DataTypeGetInfo(type)->PrintFmt;
#define CSX_SLIDER_TYPE(kind, cpp) \
	case kind:                     \
		return DrawSlider(label, type, static_cast<cpp*>(value), *static_cast<const cpp*>(minimum), *static_cast<const cpp*>(maximum), format, flags)
		switch (type) {
			CSX_SLIDER_TYPE(ImGuiDataType_Float, float);
			CSX_SLIDER_TYPE(ImGuiDataType_Double, double);
			CSX_SLIDER_TYPE(ImGuiDataType_S32, int);
			CSX_SLIDER_TYPE(ImGuiDataType_U32, unsigned int);
			CSX_SLIDER_TYPE(ImGuiDataType_S64, ImS64);
			CSX_SLIDER_TYPE(ImGuiDataType_U64, ImU64);
		default:
			return ImGui::SliderScalar(label, type, value, minimum, maximum, format, flags);
		}
#undef CSX_SLIDER_TYPE
	}
	bool SliderFloat(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags)
	{
		return SliderScalar(label, ImGuiDataType_Float, value, &minimum, &maximum, format, flags);
	}
	bool SliderFloatOnRelease(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags)
	{
		ImGui::PushID(label);
		const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
		auto* storage = ImGui::GetStateStorage();
		const auto valueId = ImGui::GetID("##PendingValue");
		const auto originalId = ImGui::GetID("##OriginalValue");
		const auto frameId = ImGui::GetID("##EditFrame");
		const auto mouseId = ImGui::GetID("##MouseEdit");
		const auto frame = ImGui::GetFrameCount();
		const bool editing = storage->GetInt(frameId, -2) == frame - 1 && storage->GetFloat(originalId) == *value;
		float pending = editing ? storage->GetFloat(valueId) : *value;
		const bool mouseEdit = editing && storage->GetBool(mouseId);
		SliderFloat(label, &pending, minimum, maximum, format, flags);
		const bool editable = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0 &&
		                      (flags & static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_ReadOnly)) == 0;
		const bool completed = editable && ImGui::IsItemDeactivatedAfterEdit() &&
		                       (!mouseEdit || (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsMousePosValid()));
		const bool changed = completed && std::isfinite(pending) && pending != *value;
		if (changed)
			*value = pending;
		storage->SetFloat(valueId, pending);
		storage->SetFloat(originalId, *value);
		storage->SetInt(frameId, ImGui::IsItemActive() ? frame : -2);
		storage->SetBool(mouseId, ImGui::IsItemActive() && GImGui->ActiveIdSource == ImGuiInputSource_Mouse &&
									  !ImGui::TempInputIsActive(GImGui->LastItemData.ID));
		return changed;
	}
	bool SliderInt(const char* label, int* value, int minimum, int maximum, const char* format, ImGuiSliderFlags flags)
	{
		return SliderScalar(label, ImGuiDataType_S32, value, &minimum, &maximum, format, flags);
	}
	template <size_t Count>
	bool DrawVector(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags)
	{
		ImGui::PushID(label);
		const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
		ImGui::BeginGroup();
		SKSE::stl::scope_exit endGroup([] { ImGui::EndGroup(); });
		ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
		constexpr std::array axes{ "X", "Y", "Z" };
		bool changed = false;
		for (size_t i = 0; i < Count; ++i) {
			changed |= SliderFloat(axes[i], value + i, minimum, maximum, format, flags);
			Util::AddTooltip(std::format("Adjust {} for {}.", axes[i], std::string_view(label, ImGui::FindRenderedTextEnd(label))).c_str());
		}
		ImGui::EndGroup();
		endGroup.release();
		return changed;
	}
	bool SliderFloat2(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags)
	{
		return DrawVector<2>(label, value, minimum, maximum, format, flags);
	}
	bool SliderFloat3(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags)
	{
		return DrawVector<3>(label, value, minimum, maximum, format, flags);
	}
	bool SliderAngle(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags)
	{
		constexpr float degreesPerRadian = 57.2957795131f;
		float degrees = *value * degreesPerRadian;
		if (!SliderFloat(label, &degrees, minimum, maximum, format, flags))
			return false;
		*value = degrees / degreesPerRadian;
		return true;
	}
}
