#include <algorithm>
#include <cmath>
#include <cstdio>

struct Window
{
	bool valid = true, topmost = false, minimized = false;
	int x = 10, y = 20, width = 800, height = 600;
};
using HWND = Window*;
using HMONITOR = int;
struct RECT
{
	int left, top, right, bottom;
};
struct MONITORINFO
{
	unsigned size;
	RECT rcWork;
};
Window topmostMarker, normalMarker, firstWindow, secondWindow;
const HWND HWND_TOPMOST = &topmostMarker, HWND_NOTOPMOST = &normalMarker;
constexpr unsigned SWP_NOSIZE = 1, SWP_NOOWNERZORDER = 2, SWP_SHOWWINDOW = 4, SWP_NOMOVE = 8, SWP_NOACTIVATE = 16;
constexpr int MONITOR_DEFAULTTONEAREST = 1, SW_RESTORE = 1, FALSE = 0;
HWND gameWindow = &firstWindow, foregroundWindow = nullptr;
unsigned positionCalls = 0, focusCalls = 0, moveCalls = 0, geometryReads = 0;
bool positioningSucceeds = true, focusSucceeds = true, geometryAvailable = true;
RECT secondMonitorWorkArea{ 1920, 0, 4480, 1440 };
bool IsWindow(HWND window) { return window && window->valid; }
bool IsWindowTopmost(HWND window) { return window->topmost; }
HWND GetGameWindowHandle() { return gameWindow; }
HWND GetForegroundWindow() { return foregroundWindow; }
bool IsIconic(HWND window) { return window->minimized; }
void ShowWindow(HWND window, int) { window->minimized = false; }
bool GetWindowRect(HWND window, RECT* rectangle)
{
	++geometryReads;
	if (!geometryAvailable)
		return false;
	*rectangle = { window->x, window->y, window->x + window->width, window->y + window->height };
	return true;
}
HMONITOR MonitorFromWindow(HWND window, int) { return window->x >= 1920 ? 2 : 1; }
bool GetMonitorInfo(HMONITOR monitor, MONITORINFO* info)
{
	info->rcWork = monitor == 1 ? RECT{ 0, 0, 1920, 1080 } : secondMonitorWorkArea;
	return true;
}
bool SetWindowPos(HWND window, HWND order, int x, int y, int, int, unsigned flags)
{
	++positionCalls;
	if (!positioningSucceeds)
		return false;
	window->topmost = order == HWND_TOPMOST;
	if (!(flags & SWP_NOMOVE)) {
		++moveCalls;
		window->x = x;
		window->y = y;
	}
	return true;
}
void BringWindowToTop(HWND) { ++focusCalls; }
void SetForegroundWindow(HWND window)
{
	++focusCalls;
	if (focusSucceeds)
		foregroundWindow = window;
}
void SetActiveWindow(HWND) { ++focusCalls; }
void SetFocus(HWND) { ++focusCalls; }
namespace Util
{
	double now = 100;
	double GetNowSecs() { return now; }
}
namespace globals
{
	struct Menu
	{
		bool open = true;
		bool IsMenuSessionOpen() const { return open; }
	} menuValue;
	Menu* menu = &menuValue;
}
enum class AttachMode
{
	None,
	HMDOnly
};
struct VR
{
	struct
	{
		bool KeepDesktopWindowFocusedForVRMenu = true;
	} settings;
	struct
	{
		bool isCompatible = true;
	} openVRInfo;
	AttachMode attachMode = AttachMode::HMDOnly;
	AttachMode GetEffectiveMenuAttachMode() const { return attachMode; }
	bool desktopWindowManagementApplied = false, desktopWindowWasTopmost = false;
	bool desktopWindowManagementReleasePending = false;
	HWND desktopWindowManagedHandle = nullptr;
	double nextDesktopWindowManagementCheckSecs = 0;
	void UpdateMenuDesktopWindowManagement(bool force = false);
	void ReleaseMenuDesktopWindowManagement();
};
#include "vr_menu_desktop_helpers_under_test.h"
#include "vr_menu_desktop_management_under_test.h"

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::printf("FAIL: %s\n", message);
	return condition;
}

