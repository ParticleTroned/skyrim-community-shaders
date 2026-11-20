#include "Features/Upscaling/NeuralRendering/CombatPausePolicy.h"
#include "Features/Upscaling/NeuralRendering/CombatState.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <mutex>
#include <string_view>
#include <thread>

thread_local bool onMainThread = false;
std::atomic<unsigned> offThreadCombatReads{ 0 };

namespace RE
{
	struct MainMenu
	{
		static constexpr std::string_view MENU_NAME = "MainMenu";
	};
	struct LoadingMenu
	{
		static constexpr std::string_view MENU_NAME = "LoadingMenu";
	};
	struct MapMenu
	{
		static constexpr std::string_view MENU_NAME = "MapMenu";
	};
	struct UI
	{
		bool paused = false;
		bool mainMenu = false, loading = false, mapMenu = false;
		bool GameIsPaused() const { return paused; }
		bool IsMenuOpen(std::string_view name) const
		{
			if (!onMainThread)
				++offThreadCombatReads;
			return name == MainMenu::MENU_NAME ? mainMenu : name == LoadingMenu::MENU_NAME ? loading :
			                                                                                 mapMenu;
		}
	};
	struct PlayerCharacter
	{
		static inline PlayerCharacter* instance = nullptr;
		bool combat = false;
		unsigned samples = 0;
		static PlayerCharacter* GetSingleton()
		{
			if (!onMainThread)
				++offThreadCombatReads;
			return instance;
		}
		bool IsInCombat()
		{
			if (!onMainThread)
				++offThreadCombatReads;
			++samples;
			return combat;
		}
	};
}
namespace Util
{
	template <class Menu>
	bool IsMenuOpen(RE::UI* ui)
	{
		return ui && ui->IsMenuOpen(Menu::MENU_NAME);
	}
}
struct State
{
	std::uint32_t frameCount = 1, lastWorldRenderFrame = 1, lastCompletedWorldRenderFrame = 1;
};
namespace globals
{
	State* state = nullptr;
	namespace game
	{
		RE::UI* ui = nullptr;
	}
}
std::uint64_t nowMs = 100;
std::uint64_t GetTickCount64() { return nowMs; }

struct Upscaling
{
	enum class NeuralStereoRouteRole
	{
		Main,
		Submit,
		Count
	};
	enum class NeuralStereoFallbackReason
	{
		None,
		MenuContext,
		GamePaused,
		TemporalSourceStale,
		CombatPause,
		RouteIneligible
	};
	struct Settings
	{
		bool neuralRenderingEnabled = true;
		bool neuralRenderingPauseInCombat = true;
	} settings;
	mutable std::mutex neuralCombatPauseMutex;
	mutable NeuralRendering::CombatPausePolicy neuralCombatPause;
	mutable NeuralRendering::CombatState neuralCombatState;
	std::atomic<std::uint32_t> neuralTemporalAdmissionLatch{ 0 };
	unsigned resets = 0;
	bool IsNeuralRenderingEnabled() const { return settings.neuralRenderingEnabled; }
	bool IsNeuralRenderingRequested() const { return IsNeuralRenderingEnabled(); }
	void RequestHistoryReset() { ++resets; }
	void UpdateNeuralCombatState();
	NeuralRendering::CombatPauseSnapshot GetNeuralCombatPauseSnapshot() const noexcept;
	NeuralRendering::CombatPauseSnapshot ResolveNeuralCombatPause() const noexcept;
	NeuralRendering::TemporalAdmissionResult BuildNeuralTemporalAdmission(NeuralStereoRouteRole, bool, bool = false) const noexcept;
	void ObserveNeuralTemporalAdmission(NeuralStereoRouteRole, const NeuralRendering::TemporalAdmissionResult&) noexcept;
	static NeuralStereoFallbackReason GetNeuralTemporalFallbackReason(const NeuralRendering::TemporalAdmissionResult&) noexcept;
};

#include "neural_combat_pause_under_test.h"

