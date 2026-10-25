#include "Features/Upscaling/NeuralRendering/CombatPausePolicy.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>

namespace RE
{
	struct UI
	{
		bool paused = false;
		bool GameIsPaused() const { return paused; }
	};
	struct PlayerCharacter
	{
		static inline PlayerCharacter* instance = nullptr;
		bool combat = false;
		unsigned samples = 0;
		static PlayerCharacter* GetSingleton() { return instance; }
		bool IsInCombat()
		{
			++samples;
			return combat;
		}
	};
}
struct State
{
	std::uint32_t frameCount = 1, lastWorldRenderFrame = 1, lastCompletedWorldRenderFrame = 1;
	bool loading = false;
	bool IsMainOrLoadingMenuOpen(RE::UI*) const { return loading; }
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
	std::atomic<std::uint32_t> neuralTemporalAdmissionLatch{ 0 };
	unsigned resets = 0;
	bool IsNeuralRenderingEnabled() const { return settings.neuralRenderingEnabled; }
	bool IsNeuralRenderingRequested() const { return IsNeuralRenderingEnabled(); }
	void RequestHistoryReset() { ++resets; }
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
	const auto nextFrame = [&](std::uint64_t elapsed = 1) {
		++state.frameCount;
		state.lastWorldRenderFrame = state.lastCompletedWorldRenderFrame = state.frameCount;
		nowMs += elapsed;
	};
	const auto admit = [&](Role role) {
		const auto result = upscaling.BuildNeuralTemporalAdmission(role, false, true);
		upscaling.ObserveNeuralTemporalAdmission(role, result);
		return result;
	};
	Require(!upscaling.GetNeuralCombatPauseSnapshot().sampled, "Status must not sample gameplay");
	Require(admit(Role::Main).admitted, "Initial peaceful gameplay must remain admitted");
	player.combat = true;
	Require(admit(Role::Submit).admitted && player.samples == 1, "Both eyes/routes must share one immutable frame sample");
	nextFrame();
	const auto combat = admit(Role::Main);
	Require(!combat.admitted && combat.combatPaused, "Combat must block NR admission");
	Require(Upscaling::GetNeuralTemporalFallbackReason(combat) == Upscaling::NeuralStereoFallbackReason::CombatPause, "Combat must publish its fallback reason");
	Require(std::string_view(NeuralRendering::GetTemporalAdmissionBlockReasonName(combat.blockReason)) == "combat_pause", "Combat telemetry reason must be named");
	Require(!admit(Role::Submit).admitted && upscaling.resets == 2, "Entering combat must invalidate both route histories once");
	Require(!admit(Role::Submit).admitted && upscaling.resets == 2, "Repeated eyes must not repeat history invalidation");
	player.combat = false;
	nextFrame();
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().remainingMs == 3000, "Resume interval starts at first peaceful sample");
	nextFrame(2999);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().remainingMs == 1, "Do not resume before the full interval");
	player.combat = true;
	nextFrame();
	Require(!admit(Role::Main).admitted, "Renewed combat must interrupt cooldown");
	player.combat = false;
	nextFrame();
	Require(!admit(Role::Main).admitted, "Renewed combat must restart the full cooldown");
	nextFrame(3000);
	Require(admit(Role::Main).admitted && admit(Role::Submit).admitted && upscaling.resets == 4, "Resume must reset both histories before evaluation");

	player.combat = true;
	nextFrame();
	Require(!admit(Role::Main).admitted, "Combat re-entry must pause again");
	player.combat = false;
	nextFrame();
	Require(!admit(Role::Main).admitted, "Cooldown must remain blocked");
	ui.paused = true;
	nextFrame(1000);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().phase == Phase::Unavailable, "Paused menus must interrupt the cooldown even with retained-world continuity");
	ui.paused = false;
	nextFrame(10000);
	Require(!admit(Role::Main).admitted && upscaling.GetNeuralCombatPauseSnapshot().remainingMs == 3000, "Time in paused menus must not satisfy cooldown");
	state.loading = true;
	nextFrame();
	Require(!admit(Role::Main).admitted, "Loading must block combat admission");
	state.loading = false;
	RE::PlayerCharacter::instance = nullptr;
	nextFrame();
	Require(!admit(Role::Submit).admitted && upscaling.GetNeuralCombatPauseSnapshot().phase == Phase::Unavailable, "Missing player must fail closed");
	RE::PlayerCharacter::instance = &player;
	nextFrame();
	Require(!admit(Role::Main).admitted, "Player recovery must start a new interval");
	nextFrame(3000);
	Require(admit(Role::Submit).admitted, "Player recovery must eventually resume");

	upscaling.settings.neuralRenderingPauseInCombat = false;
	player.combat = true;
	nextFrame();
	const auto samples = player.samples;
	Require(admit(Role::Main).admitted && player.samples == samples, "Disabled option must not read player combat state");
	upscaling.settings.neuralRenderingPauseInCombat = true;
	Require(admit(Role::Submit).admitted, "A setting change must not split the current stereo frame");
	nextFrame();
	Require(!admit(Role::Main).admitted, "Enabling combat pause applies on the next frame");
	upscaling.settings.neuralRenderingEnabled = false;
	nextFrame();
	Require(upscaling.ResolveNeuralCombatPause().phase == Phase::Disabled, "Disabled NR must disable combat policy");

	NeuralRendering::CombatPausePolicy policy;
	Require(policy.Update(1, true, 100, true).IsPaused(), "Policy combat setup failed");
	Require(policy.Update(2, true, 101, false).remainingMs == 3000, "Policy cooldown setup failed");
	Require(policy.Update(4, true, 10000, false).remainingMs == 3000, "Unsampled frames must interrupt the safe interval");
	Require(policy.Update(5, true, 5, false).remainingMs == 3000, "Clock rollback must restart the interval without underflow");
	Require(policy.Update(1, true, 10005, false).remainingMs == 3000, "Frame reset must restart the interval");
	Require(!policy.Update(2, false, 10006, std::nullopt).IsPaused(), "Disabling must always release the pause on a new frame");
	return 0;
}
