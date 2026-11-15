#include "Api/MainThreadDispatchPolicy.h"
#include "Utils/RuntimeToggle.h"

#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

namespace RE
{
	struct UI
	{
		std::vector<int> menuStack{ 1 };
		bool mainMenu = false;
	} ui;
	struct PlayerCharacter
	{
		bool hasCell = true;
		static PlayerCharacter* GetSingleton();
		const void* GetParentCell() const { return hasCell ? this : nullptr; }
	} player;
	bool playerAvailable = true;
	PlayerCharacter* PlayerCharacter::GetSingleton() { return playerAvailable ? &player : nullptr; }
	struct Console
	{
		static inline std::vector<std::string> commands;
		static inline bool fail = false;
		static void ExecuteCommand(const char* command)
		{
			if (fail)
				throw std::runtime_error("console unavailable");
			commands.emplace_back(command);
		}
	};
}

namespace globals
{
	struct State
	{
		bool safeMode = false, engineActivity = false, mainMenu = false, loadingMenu = false;
		bool pendingPostLoadRuntimeReset = false, worldTransition = false;
		std::atomic_uint32_t worldLoadTransitionStartFrame{ 0 };
		mutable int liveMenuQueries = 0;
		bool IsSaveLoadSafeModeActive() const { return safeMode; }
		bool IsEngineSaveLoadActivityActive() const { return engineActivity; }
		bool IsWorldLoadTransitionActive() const { return worldTransition; }
		bool IsMainOrLoadingMenuOpen() const { return mainMenu || loadingMenu; }
		bool IsMainOrLoadingMenuOpen(RE::UI* ui) const
		{
			++liveMenuQueries;
			return IsMainOrLoadingMenuOpen() || ui->mainMenu;
		}
	} stateStorage;
	State* state = &stateStorage;
	namespace game
	{
		RE::UI* ui = &RE::ui;
	}
}

struct PerformanceTuningRenderer
{
	static inline bool active = false;
	static bool HasActiveMeasurements() { return active; }
};

namespace SKSE
{
	namespace stl
	{
		template <class F>
		struct scope_exit
		{
			F fn;
			explicit scope_exit(F f) : fn(std::move(f)) {}
			~scope_exit() { fn(); }
		};
	}
	struct TaskInterface
	{
		enum class Mode
		{
			Queue,
			Throw,
			RetainThenThrow,
			RunThenThrow
		} mode = Mode::Queue;
		std::vector<std::function<void()>> queue;
		void AddTask(std::function<void()> task)
		{
			if (mode == Mode::RunThenThrow)
				task();
			else if (mode != Mode::Throw)
				queue.push_back(std::move(task));
			if (mode != Mode::Queue)
				throw std::runtime_error("submission failed");
		}
		void Drain()
		{
			auto pending = std::exchange(queue, {});
			for (const auto& task : pending)
				task();
		}
	} tasks;
	bool available = true;
	TaskInterface* GetTaskInterface() { return available ? &tasks : nullptr; }
}

namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
	template <class... T>
	void error(const char*, T&&...)
	{}
}

namespace Util
{
	struct DisableGuard
	{
		explicit DisableGuard(bool disabled) { ImGui::BeginDisabled(disabled); }
		~DisableGuard() { ImGui::EndDisabled(); }
	};
	struct HoverTooltipWrapper
	{
		explicit operator bool() const { return false; }
	};
	enum class ActionGlyph
	{
		Teleport
	};
	void DrawActionGlyph(ImDrawList*, ActionGlyph, ImVec2, ImVec2, ImU32) {}
}

static const char* GetCocLabel(const std::string& id);

struct AdaptiveBrightness
{
	struct LocationOverride
	{
		std::string key, cocCode, name = "Saved destination";
	};
	struct Settings
	{
		std::vector<LocationOverride> locationOverrides{ { "cell-key", "WhiterunBreezehome" } };
	} settings;
	std::vector<LocationOverride>& locations = settings.locationOverrides;
	bool loaded = true;
#include "adaptive_balance_travel_members.h"
	const LocationOverride* FindLocationOverride(const std::string& key) const
	{
		const auto found = std::ranges::find(locations, key, &LocationOverride::key);
		return found == locations.end() ? nullptr : &*found;
	}
	const char* GetLocationTeleportBlockReason() const;
	static const char* GetLocationTeleportError(const LocationOverride&, const char*);
	bool TeleportToLocationOverride(const std::string&, std::string&);

	void DrawCoc(const LocationOverride& locationOverride)
	{
		const auto* teleportBlockReason = GetLocationTeleportBlockReason();
		const auto selectOverride = [] {};
#include "adaptive_balance_travel_ui_under_test.h"
	}
};

static const char* GetCocLabel(const std::string& id) { return id.empty() ? "-" : id.c_str(); }
#include "adaptive_balance_travel_under_test.h"

namespace globals::features
{
	AdaptiveBrightness adaptiveBrightness;
}

