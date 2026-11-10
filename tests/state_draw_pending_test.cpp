#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

struct Upscaling
{
	std::atomic_bool pendingPerfModeRenderTargetRecreate{ false };
	std::atomic_uint64_t vrRenderScaleRelatchDrainEpoch{ 0 };
	unsigned calls = 0;
	bool fail = false;
#include "state_draw_gate_under_test.h"
	void ApplyPendingPerfModeRenderTargetRecreate(const char*)
	{
		++calls;
		if (fail)
			throw std::runtime_error("retry");
	}
};
namespace globals
{
	namespace game
	{
		bool isVR = false;
	}
	namespace features
	{
		Upscaling upscaling;
	}
}
enum class StateDrawPhase
{
	PerfModeRelatch
};
struct ScopedStateDrawPhaseTimer
{
	ScopedStateDrawPhaseTimer(StateDrawPhase, uint32_t, bool) {}
};
struct ShaderCache
{
	unsigned calls = 0;
	void ServicePendingDisable() { ++calls; }
};
void Draw(ShaderCache* shaderCache)
{
	const uint32_t stateDrawDiagFrame = 1;
	const bool stateDrawDiagActive = false;
#include "state_draw_pending_under_test.h"
}
void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main()
{
	try {
		auto& upscaling = globals::features::upscaling;
		ShaderCache cache;
		upscaling.pendingPerfModeRenderTargetRecreate = true;
		upscaling.vrRenderScaleRelatchDrainEpoch = 1;
		Draw(&cache);
		Require(upscaling.calls == 0 && cache.calls == 1, "Flat runtime entered VR target work");
		globals::game::isVR = true;
		upscaling.pendingPerfModeRenderTargetRecreate = false;
		upscaling.vrRenderScaleRelatchDrainEpoch = 0;
		for (unsigned i = 0; i < 10000; ++i) Draw(&cache);
		Require(upscaling.calls == 0 && cache.calls == 10001, "Idle draws entered target work or suppressed pending shader disable");
		upscaling.pendingPerfModeRenderTargetRecreate.store(true, std::memory_order_release);
		Draw(&cache);
		Require(upscaling.calls == 1, "Same-frame request was deferred by draw reuse");
		upscaling.pendingPerfModeRenderTargetRecreate = false;
		upscaling.vrRenderScaleRelatchDrainEpoch = 42;
		Draw(&cache);
		Require(upscaling.calls == 2, "Cancelled relatch stranded drain cleanup");
		upscaling.vrRenderScaleRelatchDrainEpoch = 0;
		Draw(&cache);
		Require(upscaling.calls == 2, "Completed cleanup kept entering target work");
		upscaling.pendingPerfModeRenderTargetRecreate = true;
		upscaling.fail = true;
		try {
			Draw(&cache);
		} catch (const std::runtime_error&) {}
		upscaling.fail = false;
		Draw(&cache);
		Require(upscaling.calls == 4 && upscaling.pendingPerfModeRenderTargetRecreate, "Failed work suppressed a same-frame retry or consumed ownership");
		std::cout << "PASS: production per-draw gate, same-frame requests, drain cleanup, retry and shader-disable service\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