void Require(bool value, const char* message)
{
	if (!value) {
		std::fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

void PublishMainUpdate(Upscaling& upscaling)
{
	onMainThread = true;
	upscaling.UpdateNeuralCombatState();
	onMainThread = false;
}

void CheckUnavailableInputsAndConcurrentRoutes(State& state, RE::UI& ui, RE::PlayerCharacter& player)
{
	using Role = Upscaling::NeuralStereoRouteRole;
	using Phase = NeuralRendering::CombatPausePhase;
	const auto restore = [&] {
		globals::state = &state;
		globals::game::ui = &ui;
		RE::PlayerCharacter::instance = &player;
		++state.frameCount;
		state.lastWorldRenderFrame = state.lastCompletedWorldRenderFrame = state.frameCount;
		ui.paused = false;
		ui.mainMenu = ui.loading = ui.mapMenu = false;
		player.combat = false;
	};
	for (int missing = 0; missing < 5; ++missing) {
		restore();
		Upscaling unavailable;
		PublishMainUpdate(unavailable);
		Require(unavailable.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
			"Available state must establish a peaceful sample before it disappears");
		++state.frameCount;
		if (missing == 0)
			globals::state = nullptr;
		else if (missing == 1)
			globals::game::ui = nullptr;
		else if (missing == 2)
			RE::PlayerCharacter::instance = nullptr;
		else if (missing == 3)
			ui.mainMenu = true;
		else
			ui.loading = true;
		const auto samples = player.samples;
		PublishMainUpdate(unavailable);
		Require(!unavailable.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
					unavailable.GetNeuralCombatPauseSnapshot().phase == Phase::Unavailable && player.samples == samples,
			"Unavailable world, UI or player must block NR without sampling an actor");
		restore();
		PublishMainUpdate(unavailable);
		Require(!unavailable.BuildNeuralTemporalAdmission(Role::Submit, false, true).admitted &&
					unavailable.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
			"Restored gameplay must begin a complete safe interval");
	}
	restore();
	player.combat = true;
	Upscaling concurrent;
	PublishMainUpdate(concurrent);
	const auto samples = player.samples;
	std::atomic<bool> coherent{ true };
	std::array<std::thread, 8> readers;
	for (std::size_t index = 0; index < readers.size(); ++index) {
		readers[index] = std::thread([&, index] {
			for (unsigned read = 0; read < 32; ++read) {
				const auto admission = concurrent.BuildNeuralTemporalAdmission(index % 2 ? Role::Submit : Role::Main, false, true);
				if (admission.admitted || !admission.combatPaused || concurrent.GetNeuralCombatPauseSnapshot().phase != Phase::Combat)
					coherent.store(false, std::memory_order_relaxed);
			}
		});
	}
	for (auto& reader : readers)
		reader.join();
	Require(coherent.load() && player.samples == samples,
		"Concurrent main/submit readers must consume the main-thread combat sample without actor access");
}

void CheckPublishedSnapshots()
{
	NeuralRendering::CombatState state;
	Require(!state.ConsumeSnapshot().inCombat && state.ConsumeSnapshot().gamePaused,
		"Combat state must be unavailable before the first main update");
	state.Publish(false, false, 100);
	const auto peaceful = state.ConsumeSnapshot();
	state.Publish(std::nullopt, true, 101);
	const auto unavailable = state.ConsumeSnapshot();
	Require(!unavailable.inCombat && unavailable.gamePaused &&
				peaceful.inCombat == false && !peaceful.gamePaused,
		"Unavailable publication must replace peaceful state without changing earlier value snapshots");
	std::latch start{ 2 };
	std::jthread publisher([&] {
		start.arrive_and_wait();
		for (unsigned i = 0; i < 10000; ++i)
			state.Publish(i % 2 ? std::optional<bool>{ false } : std::nullopt, i % 2 == 0, i);
	});
	start.arrive_and_wait();
	for (unsigned i = 0; i < 10000; ++i) {
		const auto sample = state.ConsumeSnapshot();
		Require(sample.inCombat ? !*sample.inCombat && !sample.gamePaused : sample.gamePaused,
			"Concurrent publication must keep combat availability and menu state coherent");
	}
}

void CheckFrameContinuity()
{
	NeuralRendering::CombatPausePolicy peaceful;
	Require(!peaceful.Update(1, true, 10000, false).IsPaused(), "Peaceful policy setup failed");
	Require(!peaceful.Update(3, true, 10001, false).IsPaused(), "Peaceful frame gaps must not invent combat recovery");
	Require(!peaceful.Update(4, true, 1000, false).IsPaused(), "Peaceful clock reversal must not invent combat recovery");
	Require(!peaceful.Update(0, true, 1001, false).IsPaused(), "Peaceful frame reset must not invent combat recovery");
	Require(!peaceful.Update(UINT32_MAX, true, 1002, false).IsPaused() &&
				!peaceful.Update(0, true, 1003, false).IsPaused(),
		"Frame counter wrap must preserve peaceful admission");
	peaceful.Update(1, false, 1004, std::nullopt);
	Require(!peaceful.Update(4, true, 1005, false).IsPaused(), "Enabling across a frame gap must preserve peaceful admission");

	NeuralRendering::CombatPausePolicy policy;
	Require(policy.Update(1, true, 100, true).IsPaused(), "Policy combat setup failed");
	Require(policy.Update(2, true, 101, false).remainingMs == 3000, "Policy cooldown setup failed");
	Require(policy.Update(4, true, 10000, false).remainingMs == 3000, "Unsampled frames must interrupt the safe interval");
	Require(policy.Update(5, true, 5, false).remainingMs == 3000, "Clock rollback must restart the interval without underflow");
	Require(policy.Update(1, true, 10005, false).remainingMs == 3000, "Frame reset must restart the interval");
	Require(!policy.Update(2, false, 10006, std::nullopt).IsPaused(), "Disabling must always release the pause on a new frame");
}

void CheckPublicationContinuity(State& state, RE::UI& ui, RE::PlayerCharacter& player)
{
	using Role = Upscaling::NeuralStereoRouteRole;
	using Phase = NeuralRendering::CombatPausePhase;
	ui.paused = ui.mainMenu = ui.loading = ui.mapMenu = false;
	player.combat = true;
	const auto advance = [&](Upscaling& target, std::uint64_t elapsed = 1, bool publish = true) {
		++state.frameCount;
		state.lastWorldRenderFrame = state.lastCompletedWorldRenderFrame = state.frameCount;
		nowMs += elapsed;
		if (publish)
			PublishMainUpdate(target);
	};
	Upscaling recovering;
	advance(recovering);
	Require(!recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted, "Continuity combat setup failed");
	player.combat = false;
	advance(recovering);
	Require(!recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted, "Continuity recovery setup failed");
	advance(recovering, 10000, false);
	Require(!recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
				recovering.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
		"Rendering without a new main update must not advance combat recovery");

	ui.mapMenu = true;
	PublishMainUpdate(recovering);
	ui.mapMenu = false;
	PublishMainUpdate(recovering);
	advance(recovering, 1, false);
	Require(!recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
				recovering.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
		"An open-close map interval between render frames must restart recovery");
	advance(recovering, 3000);
	Require(recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
		"A consumed interruption must not keep restarting subsequent recovery");

	ui.mapMenu = true;
	PublishMainUpdate(recovering);
	ui.mapMenu = false;
	PublishMainUpdate(recovering);
	advance(recovering, 1, false);
	Require(recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
		"A peaceful open-close map interval must not invent combat recovery");

	player.combat = true;
	PublishMainUpdate(recovering);
	player.combat = false;
	PublishMainUpdate(recovering);
	Require(recovering.BuildNeuralTemporalAdmission(Role::Submit, false, true).admitted,
		"The second route must retain its current frame without consuming a pending interruption");
	advance(recovering, 1, false);
	Require(!recovering.BuildNeuralTemporalAdmission(Role::Submit, false, true).admitted &&
				recovering.GetNeuralCombatPauseSnapshot().phase == Phase::Cooldown,
		"Combat beginning and ending between render frames must still start recovery");
	RE::PlayerCharacter::instance = nullptr;
	PublishMainUpdate(recovering);
	RE::PlayerCharacter::instance = &player;
	nowMs += 3000;
	PublishMainUpdate(recovering);
	advance(recovering, 1, false);
	Require(!recovering.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
				recovering.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
		"Unavailable state overwritten before rendering must still restart recovery");
}

int main()
{
	using Phase = NeuralRendering::CombatPausePhase;
	using Role = Upscaling::NeuralStereoRouteRole;
	State state;
	RE::UI ui;
	RE::PlayerCharacter player;
	globals::state = &state;
	globals::game::ui = &ui;
	RE::PlayerCharacter::instance = &player;
	Upscaling upscaling;
	const auto nextFrame = [&](Upscaling& target, std::uint64_t elapsed = 1) {
		++state.frameCount;
		state.lastWorldRenderFrame = state.lastCompletedWorldRenderFrame = state.frameCount;
		nowMs += elapsed;
		PublishMainUpdate(target);
	};
	const auto admit = [&](Role role) {
		const auto result = upscaling.BuildNeuralTemporalAdmission(role, false, true);
		upscaling.ObserveNeuralTemporalAdmission(role, result);
		return result;
	};
	Require(!upscaling.GetNeuralCombatPauseSnapshot().sampled, "Status must not sample gameplay");
	Upscaling unsampled;
	Require(!unsampled.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted && player.samples == 0,
		"Rendering before the first main update must fail closed without querying the player");
	PublishMainUpdate(upscaling);
	Require(admit(Role::Main).admitted, "Initial peaceful gameplay must remain admitted");
	player.combat = true;
	PublishMainUpdate(upscaling);
	Require(admit(Role::Submit).admitted && player.samples == 2,
		"A newly published combat sample must not split an already latched stereo frame");
	nextFrame(upscaling);
	const auto combat = admit(Role::Main);
	Require(!combat.admitted && combat.combatPaused, "Combat must block NR admission");
	Require(Upscaling::GetNeuralTemporalFallbackReason(combat) == Upscaling::NeuralStereoFallbackReason::CombatPause, "Combat must publish its fallback reason");
	Require(std::string_view(NeuralRendering::GetTemporalAdmissionBlockReasonName(combat.blockReason)) == "combat_pause", "Combat telemetry reason must be named");
	Require(!admit(Role::Submit).admitted && upscaling.resets == 2, "Entering combat must invalidate both route histories once");
	Require(!admit(Role::Submit).admitted && upscaling.resets == 2, "Repeated eyes must not repeat history invalidation");
	player.combat = false;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().remainingMs == 3000, "Resume interval starts at first peaceful sample");
	nextFrame(upscaling, 2999);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().remainingMs == 1, "Do not resume before the full interval");
	player.combat = true;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Renewed combat must interrupt cooldown");
	player.combat = false;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Renewed combat must restart the full cooldown");
	nextFrame(upscaling, 3000);
	Require(admit(Role::Main).admitted && admit(Role::Submit).admitted && upscaling.resets == 4, "Resume must reset both histories before evaluation");

	player.combat = true;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Combat re-entry must pause again");
	player.combat = false;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Cooldown must remain blocked");
	ui.paused = true;
	nextFrame(upscaling, 1000);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().phase == Phase::Unavailable, "Paused menus must interrupt the cooldown even with retained-world continuity");
	ui.paused = false;
	nextFrame(upscaling, 10000);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().remainingMs == 3000, "Time in paused menus must not satisfy cooldown");
	ui.loading = true;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Loading must block combat admission");
	ui.loading = false;
	RE::PlayerCharacter::instance = nullptr;
	nextFrame(upscaling);
	Require(!admit(Role::Submit).admitted && upscaling.GetNeuralCombatPauseSnapshot().phase == Phase::Unavailable, "Missing player must fail closed");
	RE::PlayerCharacter::instance = &player;
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Player recovery must start a new interval");
	nextFrame(upscaling, 3000);
	Require(admit(Role::Submit).admitted, "Player recovery must eventually resume");

	upscaling.settings.neuralRenderingPauseInCombat = false;
	player.combat = true;
	nextFrame(upscaling);
	const auto samples = player.samples;
	Require(admit(Role::Main).admitted && player.samples == samples, "Disabled render admission must not query player combat state");
	upscaling.settings.neuralRenderingPauseInCombat = true;
	Require(admit(Role::Submit).admitted, "A setting change must not split the current stereo frame");
	nextFrame(upscaling);
	Require(!admit(Role::Main).admitted, "Enabling combat pause applies on the next frame");
	upscaling.settings.neuralRenderingEnabled = false;
	nextFrame(upscaling);
	Require(upscaling.ResolveNeuralCombatPause().phase == Phase::Disabled, "Disabled NR must disable combat policy");

	// A peaceful paused menu may reuse the existing world image without combat fallback.
	{
		Upscaling peaceful;
		player.combat = false;
		ui.paused = false;
		nextFrame(peaceful);
		Require(peaceful.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted, "Peaceful setup must admit NR");
		++state.frameCount;
		ui.paused = true;
		PublishMainUpdate(peaceful);
		const auto menu = peaceful.BuildNeuralTemporalAdmission(Role::Submit, false, true);
		Require(menu.admitted && menu.retainedWorldFrame && !menu.combatPaused,
			"A peaceful paused menu must preserve retained-world NR continuity");
		ui.paused = false;
		nextFrame(peaceful, 10000);
		Require(peaceful.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
			"Closing a peaceful menu must not introduce a combat cooldown");
		ui.paused = true;
		player.combat = true;
		nextFrame(peaceful);
		Require(!peaceful.BuildNeuralTemporalAdmission(Role::Submit, false, true).admitted &&
					peaceful.GetNeuralCombatPauseSnapshot().phase == Phase::Combat,
			"A paused menu must still detect combat and preserve the pause");
		player.combat = false;
		nextFrame(peaceful, 10000);
		Require(!peaceful.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
			"Combat ending in a menu must wait for active gameplay");
		ui.paused = false;
		nextFrame(peaceful);
		Require(!peaceful.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
					peaceful.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
			"Leaving a combat menu must start the complete resume interval");
	}

	{
		Upscaling map;
		player.combat = false;
		ui.paused = false;
		ui.mapMenu = true;
		nextFrame(map);
		Require(map.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
			"A peaceful map must not create a combat pause when its pause flag is cleared");
		player.combat = true;
		nextFrame(map);
		Require(!map.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
			"Combat must pause NR while an unpaused map is open");
		player.combat = false;
		ui.mapMenu = false;
		nextFrame(map);
		Require(!map.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
					map.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
			"Leaving combat must start the peaceful interval");
		ui.mapMenu = true;
		nextFrame(map, 10000);
		Require(!map.BuildNeuralTemporalAdmission(Role::Submit, false, true).admitted &&
					map.GetNeuralCombatPauseSnapshot().phase == Phase::Unavailable,
			"A map with its pause flag cleared must interrupt a pending recovery");
		ui.mapMenu = false;
		nextFrame(map, 10000);
		Require(!map.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted &&
					map.GetNeuralCombatPauseSnapshot().remainingMs == 3000,
			"Time in the unpaused map must not satisfy recovery");
		nextFrame(map, 3000);
		Require(map.BuildNeuralTemporalAdmission(Role::Main, false, true).admitted,
			"A complete peaceful interval after closing the map must resume NR");
	}

	CheckUnavailableInputsAndConcurrentRoutes(state, ui, player);
	CheckPublishedSnapshots();
	CheckPublicationContinuity(state, ui, player);
	Require(offThreadCombatReads.load() == 0, "Only the main thread may query native combat state");

	CheckFrameContinuity();

	return 0;
}