int main()
{
	bool passed = true;
	VR menu;
	menu.UpdateMenuDesktopWindowManagement(true);
	passed &= Check(moveCalls == 1 && firstWindow.x == 560 && firstWindow.y == 240 && firstWindow.topmost && foregroundWindow == &firstWindow,
		"opening acquires centered, focused and topmost placement");
	const auto initialFocusCalls = focusCalls;
	for (int frame = 0; frame < 30; ++frame) {
		Util::now += 1;
		menu.UpdateMenuDesktopWindowManagement();
	}
	passed &= Check(positionCalls == 1 && focusCalls == initialFocusCalls && geometryReads == 4,
		"stable updates check actual geometry only every ten seconds and send no positioning or focus messages");
	firstWindow.x = 123;
	firstWindow.y = 456;
	for (int frame = 0; frame < 100; ++frame)
		menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(firstWindow.x == 123 && firstWindow.y == 456 && geometryReads == 4,
		"per-frame updates do not repeat geometry checks within the polling interval");
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(firstWindow.x == 560 && firstWindow.y == 240 && moveCalls == 2 && focusCalls == initialFocusCalls,
		"manual movement is detected and recentered on the next check without refocusing");
	firstWindow.width = 1000;
	firstWindow.height = 700;
	menu.UpdateMenuDesktopWindowManagement(true);
	passed &= Check(firstWindow.x == 460 && firstWindow.y == 190 && moveCalls == 3,
		"a forced check uses the current window size and bypasses the interval");
	menu.UpdateMenuDesktopWindowManagement(true);
	passed &= Check(moveCalls == 3 && positionCalls == 3 && focusCalls == initialFocusCalls,
		"forcing a stable check still avoids redundant positioning and focus messages");
	++firstWindow.x;
	--firstWindow.y;
	menu.UpdateMenuDesktopWindowManagement(true);
	passed &= Check(moveCalls == 3 && positionCalls == 3, "one-pixel native rounding does not trigger repeated positioning");
	firstWindow.x = 2200;
	firstWindow.y = 100;
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(firstWindow.x == 2700 && firstWindow.y == 370 && moveCalls == 4,
		"movement to another monitor recenters in that monitor's current work area");
	secondMonitorWorkArea = { 1980, 80, 4480, 1440 };
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(firstWindow.x == 2730 && firstWindow.y == 410 && moveCalls == 5,
		"work-area changes are detected without relying on a cached center");
	firstWindow.topmost = false;
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(firstWindow.topmost && moveCalls == 5 && focusCalls == initialFocusCalls, "topmost recovery does not move or refocus an already centered window");
	foregroundWindow = nullptr;
	focusSucceeds = false;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(focusCalls == initialFocusCalls, "an unstable window is retried at the bounded interval");
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	const auto failedFocusCalls = focusCalls;
	passed &= Check(foregroundWindow == nullptr && failedFocusCalls > initialFocusCalls,
		"failed foreground acquisition is detected");
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(focusCalls == failedFocusCalls, "failed foreground acquisition is not retried within the interval");
	focusSucceeds = true;
	Util::now += 1;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(foregroundWindow == &firstWindow && moveCalls == 5,
		"failed foreground acquisition retries after one second without repositioning");
	firstWindow.minimized = true;
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!firstWindow.minimized && moveCalls == 5, "restoring a minimized window does not reposition it when already centered");
	secondWindow.topmost = true;
	gameWindow = &secondWindow;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!firstWindow.topmost && secondWindow.topmost && moveCalls == 6 && menu.desktopWindowWasTopmost,
		"a new HWND releases the previous window and acquires its own placement and topmost baseline");
	globals::menu->open = false;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(secondWindow.topmost && !menu.desktopWindowManagementApplied, "closing preserves pre-existing topmost ownership");
	globals::menu->open = true;
	secondWindow.x = 10;
	positioningSucceeds = false;
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!menu.desktopWindowManagementApplied, "failed acquisition remains retryable");
	positioningSucceeds = true;
	Util::now += 1;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(moveCalls == 7 && menu.desktopWindowManagementApplied, "a later successful acquisition retries centering the new session");
	secondWindow.x = 30;
	geometryAvailable = false;
	Util::now += 10;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(moveCalls == 7 && secondWindow.x == 30, "failed geometry reads cannot move the window using stale coordinates");
	geometryAvailable = true;
	Util::now += 1;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(moveCalls == 8 && secondWindow.x == 560, "geometry recovery retries using the current rectangle");
	menu.settings.KeepDesktopWindowFocusedForVRMenu = false;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!menu.desktopWindowManagementApplied, "disabling management releases ownership");
	menu.settings.KeepDesktopWindowFocusedForVRMenu = true;
	menu.UpdateMenuDesktopWindowManagement();
	secondWindow.valid = false;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!menu.desktopWindowManagementApplied && !menu.desktopWindowManagedHandle, "a missing managed window releases stale ownership");

	gameWindow = &firstWindow;
	firstWindow.topmost = false;
	menu.UpdateMenuDesktopWindowManagement();
	globals::menu->open = false;
	positioningSucceeds = false;
	const auto beforeRelease = positionCalls;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(menu.desktopWindowManagementApplied && menu.desktopWindowManagementReleasePending && positionCalls == beforeRelease + 1,
		"failed topmost restoration retains ownership for retry");
	for (int frame = 0; frame < 100; ++frame)
		menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(positionCalls == beforeRelease + 1, "failed restoration cannot busy-retry on every frame");
	globals::menu->open = true;
	gameWindow = &secondWindow;
	secondWindow.valid = true;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(menu.desktopWindowManagedHandle == &firstWindow && positionCalls == beforeRelease + 1,
		"a new window cannot replace ownership while the old window still needs restoration");
	positioningSucceeds = true;
	Util::now += 1;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!firstWindow.topmost && menu.desktopWindowManagedHandle == &secondWindow && !menu.desktopWindowManagementReleasePending,
		"restoration retries after one second before acquiring the new window");

	menu.ReleaseMenuDesktopWindowManagement();
	secondWindow.topmost = false;
	menu.UpdateMenuDesktopWindowManagement();
	positioningSucceeds = false;
	globals::menu->open = false;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(menu.desktopWindowManagementReleasePending, "closing retains a failed restoration request");
	globals::menu->open = true;
	positioningSucceeds = true;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!menu.desktopWindowManagementReleasePending && secondWindow.topmost,
		"reopening the same window cancels restoration while retaining its original ownership baseline");
	globals::menu->open = false;
	menu.UpdateMenuDesktopWindowManagement();
	passed &= Check(!secondWindow.topmost && !menu.desktopWindowManagementApplied,
		"final close restores the original baseline after a canceled restoration");
	return passed ? 0 : 1;
}
