#pragma once

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace MenuUI
{
	/** Reserve enough coverage for section rules to remain visible in a scaled desktop view. */
	inline float SectionDividerThickness()
	{
		const float scale = ImGui::GetIO().DisplayFramebufferScale.y > 0 ? ImGui::GetIO().DisplayFramebufferScale.y : 1;
		return std::ceil(std::max(2.0f, ImGui::GetFontSize() / 12.0f) * scale) / scale;
	}

	/** Draw a filled, pixel-aligned rule using the current window's clipping rectangle. */
	inline void DrawSectionDivider(ImVec2 position, float width, ImU32 color)
	{
		const auto framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
		const float scaleX = framebufferScale.x > 0 ? framebufferScale.x : 1;
		const float scaleY = framebufferScale.y > 0 ? framebufferScale.y : 1;
		const auto origin = ImGui::GetWindowViewport()->Pos;
		const ImVec2 minimum{
			origin.x + std::ceil((position.x - origin.x) * scaleX) / scaleX,
			origin.y + std::floor((position.y - origin.y) * scaleY) / scaleY
		};
		const ImVec2 maximum{
			origin.x + std::floor((position.x + width - origin.x) * scaleX) / scaleX,
			minimum.y + SectionDividerThickness()
		};
		if (maximum.x > minimum.x)
			ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum, color);
	}
}
