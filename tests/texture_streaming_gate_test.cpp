#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
namespace globals::game
{
	bool isVR = false;
}
struct Upscaling
{
#include "texture_streaming_states_under_test.h"
	std::atomic_bool postLoadRuntimeResetPending = false, pendingPerfModeRenderTargetRecreate = false,
					 perfModeRenderTargetRecreateInProgress = false, vrRenderScaleMemoryTrimPending = false;
	std::atomic_uint32_t deferredVRRenderScalePostLoadRecoveryEpoch = 0;
	std::atomic<VRRenderScaleTransitionState> vrRenderScaleTransitionState{ VRRenderScaleTransitionState::Idle };
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
			upscaling.vrRenderScaleTransitionState = state;
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
