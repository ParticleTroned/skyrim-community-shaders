#include "Features/VR.h"
#include "Globals.h"
#include "Menu.h"
#include "NumericEntry.h"
#include "UI.h"
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
		constexpr float controlPadding = .55f;
		constexpr float numberTargetWidth = 4.5f;
		constexpr float handleRadius = .6f;

		struct ControlStyle
		{
			explicit ControlStyle(bool scaleText)
			{
				ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * (scaleText ? controlTextScale : 1.0f));
				const float font = ImGui::GetFontSize();
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, font * controlPadding });
				ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, font * handleRadius * 2);
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
		bool DrawNumberPad(const char* label, ImGuiDataType type, T* value, T minimum, T maximum, const char* inputFormat, ImGuiSliderFlags flags, bool locked, bool showRange = true)
		{
			bool entryCommitted = false;
			// Drafts belong to the widget ID; no pointer to a feature setting outlives this call.
			static std::unordered_map<ImGuiID, std::string> drafts;
			const auto draftId = ImGui::GetID("##Draft");
			if (!ImGui::IsPopupOpen("Enter value")) {
				drafts.erase(draftId);
				return false;
			}
			if (!drafts.contains(draftId)) {
				if constexpr (std::is_floating_point_v<T>)
					drafts[draftId] = std::format("{}", *value);
				else
					drafts[draftId] = std::to_string(*value);
			}
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * 1.25f);
			const SKSE::stl::scope_exit restoreKeypadFont([] { ImGui::PopFont(); });
			const auto viewportSize = ImGui::GetWindowViewport()->WorkSize;
			ImGui::SetNextWindowSizeConstraints({ 0, 0 }, { std::max(1.0f, viewportSize.x - ImGui::GetFontSize()), std::max(1.0f, viewportSize.y - ImGui::GetFontSize()) });
			if (auto popup = Util::CenteredPopupModal("Enter value")) {
				auto& draft = drafts[draftId];
				const float key = ImGui::GetFontSize() * 2.8f;
				const float keypadWidth = key * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + keypadWidth);
				ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
				ImGui::PopTextWrapPos();
				const auto readout = ImGui::GetCursorScreenPos();
				const float readoutHeight = ImGui::GetTextLineHeight() * 2.4f;
				auto* draw = ImGui::GetWindowDrawList();
				draw->AddRectFilled(readout, { readout.x + keypadWidth, readout.y + readoutHeight }, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
				draw->PushClipRect(readout, { readout.x + keypadWidth, readout.y + readoutHeight }, true);
				const float textInset = ImGui::GetFontSize() * .3f;
				draw->AddText(nullptr, ImGui::GetFontSize(), { readout.x + textInset, readout.y + ImGui::GetFontSize() * .2f }, ImGui::GetColorU32(ImGuiCol_Text), draft.empty() ? "Enter a value" : draft.c_str(), nullptr, keypadWidth - textInset * 2);
				draw->PopClipRect();
				ImGui::Dummy({ keypadWidth, readoutHeight });
				char lower[128]{}, upper[128]{};
				ImGui::DataTypeFormatString(lower, IM_ARRAYSIZE(lower), type, &minimum, inputFormat);
				ImGui::DataTypeFormatString(upper, IM_ARRAYSIZE(upper), type, &maximum, inputFormat);
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + keypadWidth);
				if (showRange)
					ImGui::TextWrapped("Range: %s to %s", lower, upper);
				ImGui::PopTextWrapPos();
				const auto parsed = MenuUI::ParseNumber<T>(draft, minimum, maximum);
				const ImVec2 actionSize{ (keypadWidth - ImGui::GetStyle().ItemSpacing.x) * .5f, key * .85f };
				constexpr std::array keys{ "7", "8", "9", "4", "5", "6", "1", "2", "3", "-", "0", "." };
				for (size_t i = 0; i < keys.size(); ++i) {
					if (i % 3)
						ImGui::SameLine();
					const bool sign = i == 9;
					const bool decimal = i == 11;
					auto disabled = Util::DisableGuard(locked || (sign && minimum >= 0) || (decimal && !std::is_floating_point_v<T>));
					if (ImGui::Button(keys[i], { key, key })) {
						if (sign) {
							if (draft.starts_with('-'))
								draft.erase(0, 1);
							else
								draft.insert(0, "-");
						} else if (draft.size() < 40 && (!decimal || draft.find('.') == std::string::npos))
							draft += keys[i];
					}
					Util::AddTooltip(sign ? "Change the sign." : decimal ? "Add a decimal point." :
																		   "Add this digit.");
				}
				if (ImGui::Button("Backspace", actionSize) && !draft.empty())
					draft.pop_back();
				Util::AddTooltip("Remove the last digit.");
				ImGui::SameLine();
				if (ImGui::Button("Clear", actionSize))
					draft.clear();
				Util::AddTooltip("Clear this entry. Your setting is unchanged.");
				// Reserve the help row so Apply and Cancel never move while the draft changes.
				const auto help = ImGui::GetCursorScreenPos();
				if (!parsed)
					ImGui::GetWindowDrawList()->AddText(nullptr, ImGui::GetFontSize(), help, ImGui::GetColorU32(ImGuiCol_Text), "Enter a number within the slider's range.", nullptr, keypadWidth);
				ImGui::Dummy({ keypadWidth, ImGui::GetTextLineHeight() * 2 });
				{
					auto disabled = Util::DisableGuard(locked || !parsed);
					if (ImGui::Button("Apply", actionSize)) {
						const T accepted = RoundEntry(*parsed, minimum, maximum, type, inputFormat, flags);
						entryCommitted = *value != accepted;
						*value = accepted;
						ImGui::CloseCurrentPopup();
					}
					Util::AddTooltip("Use this value.");
				}
				ImGui::SameLine();
				// A setting can become unavailable while its modal draft is open.
				const bool cancelDisabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
				if (cancelDisabled)
					ImGui::BeginDisabledOverrideReenable();
				const SKSE::stl::scope_exit restoreDisabled([cancelDisabled] {
					if (cancelDisabled)
						ImGui::EndDisabledOverrideReenable();
				});
				if (ImGui::Button("Cancel", actionSize) || ImGui::IsKeyPressed(ImGuiKey_Escape))
					ImGui::CloseCurrentPopup();
				Util::AddTooltip("Keep the previous value.");
			}
			if (!ImGui::IsPopupOpen("Enter value"))
				drafts.erase(draftId);
			return entryCommitted;
		}

		template <class T>
		bool DrawSlider(const char* label, ImGuiDataType type, T* value, T minimum, T maximum, const char* format, ImGuiSliderFlags flags)
		{
			if (ImGui::GetCurrentWindow()->SkipItems)
				return false;
			if (!std::isfinite(static_cast<double>(*value)) || !std::isfinite(static_cast<double>(minimum)) ||
				!std::isfinite(static_cast<double>(maximum)) || minimum > maximum) {
				ImGui::TextDisabled("%s: unavailable", label);
				Util::AddTooltip("This value is unavailable. Load saved settings or restore defaults.");
				return false;
			}

			const ControlStyle controlStyle(controlLayout != 0);
			const ControlRow row(label);
			ImGui::PushID(label);
			const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
			ImGui::BeginGroup();
			SKSE::stl::scope_exit endGroup([] { ImGui::EndGroup(); });
			const bool locked = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 ||
			                    (flags & (static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_NoInput) | static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_ReadOnly))) != 0;
			const bool headset = globals::features::vr.IsMenuPointerInHeadset();
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
			bool changed;
			bool entryCommitted = false;
			{
				if (textHidden) {
					for (auto color : { ImGuiCol_Text, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border })
						ImGui::PushStyleColor(color, ImVec4(0, 0, 0, 0));
				}
				const SKSE::stl::scope_exit restoreText([textHidden] { if (textHidden) ImGui::PopStyleColor(7); });
				changed = ImGui::SliderScalar("##Track", type, value, &minimum, &maximum, format, flags);
			}
			const auto trackItem = GImGui->LastItemData;
			if (!ImGui::TempInputIsActive(trackId)) {
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
				const float ratio = SliderPosition(type, *value, minimum, maximum, (flags & ImGuiSliderFlags_Logarithmic) != 0, epsilon, deadzone);
				const float centre = start + (finish - start) * ratio;
				const float y = (trackMin.y + trackMax.y) * .5f;
				const float radius = ImGui::GetFontSize() * handleRadius;
				const float thickness = std::max(2.0f, ImGui::GetFontSize() * .3f);
				auto* draw = ImGui::GetWindowDrawList();
				draw->AddLine({ start, y }, { finish, y }, ImGui::GetColorU32(ImGuiCol_Text), thickness);
				draw->AddLine({ start, y }, { centre, y }, ImGui::GetColorU32(ImGuiCol_SliderGrab), thickness);
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
			Util::AddTooltip("Double-click to enter a value. In the headset, this opens the number pad.");
			const bool edit = !locked && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

			if (edit && headset)
				ImGui::OpenPopup("Enter value");
			if (edit && !headset)
				ImGui::GetStateStorage()->SetBool(ImGui::GetID("##DirectEdit"), true);
			if (!controlLayout) {
				ImGui::SameLine(0, gap);
				ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
			}
			const bool directEdit = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##DirectEdit"));
			if (directEdit && !locked && !headset) {
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
			entryCommitted |= DrawNumberPad(label, type, value, minimum, maximum, inputFormat, flags, locked);
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
				Util::AddTooltip(std::format("Adjust {}. Double-click the value to enter a number.", name).c_str());
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
		constexpr float headerFrameRatio = 37.0f / 27.0f;
		constexpr float requestedScale = 1.25f;
		const float bodySize = ImGui::GetDefaultFont()->LegacySize * GImGui->FontSize / GImGui->FontSizeBase;
		return bodySize * headerFrameRatio * requestedScale;
	}

	bool Checkbox(const char* label, bool* value)
	{
		// The square stays the same size when detail labels use a larger font.
		std::optional<ControlStyle> style;
		if (controlLayout)
			style.emplace(true);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { ImGui::GetStyle().FramePadding.x, std::max(0.0f, (CheckboxSize() - ImGui::GetFontSize()) * .5f) });
		const SKSE::stl::scope_exit restorePadding([] { ImGui::PopStyleVar(); });
		const bool checked = *value;
		if (checked) {
			const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
			const float luminance = accent.x * .2126f + accent.y * .7152f + accent.z * .0722f;
			const ImVec4 tick = luminance > .5f ? ImVec4(.12f, .12f, .10f, 1) : ImVec4(1, 1, 1, 1);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, accent);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, accent);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, accent);
			ImGui::PushStyleColor(ImGuiCol_CheckMark, tick);
		}
		const SKSE::stl::scope_exit restore([checked] { if (checked) ImGui::PopStyleColor(4); });
		const bool changed = ImGui::Checkbox(label, value);
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
		return DrawEntry(label, [&](const char* id) {
			if (!globals::features::vr.IsMenuPointerInHeadset())
				return ImGui::InputDouble(id, value, step, fastStep, format, flags);
			const bool locked = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 || (flags & ImGuiInputTextFlags_ReadOnly) != 0;
			char formatted[128]{};
			ImGui::DataTypeFormatString(formatted, IM_ARRAYSIZE(formatted), ImGuiDataType_Double, value, format);
			if (ImGui::Button(std::format("{}###Entry", formatted).c_str(), { ImGui::CalcItemWidth(), ImGui::GetFrameHeight() }) && !locked)
				ImGui::OpenPopup("Enter value");
			Util::AddTooltip("Enter this number using the headset number pad.");
			return DrawNumberPad(label, ImGuiDataType_Double, value, std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(), format,
				ImGuiSliderFlags_NoRoundToFormat, locked, false);
		});
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
