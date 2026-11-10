#include "Menu/Layout.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace globals
{
	namespace game
	{
		bool isVR = true;
	}
	namespace features
	{
		struct VRPointer
		{
			bool inHeadset = false;
			bool IsMenuPointerInHeadset() const { return inHeadset; }
		} vr;
	}
}

class MenuHeaderRenderer
{
public:
	static void RenderSteamVRResizeHandles(float uiScale, ImVec2 minimumSize);
	static void RenderResizeGrip(float uiScale, ImVec2 minimumSize, bool customResizeControls = false);
	static void RenderResizeHandles(float uiScale, ImVec2 minimumSize, bool steamVRControls, bool customResizeControls = false);
};

namespace
{
	struct POINT
	{
		long x, y;
	};
	struct RECT
	{
		long left, top, right, bottom;
	};
	using HWND = void*;
	RECT clientRect{};
	POINT screenCursor{};
	bool clientAvailable = true, cursorAvailable = true, conversionAvailable = true;
	bool GetClientRect(HWND, RECT* rect)
	{
		*rect = clientRect;
		return clientAvailable;
	}
	bool GetCursorPos(POINT* point)
	{
		*point = screenCursor;
		return cursorAvailable;
	}
	bool ScreenToClient(HWND, POINT*) { return conversionAvailable; }
#include "menu_mirror_under_test.h"

	bool CheckDesktopMirrorMapping()
	{
		bool passed = true;
		for (const ImVec2 canvas : { ImVec2(1920, 1620), ImVec2(1920, 1080) }) {
			for (const ImVec2 buffer : { ImVec2(1920, 1080), ImVec2(1440, 1600), ImVec2(3840, 2160) }) {
				for (const ImVec2 client : { buffer, ImVec2(960, 540), ImVec2(2560, 1440), ImVec2(1000, 1500) }) {
					clientRect = { 0, 0, static_cast<long>(client.x), static_cast<long>(client.y) };
					const float scale = std::min(buffer.x / canvas.x, buffer.y / canvas.y);
					const ImVec2 offset((buffer.x - canvas.x * scale) * .25f, (buffer.y - canvas.y * scale) * .25f);
					for (const ImVec2 uv : { ImVec2(.1f, .1f), ImVec2(.5f, .5f), ImVec2(.9f, .9f) }) {
						const ImVec2 expected(uv.x * canvas.x, uv.y * canvas.y);
						screenCursor = {
							std::lround((expected.x * scale + offset.x) * client.x / buffer.x),
							std::lround((expected.y * scale + offset.y) * client.y / buffer.y)
						};
						const auto actual = GetDesktopMirrorCursor(nullptr, canvas, buffer);
						passed = std::abs(actual.x - expected.x) <= buffer.x / client.x / scale &&
						         std::abs(actual.y - expected.y) <= buffer.y / client.y / scale && passed;
					}
				}
			}
		}
		const auto invalid = [&](ImVec2 buffer = ImVec2(1920, 1080)) {
			const auto cursor = GetDesktopMirrorCursor(nullptr, { 1920, 1620 }, buffer);
			return cursor.x == -FLT_MAX && cursor.y == -FLT_MAX;
		};
		clientRect = { 0, 0, 960, 540 };
		screenCursor = { -1, -1 };
		passed = invalid() && passed;
		screenCursor = { 959, 539 };
		passed = invalid() && passed;
		screenCursor = { 300, 300 };
		passed = invalid({ 0, 0 }) && invalid({ INFINITY, 1080 }) && passed;
		const float tiny = std::numeric_limits<float>::denorm_min();
		const auto overflow = GetDesktopMirrorCursor(nullptr, { tiny, tiny }, { FLT_MAX, FLT_MAX });
		passed = overflow.x == -FLT_MAX && overflow.y == -FLT_MAX && passed;
		clientRect = {};
		passed = invalid() && passed;
		clientRect = { 0, 0, 960, 540 };
		for (bool* available : { &clientAvailable, &cursorAvailable, &conversionAvailable }) {
			*available = false;
			passed = invalid() && passed;
			*available = true;
		}
		if (!passed)
			std::puts("FAIL: desktop mirror cursor does not match the resized client or reject invalid input");
		return passed;
	}

#include "menu_resize_helpers_under_test.h"
	struct SettingsWindowLayout
	{
		ImVec2 center;
		ImVec2 size;
		bool constrainedByTopStatusWindow = false;
	};
#include "menu_canvas_under_test.h"
}
#include "menu_resize_under_test.h"

