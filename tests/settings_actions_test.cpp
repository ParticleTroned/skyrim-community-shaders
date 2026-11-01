#include <cassert>
#include <string>
#include <utility>
#define T(key, english) english
namespace MenuUI
{
#include "settings_actions_types.h"
}
struct SettingsFixture
{
	bool blocked = false, defaultsSucceed = true;
	unsigned saves = 0, loads = 0, defaults = 0;
	bool IsPersistentMutationBlocked() const { return blocked; }
	void Save() { ++saves; }
	void Load() { ++loads; }
	bool RestoreDefaultSettings(std::string& error)
	{
		++defaults;
		error = defaultsSucceed ? "" : "restore failed";
		return defaultsSucceed;
	}
};
struct MenuFixture
{
	std::string error;
	void ReportSettingsSaveResult(bool success, std::string message)
	{
		assert(!success);
		error = std::move(message);
	}
};
struct CacheFixture
{
	bool awaiting = false, capturing = false;
	bool IsAwaitingMenuCloseCapture() const { return awaiting; }
	bool IsCapturingActiveShaders() const { return capturing; }
};
namespace globals
{
	SettingsFixture* state = nullptr;
	MenuFixture* menu = nullptr;
	CacheFixture* shaderCache = nullptr;
	namespace features::llf
	{
		struct ParticleFixture
		{
			unsigned reloads = 0;
			void GetConfigs() { ++reloads; }
		} particleLights;
	}
}
namespace PerformanceTuningRenderer
{
	bool active = false;
	bool HasActiveMeasurements() { return active; }
}
namespace Util
{
	unsigned requests = 0;
	const char* GetClearShaderCacheTooltip() { return "clear help"; }
	int ResolveShaderCacheClearScope() { return 7; }
	void RequestClearShaderCacheConfirmation(int scope)
	{
		assert(scope == 7);
		++requests;
	}
}
#include "settings_actions_under_test.h"
int main()
{
	using namespace MenuUI;
	using A = GlobalSettingsAction;
	InvokeGlobalSettingsAction(A::None);
	assert(!GetGlobalSettingsActionBlockReason(A::None));
	assert(GetGlobalSettingsActionBlockReason(A::Save));
	SettingsFixture state;
	MenuFixture menu;
	CacheFixture cache;
	globals::state = &state;
	globals::menu = &menu;
	globals::shaderCache = &cache;
	for (const auto action : { A::Save, A::Load, A::RestoreDefaults, A::ClearShaderCache }) {
		PerformanceTuningRenderer::active = true;
		InvokeGlobalSettingsAction(action);
		assert(!menu.error.empty());
		PerformanceTuningRenderer::active = false;
		state.blocked = true;
		InvokeGlobalSettingsAction(action);
		state.blocked = false;
		assert(!state.saves && !state.loads && !state.defaults && !Util::requests);
	}
	cache.awaiting = true;
	InvokeGlobalSettingsAction(A::ClearShaderCache);
	assert(!Util::requests);
	assert(!GetGlobalSettingsActionBlockReason(A::Save));
	cache.awaiting = false;
	cache.capturing = true;
	InvokeGlobalSettingsAction(A::ClearShaderCache);
	assert(!Util::requests);
	cache.capturing = false;
	InvokeGlobalSettingsAction(A::Save);
	InvokeGlobalSettingsAction(A::Load);
	InvokeGlobalSettingsAction(A::RestoreDefaults);
	InvokeGlobalSettingsAction(A::ClearShaderCache);
	assert(state.saves == 1 && state.loads == 1 && state.defaults == 1 && Util::requests == 1);
	assert(globals::features::llf::particleLights.reloads == 1);
	state.defaultsSucceed = false;
	InvokeGlobalSettingsAction(A::RestoreDefaults);
	assert(menu.error == "restore failed");
	// Rechecking at execution closes the gap after an enabled button was drawn.
	assert(!GetGlobalSettingsActionBlockReason(A::Save));
	PerformanceTuningRenderer::active = true;
	InvokeGlobalSettingsAction(A::Save);
	assert(state.saves == 1);
	assert(std::string(GetGlobalSettingsActionHelp(A::ClearShaderCache)) == "clear help");
}
