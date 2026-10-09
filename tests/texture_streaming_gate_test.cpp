#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
namespace globals::game
{
	bool isVR = false;
}
namespace Util
{
	struct GpuMemoryBudget
	{
		enum class Owner
		{
			RenderScale
		};
		bool priority = false;
		static GpuMemoryBudget& Get()
		{
			static GpuMemoryBudget value;
			return value;
		}
		void SetPriorityWork(Owner, bool value) { priority = value; }
	};
}
struct Upscaling
{
#include "texture_streaming_states_under_test.h"
	std::atomic_bool postLoadRuntimeResetPending = false, pendingPerfModeRenderTargetRecreate = false,
					 perfModeRenderTargetRecreateInProgress = false, vrRenderScaleMemoryTrimPending = false;
	std::atomic_uint32_t deferredVRRenderScalePostLoadRecoveryEpoch = 0;
	std::atomic<VRRenderScaleTransitionState> vrRenderScaleTransitionState{ VRRenderScaleTransitionState::Idle };
	struct
	{
		VRRenderScaleTransitionState state = VRRenderScaleTransitionState::Idle;
	} vrRenderScaleTransitionController;
	void StoreVRRenderScaleTransitionStateLocked(VRRenderScaleTransitionState) noexcept;
	bool nrTransition = false;
	bool IsNeuralRenderingInsertionTransitionBlocked() const noexcept { return nrTransition; }
	bool IsTextureStreamingTransitionActive() const noexcept;
};
#include "texture_streaming_gate_under_test.h"
int main()
{
	Upscaling upscaling;
	using State = Upscaling::VRRenderScaleTransitionState;
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		for (auto state : { State::Idle, State::Requested, State::WaitingForSafePoint, State::Preparing, State::Applying, State::Stabilizing, State::Active }) {
			upscaling.StoreVRRenderScaleTransitionStateLocked(state);
			if (Util::GpuMemoryBudget::Get().priority != (state != State::Idle && state != State::Active) ||
				upscaling.vrRenderScaleTransitionController.state != state || upscaling.vrRenderScaleTransitionState != state) {
				std::cerr << "Steady render-scale state blocked streaming restoration\n";
				return 1;
			}
			for (unsigned mask = 0; mask < 64; ++mask) {
				upscaling.postLoadRuntimeResetPending = mask & 1;
				upscaling.pendingPerfModeRenderTargetRecreate = mask & 2;
				upscaling.perfModeRenderTargetRecreateInProgress = mask & 4;
				upscaling.nrTransition = mask & 8;
				upscaling.vrRenderScaleMemoryTrimPending = mask & 16;
				upscaling.deferredVRRenderScalePostLoadRecoveryEpoch = mask & 32;
				const bool expected = (mask & 15) || (vr && ((mask & 48) || (state != State::Idle && state != State::Active)));
				if (upscaling.IsTextureStreamingTransitionActive() != expected) {
					std::cerr << "Incorrect streaming gate for runtime=" << vr << " state=" << static_cast<unsigned>(state) << " blockers=" << mask << '\n';
					return 1;
				}
			}
		}
	}
	std::cout << "PASS: 896 settled-state and transition guard combinations across VR and flat runtimes\n";
}