namespace
{
	struct ResizeLayout
	{
		ImVec2 position{ 100, 100 };
		ImVec2 size{ 800, 600 };
		ImVec2 minimum{ 420, 320 };
		ImVec2 expectedShrink{ 420, 320 };
	};

	ImGuiWindow* DrawMenu(bool lockVRMenuToCanvas, bool useSteamVRWindowControls, bool openComposite, const ResizeLayout& layout)
	{
		const bool willBeDocked = false;
		const bool headsetPointer = globals::game::isVR && globals::features::vr.IsMenuPointerInHeadset();
		const bool useOpenCompositeStableHeader = openComposite && !useSteamVRWindowControls;
#include "menu_resize_flags_under_test.h"
		ImGui::SetNextWindowPos(layout.position, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(layout.size, ImGuiCond_FirstUseEver);
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
		const auto minimumWindowSize = layout.minimum;
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
					window->Pos.x < MenuUI::Layout::CanvasPadding || window->Pos.y < MenuUI::Layout::CanvasPadding ||
					window->Pos.x + window->SizeFull.x > io.DisplaySize.x - MenuUI::Layout::CanvasPadding ||
					window->Pos.y + window->SizeFull.y > io.DisplaySize.y - MenuUI::Layout::CanvasPadding)) {
				std::printf("FAIL: headset resize violated theme/canvas bounds on frame %d (size=%.1f,%.1f)\n", frame, window->SizeFull.x, window->SizeFull.y);
				passed = false;
			}
			if (frame == 10)
				finalSize = window->SizeFull;
			if (frame == 6 && !locked && !invalidPosition && direction == 1) {
				const ImVec2 expected(layout.size.x + 50, layout.size.y + 30);
				if (window->SizeFull.x != expected.x || window->SizeFull.y != expected.y) {
					std::printf("FAIL: resize does not track desktop pointer 1:1 (SteamVR=%d, size=%.1f,%.1f)\n", steamVR, window->SizeFull.x, window->SizeFull.y);
					passed = false;
				}
			}
			if (frame > 11 && !locked &&
				((direction == 1 && (window->Size.x <= layout.size.x || window->Size.y <= layout.size.y)) ||
					(direction < 0 && (window->Size.x != layout.expectedShrink.x || window->Size.y != layout.expectedShrink.y)) ||
					(direction > 1 && (window->Size.x != (topLeft ? layout.position.x + layout.size.x : io.DisplaySize.x - layout.position.x) - MenuUI::Layout::CanvasPadding ||
										  window->Size.y != (topLeft ? layout.position.y + layout.size.y : io.DisplaySize.y - layout.position.y) - MenuUI::Layout::CanvasPadding)) ||
					window->Size.x != finalSize.x || window->Size.y != finalSize.y)) {
				std::printf("FAIL: resized geometry did not persist on frame %d (SteamVR=%d)\n", frame, steamVR);
				passed = false;
			}
			if (frame >= 10 && !locked && direction > 1) {
				ImVec2 center{ window->Pos.x + window->SizeFull.x * .5f, window->Pos.y + window->SizeFull.y * .5f };
				auto size = window->SizeFull;
				if (ClampSettingsWindowToCanvas(center, size, { center, size })) {
					std::puts("FAIL: edge drag produced geometry the next frame would repair");
					passed = false;
				}
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

bool CheckCanvasRecovery()
{
	ImGui::CreateContext();
	auto* viewport = ImGui::GetMainViewport();
	viewport->WorkPos = { 40, 60 };
	viewport->WorkSize = { 1920, 1080 };
	const SettingsWindowLayout fallback{ { 1000, 600 }, { 900, 700 } };
	bool passed = true;
	for (const auto originalCenter : { ImVec2(1000, 600), ImVec2(-8000, 9000), ImVec2(std::numeric_limits<float>::quiet_NaN(), INFINITY) }) {
		for (const auto originalSize : { ImVec2(900, 700), ImVec2(5000, 5000), ImVec2(0, -1), ImVec2(INFINITY, std::numeric_limits<float>::quiet_NaN()) }) {
			auto center = originalCenter;
			auto size = originalSize;
			ClampSettingsWindowToCanvas(center, size, fallback);
			const bool bounded = std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(size.x) && std::isfinite(size.y) &&
			                     size.x > 0 && size.y > 0 && center.x - size.x * .5f >= 44 && center.y - size.y * .5f >= 64 &&
			                     center.x + size.x * .5f <= 1956 && center.y + size.y * .5f <= 1136;
			passed = bounded && !ClampSettingsWindowToCanvas(center, size, fallback) && passed;
		}
	}
	ImVec2 center = fallback.center, size = fallback.size;
	passed = !ClampSettingsWindowToCanvas(center, size, fallback) && passed;
	viewport->WorkSize = { 20, 20 };
	ImGui::GetStyle().WindowMinSize = { 100, 100 };
	passed = ClampSettingsWindowToCanvas(center, size, fallback) && size.x == 4 && size.y == 4 && passed;
	viewport->WorkSize = { 0, 0 };
	passed = !ClampSettingsWindowToCanvas(center, size, fallback) && passed;
	ImGui::DestroyContext();
	if (!passed)
		std::puts("FAIL: menu canvas recovery did not preserve valid or recover invalid geometry");
	return passed;
}

bool CheckLaserCancelsResize()
{
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = { 1920, 1080 };
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	ImGui::NewFrame();
	ImGui::Begin("Resize capture");
	auto* window = ImGui::GetCurrentWindow();
	const auto originalSize = window->Size;
	const auto handle = window->GetID("##SteamVRResizeBottomRight");
	ImGui::SetActiveID(handle, window);
	g_menuResizeDragState.activeId = handle;
	globals::features::vr.inHeadset = true;
	MenuHeaderRenderer::RenderResizeHandles(1, { 420, 320 }, true, true);
	bool passed = ImGui::GetActiveID() == 0 && g_menuResizeDragState.activeId == 0 && window->Size.x == originalSize.x && window->Size.y == originalSize.y;
	const auto slider = window->GetID("Menu scale");
	ImGui::SetActiveID(slider, window);
	MenuHeaderRenderer::RenderResizeHandles(1, { 420, 320 }, true, true);
	passed = ImGui::GetActiveID() == slider && passed;
	globals::features::vr.inHeadset = false;
	ImGui::End();
	ImGui::Render();
	ImGui::DestroyContext();
	if (!passed)
		std::puts("FAIL: laser handoff did not cancel only the owned resize capture");
	return passed;
}

int main()
{
	bool passed = CheckDesktopMirrorMapping() && CheckCanvasRecovery() && CheckLaserCancelsResize();
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
					 ResizeLayout{ { 1742, 1462 }, { 170, 150 }, { 170, 150 }, { 170, 150 } }) &&
		         passed;
		passed = CheckDrag(false, steamVR, false, -1, true,
					 ResizeLayout{ { 100, 100 }, { 800, 600 }, { 650, 200 }, { 650, 200 } }) &&
		         passed;
	}
	passed = CheckDrag(false, true, false, -1, false,
				 ResizeLayout{ { 100, 100 }, { 800, 600 }, { 650, 480 }, { 650, 480 } }, true) &&
	         passed;
	passed = CheckDrag(false, true, false, 2, false, {}, true) && passed;
	passed = CheckDrag(false, true, false, -1, false,
				 ResizeLayout{ { 1300, 1100 }, { 600, 500 }, { 420, 320 }, { 420, 320 } }, true) &&
	         passed;
	if (passed)
		std::puts("Locked canvas rejects resizing; OCU and SteamVR custom grips retain capture, remain visible, respect minimum/canvas bounds, and persist geometry.");
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
