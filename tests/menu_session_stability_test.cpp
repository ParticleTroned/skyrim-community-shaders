#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <thread>
#include <vector>

static bool onGameThread = false;
static unsigned offThreadCameraAccesses = 0;
namespace RE
{
	struct Setting
	{
		struct
		{
			float f = 120.0f;
		} data;
		float GetFloat() const { return data.f; }
	};
	Setting setting;
	bool settingAvailable = true;
	Setting* GetINISetting(const char*) { return settingAvailable ? &setting : nullptr; }
	struct PlayerCamera
	{
		struct Runtime
		{
			float idleTimer = 0.01f;
			bool allowAutoVanityMode = true;
		} runtime;
		static inline PlayerCamera* singleton = nullptr;
		static PlayerCamera* GetSingleton()
		{
			if (!onGameThread)
				++offThreadCameraAccesses;
			return singleton;
		}
		Runtime& GetRuntimeData2() { return runtime; }
	};
}
namespace SKSE
{
	struct TaskInterface
	{
		mutable std::deque<std::function<void()>> queued;
		void AddTask(std::function<void()> task) const { queued.push_back(std::move(task)); }
		void Drain()
		{
			onGameThread = true;
			while (!queued.empty()) {
				auto task = std::move(queued.front());
				queued.pop_front();
				task();
			}
			onGameThread = false;
		}
	} tasks;
	bool tasksAvailable = true;
	const TaskInterface* GetTaskInterface() { return tasksAvailable ? &tasks : nullptr; }
}
namespace logger
{
	template <class... Args>
	void info(const char*, Args...)
	{}
	template <class... Args>
	void warn(const char*, Args...)
	{}
	void error(const char*) {}
}
#include "../src/Utils/VanityCamera.cpp"

struct Menu
{
	bool mainOpen = false, editorOpen = false, modal = false;
	Util::VanityCameraSuppressionLease vanityCameraSuppression;
	bool IsMenuSessionOpen() const { return mainOpen || editorOpen; }
	bool ShouldSwallowInput() const { return IsMenuSessionOpen() || modal; }
	void UpdateIdleCameraSuppression();
};
#include "menu_idle_camera_under_test.h"

static bool Check(bool result, const char* description)
{
	if (!result)
		std::printf("FAIL: %s\n", description);
	return result;
}

int main()
{
	bool passed = true;
	RE::PlayerCamera camera, replacement;
	RE::PlayerCamera::singleton = &camera;
	Menu menu;
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	for (int request = 0; request < 1000; ++request)
		menu.UpdateIdleCameraSuppression();
	passed &= Check(SKSE::tasks.queued.size() == 1 && camera.runtime.idleTimer == 0.01f && RE::setting.GetFloat() == 120,
		"refresh requests coalesce without accessing camera state on the UI thread");
	SKSE::tasks.Drain();
	passed &= Check(camera.runtime.idleTimer > 120 && camera.runtime.allowAutoVanityMode,
		"opening CSX refreshes an expired countdown without changing camera permission");
	for (int frame = 0; frame < 20000; ++frame) {
		camera.runtime.idleTimer -= 1.0f;
		menu.UpdateIdleCameraSuppression();
		SKSE::tasks.Drain();
	}
	passed &= Check(camera.runtime.idleTimer > 120, "long browsing sessions keep idle suppression alive");
	Util::VanityCameraSuppressionLease editor;
	editor.Acquire();
	menu.mainOpen = false;
	menu.editorOpen = true;
	menu.UpdateIdleCameraSuppression();
	editor.Release();
	SKSE::tasks.Drain();
	passed &= Check(RE::setting.GetFloat() == 10000 && camera.runtime.idleTimer > 120,
		"overlapping menu and editor ownership cannot restore early");
	menu.editorOpen = false;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(camera.runtime.allowAutoVanityMode && camera.runtime.idleTimer == 120 && RE::setting.GetFloat() == 120,
		"final close restores the delay and grants a fresh idle interval");

	camera.runtime.allowAutoVanityMode = false;
	menu.modal = true;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(!camera.runtime.allowAutoVanityMode, "preserve an already disabled vanity camera");
	RE::setting.data.f = 45;
	camera.runtime.allowAutoVanityMode = true;
	menu.UpdateIdleCameraSuppression();
	menu.modal = false;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(camera.runtime.allowAutoVanityMode && camera.runtime.idleTimer == 45 && RE::setting.GetFloat() == 45,
		"queued close preserves newer external camera permission and delay changes");

	RE::PlayerCamera::singleton = nullptr;
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	RE::PlayerCamera::singleton = &replacement;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(replacement.runtime.idleTimer > 120, "camera created during a menu session is protected");
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(replacement.runtime.idleTimer == 45, "late camera gets the restored idle interval");

	for (bool missingSetting : { false, true }) {
		RE::settingAvailable = !missingSetting;
		SKSE::tasksAvailable = missingSetting;
		menu.mainOpen = true;
		menu.UpdateIdleCameraSuppression();
		passed &= Check(!menu.vanityCameraSuppression.IsActive() && SKSE::tasks.queued.empty(),
			"missing dependencies fail without claiming ownership or scheduling work");
		RE::settingAvailable = SKSE::tasksAvailable = true;
		menu.UpdateIdleCameraSuppression();
		SKSE::tasks.Drain();
		passed &= Check(menu.vanityCameraSuppression.IsActive(), "later initialization can acquire the lease");
		menu.mainOpen = false;
		menu.UpdateIdleCameraSuppression();
		SKSE::tasks.Drain();
	}

	const float originalTimer = replacement.runtime.idleTimer;
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(RE::setting.GetFloat() == 45 && replacement.runtime.idleTimer == originalTimer,
		"closing before dispatch cancels stale suppression without touching the camera");
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	SKSE::tasks.Drain();
	passed &= Check(RE::setting.GetFloat() == 10000 && replacement.runtime.idleTimer > 120,
		"reopening before queued restoration retains suppression and the original saved delay");
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	RE::PlayerCamera::singleton = &camera;
	camera.runtime.idleTimer = 17;
	SKSE::tasks.Drain();
	passed &= Check(RE::setting.GetFloat() == 45 && camera.runtime.idleTimer == 17,
		"final release does not overwrite an unrelated replacement camera");

	{
		Util::VanityCameraSuppressionLease temporary;
		temporary.Acquire();
		SKSE::tasks.Drain();
	}
	SKSE::tasks.Drain();
	passed &= Check(RE::setting.GetFloat() == 45 && camera.runtime.idleTimer == 45,
		"destruction releases ownership and restores the effective delay");

	std::vector<std::thread> callers;
	for (int owner = 0; owner < 8; ++owner) {
		callers.emplace_back([] {
			Util::VanityCameraSuppressionLease lease;
			for (int repeat = 0; repeat < 100; ++repeat) {
				lease.Acquire();
				lease.Release();
			}
		});
	}
	for (auto& caller : callers)
		caller.join();
	passed &= Check(SKSE::tasks.queued.size() == 1 && g_vanityCameraSuppressionCount == 0,
		"concurrent independent owners coalesce work and balance their leases");
	SKSE::tasks.Drain();
	passed &= Check(offThreadCameraAccesses == 0 && RE::setting.GetFloat() == 45,
		"all native camera access runs on the game thread and canceled ownership leaves settings intact");
	if (passed)
		std::puts("Menu session stability checks passed");
	return passed ? 0 : 1;
}
