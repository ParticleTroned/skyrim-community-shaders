#pragma once

#include <imgui.h>

namespace MenuUI
{
	/** Shared compact styling for pointer-operated number and text keyboards. */
	class InputPadStyle
	{
	public:
		static constexpr float Scale = .5f;
		static constexpr float FontScale = 1.25f * Scale;
		explicit InputPadStyle(float fit = 1.0f)
		{
			const float scale = Scale * fit;
			const auto& style = ImGui::GetStyle();
			ImGui::PushFont(ImGui::GetFont(), style.FontSizeBase * FontScale * fit);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { style.WindowPadding.x * scale, style.WindowPadding.y * scale });
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { style.FramePadding.x * scale, style.FramePadding.y * scale });
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { style.ItemSpacing.x * scale, style.ItemSpacing.y * scale });
			ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, { style.ItemInnerSpacing.x * scale, style.ItemInnerSpacing.y * scale });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, { style.WindowMinSize.x * scale, style.WindowMinSize.y * scale });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.WindowRounding * scale);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.FrameRounding * scale);
		}
		~InputPadStyle()
		{
			ImGui::PopStyleVar(7);
			ImGui::PopFont();
		}
		InputPadStyle(const InputPadStyle&) = delete;
		InputPadStyle& operator=(const InputPadStyle&) = delete;
	};
}