using json = nlohmann::json;
template <class F>
static json RunOnMainThread(F run)
{
	return run();
}

static json MenuAction(const json& a_args)
{
	const std::string action = a_args.value("action", std::string("status"));
#include "adaptive_balance_travel_menu_under_test.h"
	throw std::runtime_error("unexpected menu action");
}

static void CheckMenuActions()
{
	for (const auto& request : std::vector<json>{ { { "action", "teleport_adaptive_balance_location" } },
			 { { "action", "teleport_adaptive_balance_location" }, { "locationKey", 4 } },
			 { { "action", "teleport_adaptive_balance_location" }, { "locationKey", "" } },
			 { { "action", "teleport_adaptive_balance_location" }, { "locationKey", "missing" } } })
		Require(MenuAction(request)["ok"] == false && SKSE::tasks.queue.empty(), "invalid menu request reached the queue");
	const auto list = MenuAction({ { "action", "adaptive_balance_locations" } });
	Require(list["ok"] == true && list["locations"].size() == 1 && list["locations"][0]["canTeleport"] == true, "saved destinations were not discoverable");
	const auto queued = MenuAction({ { "action", "teleport_adaptive_balance_location" }, { "locationKey", "cell-key" } });
	Require(queued["ok"] == true && queued["queued"] == true && RE::Console::commands.empty(), "menu result claimed travel before queue execution");
	const auto pending = MenuAction({ { "action", "adaptive_balance_locations" } });
	Require(pending["locations"][0]["canTeleport"] == false && pending["travelStatus"].get<std::string>().find("queued") != std::string::npos, "queued state was not observable");
	SKSE::tasks.Drain();
	const auto dispatched = MenuAction({ { "action", "adaptive_balance_locations" } });
	Require(dispatched["locations"][0]["canTeleport"] == true && dispatched["travelStatus"] == "Teleport command dispatched." && RE::Console::commands.size() == 1, "menu did not report command dispatch");
	RE::Console::commands.clear();
	globals::features::adaptiveBrightness.loaded = false;
	Require(MenuAction({ { "action", "adaptive_balance_locations" } })["ok"] == false, "unavailable feature was accepted");
	globals::features::adaptiveBrightness.loaded = true;
}

