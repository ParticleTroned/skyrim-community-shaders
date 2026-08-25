#include "GpuPass.h"

#include "Globals.h"
#include "Profiler.h"
#include "State.h"

ScopedGpuPass::ScopedGpuPass(std::string_view a_name)
{
	auto* profiler = globals::profiler;
	auto* state = globals::state;

	if (profiler)
		profilerActive = profiler->BeginPass(a_name, false);

#ifdef TRACY_ENABLE
	const auto cpuSourceLocation = ___tracy_alloc_srcloc_name(
		0,
		"GpuPass", sizeof("GpuPass") - 1,
		"ScopedGpuPass", sizeof("ScopedGpuPass") - 1,
		a_name.data(), a_name.size(),
		0);
	cpuZoneCtx = ___tracy_emit_zone_begin_alloc(cpuSourceLocation, true);

	if (state && state->tracyCtx) {
		const auto gpuSourceLocation = ___tracy_alloc_srcloc_name(
			0,
			"GpuPass", sizeof("GpuPass") - 1,
			"ScopedGpuPass", sizeof("ScopedGpuPass") - 1,
			a_name.data(), a_name.size(),
			0);
		gpuZone.emplace(state->tracyCtx, gpuSourceLocation, 0, true);
	}
#endif

	if (state && state->frameAnnotations) {
		state->BeginDrawEvent(a_name);
		annotationOpen = true;
	}
}

ScopedGpuPass::~ScopedGpuPass()
{
	auto* state = globals::state;

	if (annotationOpen && state)
		state->EndDrawEvent();

#ifdef TRACY_ENABLE
	gpuZone.reset();
	TracyCZoneEnd(cpuZoneCtx);
#endif

	if (profilerActive && globals::profiler)
		globals::profiler->EndPass(false);
}
