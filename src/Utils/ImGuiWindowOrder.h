#pragma once

#include <imgui_internal.h>

namespace Util::ImGuiWindowOrder
{
	inline const ImGuiID kExplicitTopmostFrameId = ImHashStr("CSX::ExplicitTopmostFrame");

	/** Raise a floating window without letting passive status panels cover it this frame. */
	inline void BringCurrentWindowToDisplayFront()
	{
		auto* window = ImGui::GetCurrentWindow();
		window->RootWindowDockTree->StateStorage.SetInt(kExplicitTopmostFrameId, ImGui::GetFrameCount());
		ImGui::BringWindowToDisplayFront(window);
	}

	/** Preserve popup families, explicit floating layers, and ImGui's active window switcher. */
	[[nodiscard]] inline bool ShouldDrawAbovePassiveStatus(const ImGuiWindow* window)
	{
		if (!window)
			return false;
		const auto* context = ImGui::GetCurrentContext();
		for (auto* ancestor = window; ancestor; ancestor = ancestor->ParentWindowInBeginStack) {
			if ((ancestor->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) ||
				ancestor->RootWindowDockTree->StateStorage.GetInt(kExplicitTopmostFrameId, -1) == context->FrameCount)
				return true;
		}
		const auto* target = context->NavWindowingTarget;
		return target && (window->RootWindowDockTree == context->NavWindowingListWindow ||
							 (!(target->Flags & ImGuiWindowFlags_NoBringToFrontOnFocus) && window->RootWindowDockTree == target->RootWindowDockTree));
	}
}
