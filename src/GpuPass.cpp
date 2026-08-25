#include "GpuPass.h"

#include "Globals.h"
#include "Profiler.h"
#include "State.h"

#ifdef TRACY_ENABLE
ScopedGpuPass::ScopedGpuPass(const tracy::SourceLocationData* a_sourceLocation, std::string_view a_name)
{
	auto* profiler = globals::profiler;
	auto* state = globals::state;

	if (profiler)
		profilerActive = profiler->BeginPass(a_name, false);

	cpuZone.emplace(a_sourceLocation, -1, true);

	if (state && state->tracyCtx)
		gpuZone.emplace(state->tracyCtx, a_sourceLocation, true);

	if (state && state->frameAnnotations) {
		state->BeginDrawEvent(a_name);
		annotationOpen = true;
	}
}
#endif

ScopedGpuPass::ScopedGpuPass(std::string_view a_name)
{
	auto* profiler = globals::profiler;
	auto* state = globals::state;

	if (profiler)
		profilerActive = profiler->BeginPass(a_name, false);

#ifdef TRACY_ENABLE
	cpuZone.emplace(
		uint32_t(0),
		"GpuPass", sizeof("GpuPass") - 1,
		"ScopedGpuPass", sizeof("ScopedGpuPass") - 1,
		a_name.data(), a_name.size(),
		uint32_t(0), -1, true);

	if (state && state->tracyCtx) {
		gpuZone.emplace(state->tracyCtx,
			uint32_t(0),
			"GpuPass", sizeof("GpuPass") - 1,
			"ScopedGpuPass", sizeof("ScopedGpuPass") - 1,
			a_name.data(), a_name.size(),
			0, true);
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
	cpuZone.reset();
#endif

	if (profilerActive && globals::profiler)
		globals::profiler->EndPass(false);
}
