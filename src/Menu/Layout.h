#pragma once

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace MenuUI::Layout
{
	inline constexpr float MinimumWidthInFonts = 36.0f;
	inline constexpr float MinimumHeightInFonts = 22.0f;
	inline constexpr float CanvasPadding = 8.0f;
	inline constexpr float DefaultSidebarRatio = 0.24f;
	inline constexpr float MinimumSidebarWidthInFonts = 8.0f;
	inline constexpr float MinimumContentWidthInFonts = 24.0f;
	inline constexpr float MaximumSidebarRatio = 0.4f;

	/** Bound saved zoom before exponentiation, including malformed theme values. */
	inline float SanitizeGlobalScale(float exponent)
	{
		return std::isfinite(exponent) ? std::clamp(exponent, -1.0f, 1.0f) : 0.0f;
	}

	/** Keep typography usable within the active desktop or headset canvas. */
	inline float EffectiveScale(float exponent, float baseFontSize, ImVec2 canvas, float dpiScale = 1.0f)
	{
		const float requested = std::exp2(SanitizeGlobalScale(exponent));
		baseFontSize *= std::isfinite(dpiScale) && dpiScale > 0 ? dpiScale : 1.0f;
		if (!std::isfinite(baseFontSize) || baseFontSize <= 0 ||
			!std::isfinite(canvas.x) || !std::isfinite(canvas.y) ||
			canvas.x <= CanvasPadding * 2 || canvas.y <= CanvasPadding * 2)
			return requested;
		const float fittingFont = std::min((canvas.x - CanvasPadding * 2) / MinimumWidthInFonts,
			(canvas.y - CanvasPadding * 2) / MinimumHeightInFonts);
		return std::min(requested, fittingFont / baseFontSize);
	}

	/** Minimum settings geometry scales with text and never exceeds available space. */
	inline ImVec2 MinimumSize(float fontSize, ImVec2 available, ImVec2 themeMinimum = {})
	{
		return { std::min(std::max(fontSize * MinimumWidthInFonts, themeMinimum.x), available.x),
			std::min(std::max(fontSize * MinimumHeightInFonts, themeMinimum.y), available.y) };
	}

	/** Repair saved geometry without discarding usable custom placement. */
	inline bool FitWindow(ImVec2& center, ImVec2& size, ImVec2 origin, ImVec2 available,
		ImVec2 fallbackCenter, ImVec2 fallbackSize, ImVec2 minimumSize)
	{
		auto fitAxis = [](float center, float size, float origin, float span,
						   float fallbackCenter, float fallbackSize, float minimum) {
			size = std::isfinite(size) && size > 0 ? size : fallbackSize;
			center = std::isfinite(center) ? center : fallbackCenter;
			size = std::clamp(size, std::min(minimum, span), span);
			const float offset = std::clamp(center - origin - size * .5f, 0.0f, span - size);
			return ImVec2(origin + offset + size * .5f, size);
		};
		const auto x = fitAxis(center.x, size.x, origin.x, available.x, fallbackCenter.x, fallbackSize.x, minimumSize.x);
		const auto y = fitAxis(center.y, size.y, origin.y, available.y, fallbackCenter.y, fallbackSize.y, minimumSize.y);
		const bool changed = center.x != x.x || center.y != y.x || size.x != x.y || size.y != y.y;
		center = { x.x, y.x };
		size = { x.y, y.y };
		return changed;
	}

	/** Apply before the first table row so persisted and dragged widths stay usable. */
	inline void ConstrainSidebar(bool reset)
	{
		auto* table = ImGui::GetCurrentTable();
		if (!table || table->ColumnsCount != 2)
			return;
		const float width = std::max(1.0f, table->OuterRect.GetWidth() - ImGui::GetStyle().CellPadding.x * 4);
		const float font = ImGui::GetFontSize();
		const float maximum = std::clamp(1.0f - font * MinimumContentWidthInFonts / width, DefaultSidebarRatio, MaximumSidebarRatio);
		const float minimum = std::min(font * MinimumSidebarWidthInFonts / width, maximum);
		const float totalWeight = table->Columns[0].StretchWeight + table->Columns[1].StretchWeight;
		const bool validWeights = std::isfinite(totalWeight) &&
		                          table->Columns[0].StretchWeight > 0 && table->Columns[1].StretchWeight > 0;
		const float saved = validWeights ? table->Columns[0].StretchWeight / totalWeight : DefaultSidebarRatio;
		const float requested = reset ? DefaultSidebarRatio : saved;
		const float ratio = std::clamp(requested, minimum, maximum);
		if (reset || !validWeights || std::abs(saved - ratio) > 0.0001f) {
			table->Columns[0].StretchWeight = ratio;
			table->Columns[1].StretchWeight = 1.0f - ratio;
			table->IsSettingsDirty = true;
		}
	}
}
