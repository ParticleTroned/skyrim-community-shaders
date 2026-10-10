#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

class MenuHeaderRenderer
{
public:
	static void RenderSteamVRResizeHandles(float uiScale);
	static void RenderResizeGrip(float uiScale, bool customResizeControls = false);
	static void RenderResizeHandles(float uiScale, bool steamVRControls, bool customResizeControls = false);
};

namespace
{
#include "menu_resize_helpers_under_test.h"
}
#include "menu_resize_under_test.h"

namespace
{
	struct ResizeLayout
	{
		ImVec2 position{ 100, 100 };
		ImVec2 size{ 800, 600 };
		ImVec2 minimum{ 32, 32 };
		ImVec2 expectedShrink{ 420, 320 };
	};

	ImGuiWindow* DrawMenu(bool lockVRMenuToCanvas, bool useSteamVRWindowControls, bool openComposite, const ResizeLayout& layout)
	{
		const bool willBeDocked = false;
		const bool useOpenCompositeStableHeader = openComposite && !useSteamVRWindowControls;
#include "menu_resize_flags_under_test.h"
		ImGui::SetNextWindowPos(layout.position, lockVRMenuToCanvas ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(layout.size, lockVRMenuToCanvas ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
		ImGui::Begin("CommunityShaders", nullptr, windowFlags);
		auto* window = ImGui::GetCurrentWindow();
		ImGui::TextUnformatted("Settings header");
		if (ImGui::BeginChild("SettingsContent", ImVec2(0, -30))) {
			ImGui::TextUnformatted("Settings body");
		}
		ImGui::EndChild();
		const bool isDocked = ImGui::IsWindowDocked();
		const bool vrMenuLayoutUnlocked = !lockVRMenuToCanvas;
		const bool showSteamVRWindowControls = useSteamVRWindowControls && vrMenuLayoutUnlocked && !isDocked;
		const float uiScale = 1;
#include "menu_resize_dispatch_under_test.h"
		ImGui::End();
		return window;
	}

	bool CheckDrag(bool locked, bool steamVR, bool invalidPosition, int direction = 1, bool openComposite = true, const ResizeLayout& layout = {}, bool topLeft = false)
	{
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1920, 1620);
		io.Fonts->AddFontDefault();
		io.Fonts->Build();
		ImGui::GetStyle().WindowMinSize = layout.minimum;
		bool passed = true;
		ImVec2 click{};
		ImVec2 finalSize{};
		ImGuiID captured = 0;
		for (int frame = 0; frame < 16; ++frame) {
			ImVec2 mouse = frame < 3 ? ImVec2(600, 400) : click;
			if (frame >= 5 && frame <= 10) {
				const float deltaX = direction < 0 ? -150.0f : direction > 1 ? 500.0f :
				                                                               25.0f;
				const float deltaY = direction < 0 ? -100.0f : direction > 1 ? 400.0f :
				                                                               15.0f;
				mouse.x += float(frame - 4) * deltaX * (topLeft ? -1.0f : 1.0f);
				mouse.y += float(frame - 4) * deltaY * (topLeft ? -1.0f : 1.0f);
			}
			if (invalidPosition && frame == 7) {
				mouse = ImVec2(-FLT_MAX, -FLT_MAX);
			}
			io.AddMousePosEvent(mouse.x, mouse.y);
			if (frame == 4)
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			if (frame == 11)
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			ImGui::NewFrame();
			auto* window = DrawMenu(locked, steamVR, openComposite, layout);
			if (frame == 2) {
				const float size = steamVR ? GetSteamVRResizeHandleSize(1) * (topLeft ? 1.0f : 1.2f) : std::max(ImGui::GetFontSize() * 1.5f, 18.0f);
				const float inset = GetSteamVRResizeHandleInset(1);
				click = topLeft ? ImVec2(window->Pos.x + inset + size * 0.25f, window->Pos.y + inset + size * 0.25f) :
				                  ImVec2(window->Pos.x + window->Size.x - inset - size * 0.25f, window->Pos.y + window->Size.y - inset - size * 0.25f);
			}
			if (frame == 4) {
				captured = ImGui::GetActiveID();
				if (!locked && (steamVR || openComposite) && captured != window->GetID(steamVR ? (topLeft ? "##SteamVRResizeTopLeft" : "##SteamVRResizeBottomRight") : "##MenuResizeBottomRight")) {
					std::printf("FAIL: native resizing claimed the custom grip (SteamVR=%d)\n", steamVR);
					passed = false;
				}
			}
			if (frame >= 5 && frame <= 10 && !locked && (captured == 0 || ImGui::GetActiveID() != captured)) {
				std::printf("FAIL: drag ownership changed on frame %d (SteamVR=%d, miss=%d)\n", frame, steamVR, invalidPosition);
				passed = false;
			}
			if (frame > 2 && (window->Hidden || window->SkipItems)) {
				std::printf("FAIL: resized window disappeared on frame %d (locked=%d, SteamVR=%d)\n", frame, locked, steamVR);
				passed = false;
			}
			if (locked && (window->Size.x != layout.size.x || window->Size.y != layout.size.y || captured != 0)) {
				std::printf("FAIL: locked canvas accepted resize on frame %d (SteamVR=%d, width=%.1f, height=%.1f, active=%u)\n",
					frame, steamVR, window->Size.x, window->Size.y, captured);
				passed = false;
			}
			if (frame >= 4 && (openComposite || steamVR) &&
				(window->SizeFull.x < layout.minimum.x || window->SizeFull.y < layout.minimum.y ||
					window->Pos.x + window->SizeFull.x > io.DisplaySize.x || window->Pos.y + window->SizeFull.y > io.DisplaySize.y)) {
				std::printf("FAIL: headset resize violated theme/canvas bounds on frame %d (size=%.1f,%.1f)\n", frame, window->SizeFull.x, window->SizeFull.y);
				passed = false;
			}
			if (frame == 10)
				finalSize = window->SizeFull;
			if (frame > 11 && !locked &&
				((direction == 1 && (window->Size.x <= layout.size.x || window->Size.y <= layout.size.y)) ||
					(direction < 0 && (window->Size.x != layout.expectedShrink.x || window->Size.y != layout.expectedShrink.y)) ||
					(direction > 1 && (window->Size.x != (topLeft ? layout.position.x + layout.size.x : io.DisplaySize.x - layout.position.x) || window->Size.y != (topLeft ? layout.position.y + layout.size.y : io.DisplaySize.y - layout.position.y))) ||
					window->Size.x != finalSize.x || window->Size.y != finalSize.y)) {
				std::printf("FAIL: resized geometry did not persist on frame %d (SteamVR=%d)\n", frame, steamVR);
				passed = false;
			}
			ImGui::Render();
			if (frame > 2 && ImGui::GetDrawData()->TotalVtxCount == 0) {
				std::puts("FAIL: visible settings produced no draw vertices");
				passed = false;
			}
		}
		ImGui::DestroyContext();
		g_menuResizeDragState = {};
		return passed;
	}
}

int main()
{
	bool passed = true;
	for (bool steamVR : { false, true }) {
		passed = CheckDrag(true, steamVR, false) && passed;
		passed = CheckDrag(false, steamVR, false) && passed;
		passed = CheckDrag(false, steamVR, true) && passed;
		passed = CheckDrag(false, steamVR, false, -1) && passed;
		passed = CheckDrag(false, steamVR, true, -1) && passed;
		passed = CheckDrag(false, steamVR, false, 2) && passed;
	}
	passed = CheckDrag(false, false, false, 1, false) && passed;
	passed = CheckDrag(false, false, true, 1, false) && passed;
	for (bool steamVR : { false, true }) {
		passed = CheckDrag(false, steamVR, false, -1, true,
					 ResizeLayout{ { 100, 100 }, { 800, 600 }, { 650, 480 }, { 650, 480 } }) &&
		         passed;
		passed = CheckDrag(false, steamVR, false, -1, true,
					 ResizeLayout{ { 1750, 1470 }, { 150, 120 }, { 32, 32 }, { 170, 150 } }) &&
		         passed;
	}
	passed = CheckDrag(false, true, false, -1, false,
				 ResizeLayout{ { 100, 100 }, { 800, 600 }, { 650, 480 }, { 650, 480 } }, true) &&
	         passed;
	passed = CheckDrag(false, true, false, 2, false, {}, true) && passed;
	passed = CheckDrag(false, true, false, -1, false,
				 ResizeLayout{ { 1300, 1100 }, { 600, 500 }, { 32, 32 }, { 420, 320 } }, true) &&
	         passed;
	if (passed)
		std::puts("Locked canvas rejects resizing; OCU and SteamVR custom grips retain capture, remain visible, respect minimum/canvas bounds, and persist geometry.");
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