static void CheckRequests()
{
	AdaptiveBrightness balance;
	std::string error;
	for (const auto& id : std::vector<std::string>{ "", "cell; tgm", "cell name", "cell\n", "\"cell\"", std::string("cell\0tgm", 8), "cell\xE9" }) {
		balance.locations.front().cocCode = id;
		Require(!balance.TeleportToLocationOverride("cell-key", error) && !error.empty() && SKSE::tasks.queue.empty(), "invalid EditorID reached the command queue");
	}
	balance.locations.front().cocCode = "WhiterunBreezehome";
	Require(!balance.TeleportToLocationOverride("missing-key", error), "unknown saved override was accepted");
	Require(balance.TeleportToLocationOverride("cell-key", error) && error.empty(), "valid destination was rejected");
	Require(!balance.TeleportToLocationOverride("cell-key", error) && SKSE::tasks.queue.size() == 1, "duplicate queued travel was accepted");
	balance.locations.front().cocCode = "ChangedAfterQueueing";
	SKSE::tasks.Drain();
	Require(RE::Console::commands == std::vector<std::string>{ "coc WhiterunBreezehome" }, "queued command borrowed mutable settings");
	Require(!balance.locationTeleportPending && std::string_view(balance.locationTeleportStatus.load()) == "Teleport command dispatched.", "dispatch did not publish its result");
	RE::Console::commands.clear();

	for (auto flag : { &globals::State::safeMode, &globals::State::engineActivity, &globals::State::mainMenu, &globals::State::loadingMenu,
			 &globals::State::pendingPostLoadRuntimeReset, &globals::State::worldTransition }) {
		globals::stateStorage.*flag = true;
		Require(!balance.TeleportToLocationOverride("cell-key", error), "unsafe save/load state was admitted");
		globals::stateStorage.*flag = false;
		Require(balance.TeleportToLocationOverride("cell-key", error), "travel did not recover after save/load");
		globals::stateStorage.*flag = true;
		SKSE::tasks.Drain();
		Require(RE::Console::commands.empty() && !balance.locationTeleportPending && balance.locationTeleportStatus.load(), "queued travel survived a save/load transition");
		globals::stateStorage.*flag = false;
	}
	for (bool* flag : { &PerformanceTuningRenderer::active, &RE::ui.mainMenu }) {
		*flag = true;
		Require(!balance.TeleportToLocationOverride("cell-key", error), "blocked runtime state was admitted");
		*flag = false;
		Require(balance.TeleportToLocationOverride("cell-key", error), "runtime state did not recover");
		*flag = true;
		SKSE::tasks.Drain();
		Require(RE::Console::commands.empty() && !balance.locationTeleportPending, "queued travel bypassed a runtime restriction");
		*flag = false;
	}
	for (bool* flag : { &RE::playerAvailable, &RE::player.hasCell, &SKSE::available }) {
		*flag = false;
		Require(!balance.TeleportToLocationOverride("cell-key", error), "unavailable runtime service was admitted");
		*flag = true;
	}
	globals::game::ui = nullptr;
	Require(!balance.TeleportToLocationOverride("cell-key", error), "missing UI was admitted");
	globals::game::ui = &RE::ui;
	globals::state = nullptr;
	Require(!balance.TeleportToLocationOverride("cell-key", error), "missing state was admitted");
	globals::state = &globals::stateStorage;
	RE::ui.menuStack.clear();
	Require(!balance.TeleportToLocationOverride("cell-key", error), "missing console menu stack was admitted");
	RE::ui.menuStack.push_back(1);
	Require(balance.TeleportToLocationOverride("cell-key", error), "world-change test was not queued");
	++globals::stateStorage.worldLoadTransitionStartFrame;
	SKSE::tasks.Drain();
	Require(RE::Console::commands.empty() && !balance.locationTeleportPending &&
				std::string_view(balance.locationTeleportStatus.load()).find("cancelled") != std::string_view::npos,
		"queued travel reached a different loaded world");

	for (auto mode : { SKSE::TaskInterface::Mode::Throw, SKSE::TaskInterface::Mode::RetainThenThrow }) {
		SKSE::tasks.mode = mode;
		Require(!balance.TeleportToLocationOverride("cell-key", error) && !error.empty() && !balance.locationTeleportPending, "failed submission left travel pending");
		SKSE::tasks.mode = SKSE::TaskInterface::Mode::Queue;
		Require(balance.TeleportToLocationOverride("cell-key", error), "failed submission prevented retry");
		SKSE::tasks.Drain();
		Require(RE::Console::commands.size() == 1, "cancelled submission executed after a retry");
		RE::Console::commands.clear();
	}
	SKSE::tasks.mode = SKSE::TaskInterface::Mode::RunThenThrow;
	Require(balance.TeleportToLocationOverride("cell-key", error) && error.empty() && RE::Console::commands.size() == 1, "admitted command was reported rejected");
	SKSE::tasks.mode = SKSE::TaskInterface::Mode::Queue;
	RE::Console::commands.clear();
	RE::Console::fail = true;
	Require(balance.TeleportToLocationOverride("cell-key", error), "console failure test was not queued");
	SKSE::tasks.Drain();
	Require(!balance.locationTeleportPending && std::string_view(balance.locationTeleportStatus.load()).find("failed") != std::string_view::npos, "console failure did not release and report");
	RE::Console::fail = false;

	globals::stateStorage.liveMenuQueries = 0;
	const auto* reason = balance.GetLocationTeleportBlockReason();
	for (int row = 0; row < 1000; ++row)
		Require(!balance.GetLocationTeleportError(balance.locations.front(), reason), "valid row was blocked");
	Require(globals::stateStorage.liveMenuQueries == 1, "row validation repeated live menu queries");
}

static void CheckLayout()
{
	for (float fontSize : { 16.0f, 24.0f, 32.0f }) {
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = { 1280, 1024 };
		ImFontConfig font;
		font.SizePixels = fontSize;
		io.Fonts->AddFontDefault(&font);
		io.Fonts->Build();
		AdaptiveBrightness balance;
		for (float width : { 40.0f, 80.0f, 160.0f, 360.0f }) {
			for (const char* code : { "Whiterun", "AnUnbrokenCellEditorIDThatMustWrapWithoutClippingTheTeleportButton" }) {
				for (int frame = 0; frame < 3; ++frame) {
					ImGui::NewFrame();
					ImGui::SetNextWindowSize({ 640, 900 });
					ImGui::Begin("Fixture");
					if (ImGui::BeginTable("SavedOverrides", 1, ImGuiTableFlags_Borders, { width, 0 })) {
						ImGui::TableNextColumn();
						const auto clip = ImGui::GetWindowDrawList()->_ClipRectStack.back();
						const int styleDepth = GImGui->StyleVarStack.Size;
						balance.DrawCoc({ "cell-key", code });
						const auto minimum = ImGui::GetItemRectMin();
						const auto maximum = ImGui::GetItemRectMax();
						Require(minimum.x >= clip.x - .5f && maximum.x <= clip.z + .5f, "teleport button clipped in a narrow or wrapped COC cell");
						Require(GImGui->StyleVarStack.Size == styleDepth && GImGui->DisabledStackSize == 0, "COC controls leaked ImGui state");
						ImGui::EndTable();
					}
					ImGui::End();
					ImGui::Render();
				}
			}
		}
		ImGui::DestroyContext();
	}
}

int main()
{
	CheckRequests();
	CheckMenuActions();
	CheckLayout();
	std::puts("PASS: destination validation, transition gates, queue ownership, failure recovery, readiness reuse and COC layout");
}
