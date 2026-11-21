#pragma once

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <optional>

namespace CSXEditorLayout
{
	// Keep the saved ImGui window identity independent of its visible title.
	inline constexpr char BrowserTitle[] = "CSX Editor Browser###CS Editor Browser";

	struct Panel
	{
		ImVec2 position;
		ImVec2 size;
	};

	struct Panels
	{
		Panel browser, viewport, palette;
	};

	/** Aligned first-use panels within the editor's usable canvas, excluding its menu bar. */
	inline std::optional<Panels> Defaults(ImVec2 origin, ImVec2 size, float gap, float browserLimit,
		float imageAspect, ImVec2 viewportChrome, bool showViewport, bool showPalette)
	{
		if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(size.x) || !std::isfinite(size.y) ||
			!std::isfinite(gap) || !std::isfinite(browserLimit) || !std::isfinite(imageAspect) ||
			!std::isfinite(viewportChrome.x) || !std::isfinite(viewportChrome.y) ||
			size.x < 8 || size.y < 8 || gap < 0 || browserLimit <= 0 || imageAspect <= 0 || viewportChrome.x < 0 || viewportChrome.y < 0)
			return std::nullopt;

		const ImVec2 maximum{ std::floor(origin.x + size.x), std::floor(origin.y + size.y) };
		origin = { std::ceil(origin.x), std::ceil(origin.y) };
		size = { maximum.x - origin.x, maximum.y - origin.y };
		if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 8 || size.y < 8)
			return std::nullopt;
		gap = std::floor(std::min(gap, std::min(size.x, size.y) * .25f));
		const float width = size.x - gap * 3;
		const float height = size.y - gap * 2;
		const float browserWidth = std::floor(std::clamp(std::min(width * .5f, browserLimit), 1.0f, width - 1));
		const ImVec2 topLeft{ origin.x + gap, origin.y + gap };
		const Panel right{ { topLeft.x + browserWidth + gap, topLeft.y }, { width - browserWidth, height } };
		Panels result{ { topLeft, { browserWidth, height } }, right, right };
		if (showViewport && showPalette) {
			const float splitHeight = height - gap;
			const float minimumPanelHeight = std::min(splitHeight * .5f, std::max(1.0f, viewportChrome.y + gap * 2));
			const float imageHeight = std::max(1.0f, right.size.x - viewportChrome.x) / imageAspect;
			result.viewport.size.y = std::floor(std::clamp(imageHeight + viewportChrome.y, minimumPanelHeight, splitHeight - minimumPanelHeight));
			result.palette.position.y = right.position.y + result.viewport.size.y + gap;
			result.palette.size.y = splitHeight - result.viewport.size.y;
		}
		return result;
	}

	/** Apply initial or explicitly reset geometry without changing a saved arrangement. */
	inline void ApplyDefaults(const Panel& panel, ImGuiCond condition)
	{
		ImGui::SetNextWindowSize(panel.size, condition);
		ImGui::SetNextWindowPos(panel.position, condition);
	}
}
