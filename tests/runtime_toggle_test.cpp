#include "Utils/RuntimeToggle.h"

#include <iostream>
#include <stdexcept>
#include <thread>

struct State
{
	uint32_t frameCount = 0;
	bool save = false, engine = false, menu = false, pendingPostLoadRuntimeReset = false;
	bool IsSaveLoadSafeModeActive() const { return save; }
	bool IsEngineSaveLoadActivityActive() const { return engine; }
	bool IsMainOrLoadingMenuOpen() const { return menu; }
};

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

int main()
try {
	struct Settings
	{
		uint32_t enabled = 1;
		unsigned quality = 1;
	};
	{
		State state;
		Util::RuntimeToggle toggle(true);
		Settings settings;
		toggle.ReplaceSettings(settings, Settings{ 0, 2 }, &Settings::enabled);
		Require(!toggle.Get() && settings.enabled == 0 && settings.quality == 2, "startup settings must initialize the applied preference");
		toggle.Apply(settings.enabled, &state);
		toggle.ReplaceSettings(settings, Settings{ 1, 3 }, &Settings::enabled);
		Require(toggle.Get() && settings.enabled == 0 && settings.quality == 3, "settings replacement must stage only the enabled flag after rendering starts");
		toggle.Apply(settings.enabled, &state);
		Require(settings.enabled == 0, "same-frame replacement must remain deferred");
		state.save = true;
		++state.frameCount;
		toggle.Apply(settings.enabled, &state);
		Require(settings.enabled == 0, "settings replacement must obey save/load guards");
		state.save = false;
		++state.frameCount;
		toggle.Apply(settings.enabled, &state);
		Require(settings.enabled == 1, "settings replacement must apply on the next safe frame");
		toggle.ReplaceSettings(settings, Settings{ 0, 4 }, &Settings::enabled);
		toggle.Set(true);
		++state.frameCount;
		toggle.Apply(settings.enabled, &state);
		Require(settings.enabled == 1 && toggle.Get(), "later API request must supersede settings restore");
	}
	State state;
	Util::RuntimeToggle toggle(true);
	uint32_t applied = 1;
	toggle.Set(false);
	toggle.Set(true);
	Require(!toggle.Apply(applied, &state) && applied == 1, "opposing requests must coalesce");
	toggle.Set(false);
	Require(!toggle.Get() && applied == 1, "request must not write render settings");
	Require(!toggle.Apply(applied, &state), "same-frame request must remain deferred");
	++state.frameCount;
	Require(toggle.Apply(applied, &state) && applied == 0, "next frame must apply pending preference");
	for (bool* guard : { &state.save, &state.engine, &state.menu, &state.pendingPostLoadRuntimeReset }) {
		*guard = true;
		toggle.Set(true);
		++state.frameCount;
		Require(!toggle.Apply(applied, &state) && applied == 0, "protected state must defer changes");
		*guard = false;
		Require(!toggle.Apply(applied, &state), "clearing a guard must not split a frame");
		++state.frameCount;
		Require(toggle.Apply(applied, &state) && applied == 1, "guard release must retain latest request");
		toggle.Set(false);
		++state.frameCount;
		Require(toggle.Apply(applied, &state) && applied == 0, "disable must normalize shader flag");
	}
	toggle.Set(true);
	Require(!toggle.Apply(applied, static_cast<State*>(nullptr)) && applied == 0, "missing state must retain request");
	std::atomic<bool> running{ true };
	std::thread worker([&] {
		for (unsigned i = 0; i != 100000; ++i)
			toggle.Set((i & 1) != 0);
		toggle.Set(true);
		running.store(false);
	});
	while (running.load()) {
		++state.frameCount;
		toggle.Apply(applied, &state);
		Require(applied <= 1, "concurrent publication must preserve normalized flags");
	}
	worker.join();
	++state.frameCount;
	toggle.Apply(applied, &state);
	Require(toggle.Get() && applied == 1, "last worker request must survive concurrent snapshots");
	std::cout << "Runtime toggle coalescing, lifecycle and concurrency checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
