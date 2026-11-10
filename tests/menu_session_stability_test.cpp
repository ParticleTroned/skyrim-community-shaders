#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

static thread_local bool onGameThread = false;
static std::atomic<unsigned> offThreadNativeAccesses = 0;
namespace RE
{
	struct Main
	{};

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
	Setting* GetINISetting(const char*)
	{
		if (!onGameThread)
			++offThreadNativeAccesses;
		return settingAvailable ? &setting : nullptr;
	}
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
				++offThreadNativeAccesses;
			return singleton;
		}
		Runtime& GetRuntimeData2() { return runtime; }
	};
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

namespace REL
{
	template <class Signature>
	using Relocation = std::function<Signature>;
}
#include "menu_game_update_under_test.h"

static unsigned gameUpdates = 0;
static unsigned originalGameUpdates = 0;
static void GameUpdate()
{
	onGameThread = true;
	Main_Update::thunk(nullptr, 0.0f);
	onGameThread = false;
	++gameUpdates;
}

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
	Main_Update::func = [](RE::Main*, float) { ++originalGameUpdates; };
	Menu menu;
	passed &= Check(!menu.vanityCameraSuppression.Acquire(), "ownership fails closed before the first game update");
	GameUpdate();
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	for (int request = 0; request < 1000; ++request)
		menu.UpdateIdleCameraSuppression();
	passed &= Check(camera.runtime.idleTimer == 0.01f && RE::setting.GetFloat() == 120,
		"ownership requests never access native state on the UI thread");
	GameUpdate();
	passed &= Check(camera.runtime.idleTimer > 120 && camera.runtime.allowAutoVanityMode,
		"opening CSX refreshes an expired countdown without changing camera permission");
	for (int frame = 0; frame < 20000; ++frame) {
		camera.runtime.idleTimer -= 1.0f;
		GameUpdate();
	}
	passed &= Check(camera.runtime.idleTimer > 120, "long browsing sessions keep idle suppression alive");
	Util::VanityCameraSuppressionLease editor;
	editor.Acquire();
	menu.mainOpen = false;
	menu.editorOpen = true;
	menu.UpdateIdleCameraSuppression();
	editor.Release();
	GameUpdate();
	passed &= Check(RE::setting.GetFloat() == 10000 && camera.runtime.idleTimer > 120,
		"overlapping menu and editor ownership cannot restore early");
	menu.editorOpen = false;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(camera.runtime.allowAutoVanityMode && camera.runtime.idleTimer == 120 && RE::setting.GetFloat() == 120,
		"final close restores the delay and grants a fresh idle interval");

	camera.runtime.allowAutoVanityMode = false;
	menu.modal = true;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(!camera.runtime.allowAutoVanityMode, "preserve an already disabled vanity camera");
	RE::setting.data.f = 45;
	camera.runtime.allowAutoVanityMode = true;
	menu.UpdateIdleCameraSuppression();
	menu.modal = false;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(camera.runtime.allowAutoVanityMode && camera.runtime.idleTimer == 45 && RE::setting.GetFloat() == 45,
		"close before the next game update preserves newer external camera permission and delay changes");

	RE::PlayerCamera::singleton = nullptr;
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	RE::PlayerCamera::singleton = &replacement;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(replacement.runtime.idleTimer > 120, "camera created during a menu session is protected");
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(replacement.runtime.idleTimer == 45, "late camera gets the restored idle interval");

	RE::settingAvailable = false;
	GameUpdate();
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	passed &= Check(!menu.vanityCameraSuppression.IsActive(),
		"missing game-thread dependencies fail without claiming ownership");
	RE::settingAvailable = true;
	GameUpdate();
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(menu.vanityCameraSuppression.IsActive(), "later initialization can acquire the lease");
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();

	const float originalTimer = replacement.runtime.idleTimer;
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(RE::setting.GetFloat() == 45 && replacement.runtime.idleTimer == originalTimer,
		"closing before the game update cancels stale suppression without touching the camera");
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	menu.mainOpen = true;
	menu.UpdateIdleCameraSuppression();
	GameUpdate();
	passed &= Check(RE::setting.GetFloat() == 10000 && replacement.runtime.idleTimer > 120,
		"reopening before restoration retains suppression and the original saved delay");
	menu.mainOpen = false;
	menu.UpdateIdleCameraSuppression();
	RE::PlayerCamera::singleton = &camera;
	camera.runtime.idleTimer = 17;
	GameUpdate();
	passed &= Check(RE::setting.GetFloat() == 45 && camera.runtime.idleTimer == 17,
		"final release does not overwrite an unrelated replacement camera");

	{
		Util::VanityCameraSuppressionLease temporary;
		temporary.Acquire();
		GameUpdate();
	}
	GameUpdate();
	passed &= Check(RE::setting.GetFloat() == 45 && camera.runtime.idleTimer == 45,
		"destruction releases ownership and restores the effective delay");

	std::barrier phase(9);
	std::vector<std::thread> callers;
	for (int owner = 0; owner < 8; ++owner) {
		callers.emplace_back([&] {
			Util::VanityCameraSuppressionLease lease;
			for (int repeat = 0; repeat < 100; ++repeat) {
				phase.arrive_and_wait();
				lease.Acquire();
				lease.Release();
				phase.arrive_and_wait();
			}
		});
	}
	for (int repeat = 0; repeat < 100; ++repeat) {
		phase.arrive_and_wait();
		GameUpdate();
		phase.arrive_and_wait();
	}
	for (auto& caller : callers)
		caller.join();
	passed &= Check(g_vanityCameraSuppressionCount == 0,
		"concurrent ownership and game updates balance their leases");
	GameUpdate();
	passed &= Check(offThreadNativeAccesses == 0 && RE::setting.GetFloat() == 45,
		"all native camera and INI access runs on the game thread and final release restores settings");
	passed &= Check(originalGameUpdates == gameUpdates, "the production main hook forwards each update exactly once");
	if (passed)
		std::puts("Menu session stability checks passed");
	return passed ? 0 : 1;
}
