#include "Features/VR.h"
#include "Globals.h"
#include "Menu.h"
#include "NumericEntry.h"
#include "UI.h"
#include <array>
#include <cstring>
#include <format>
#include <imgui_internal.h>
#include <limits>
#include <string>
#include <unordered_map>

namespace Util::Widgets
{
	namespace
	{
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

			ImGui::PushID(label);
			const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
			ImGui::BeginGroup();
			SKSE::stl::scope_exit endGroup([] { ImGui::EndGroup(); });
			const bool locked = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 ||
			                    (flags & (static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_NoInput) | static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_ReadOnly))) != 0;
			const bool headset = globals::features::vr.IsMenuPointerInHeadset();
			const float requestedWidth = ImGui::CalcItemWidth();
			const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
			char formatted[128]{};
			ImGui::DataTypeFormatString(formatted, IM_ARRAYSIZE(formatted), type, value, format);
			const float valueWidth = std::min(requestedWidth * 0.45f, std::max(ImGui::GetFontSize() * 2, ImGui::CalcTextSize(formatted).x));
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
				const float radius = ImGui::GetFrameHeight() * .27f;
				const float thickness = std::max(2.0f, ImGui::GetFontSize() * .16f);
				auto* draw = ImGui::GetWindowDrawList();
				draw->AddLine({ start, y }, { finish, y }, ImGui::GetColorU32(ImGuiCol_TextDisabled), thickness);
				draw->AddLine({ start, y }, { centre, y }, ImGui::GetColorU32(ImGuiCol_SliderGrab), thickness);
				draw->AddCircleFilled({ centre, y }, radius, ImGui::GetColorU32(ImGuiCol_FrameBgActive));
				draw->AddCircle({ centre, y }, radius, ImGui::GetColorU32(ImGuiCol_Text), 0, std::max(1.0f, thickness * .5f));
			}

			ImGui::SameLine(0, gap);
			ImGui::DataTypeFormatString(formatted, IM_ARRAYSIZE(formatted), type, value, format);
			const auto color = globals::menu->GetTheme().StatusPalette.InfoColor;
			ImGui::InvisibleButton("##Number", { valueWidth, ImGui::GetFrameHeight() });
			const auto numberOrigin = ImGui::GetItemRectMin();
			ImGui::GetWindowDrawList()->PushClipRect(numberOrigin, { numberOrigin.x + valueWidth, numberOrigin.y + ImGui::GetFrameHeight() }, true);
			ImGui::GetWindowDrawList()->AddText({ numberOrigin.x, numberOrigin.y + ImGui::GetStyle().FramePadding.y }, ImGui::GetColorU32(color), formatted);
			ImGui::GetWindowDrawList()->PopClipRect();
			const bool edit = !locked && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

			if (edit && headset)
				ImGui::OpenPopup("Enter value");
			if (edit && !headset)
				ImGui::GetStateStorage()->SetBool(ImGui::GetID("##DirectEdit"), true);
			ImGui::SameLine(0, gap);
			ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
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
			// Drafts belong to the widget ID; no pointer to a feature setting outlives this call.
			static std::unordered_map<ImGuiID, std::string> drafts;
			const auto draftId = ImGui::GetID("##Draft");
			if (ImGui::IsPopupOpen("Enter value") && !drafts.contains(draftId)) {
				if constexpr (std::is_floating_point_v<T>)
					drafts[draftId] = std::format("{}", *value);
				else
					drafts[draftId] = std::to_string(*value);
			}
			if (auto popup = Util::CenteredPopupModal("Enter value")) {
				auto& draft = drafts[draftId];
				ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
				ImGui::TextUnformatted(draft.empty() ? " " : draft.c_str());
				ImGui::Text("Range: %s to %s", std::to_string(minimum).c_str(), std::to_string(maximum).c_str());
				const auto parsed = MenuUI::ParseNumber<T>(draft, minimum, maximum);
				const float key = ImGui::GetFrameHeight() * 1.6f;
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
				if (ImGui::Button("Backspace") && !draft.empty())
					draft.pop_back();
				Util::AddTooltip("Remove the last digit.");
				ImGui::SameLine();
				if (ImGui::Button("Clear"))
					draft.clear();
				Util::AddTooltip("Clear this entry. Your setting is unchanged.");
				if (!parsed)
					ImGui::TextWrapped("Enter a number within the slider's range.");
				{
					auto disabled = Util::DisableGuard(locked || !parsed);
					if (ImGui::Button("Apply")) {
						const T accepted = RoundEntry(*parsed, minimum, maximum, type, inputFormat, flags);
						entryCommitted = *value != accepted;
						changed |= entryCommitted;
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
				if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
					ImGui::CloseCurrentPopup();
				Util::AddTooltip("Keep the previous value.");
			}
			if (!ImGui::IsPopupOpen("Enter value"))
				drafts.erase(draftId);
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

	bool Checkbox(const char* label, bool* value)
	{
		const bool changed = ImGui::Checkbox(label, value);
		const std::string name(label, ImGui::FindRenderedTextEnd(label));
		if (!name.empty())
			Util::AddTooltip(std::format("Turn {} on or off.", name).c_str());
		return changed;
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
