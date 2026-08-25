#pragma once

#include <Tracy/TracyC.h>
#include <Tracy/TracyD3D11.hpp>

#include <optional>
#include <string_view>

/**
 * @brief Fans one render-pass name out to the internal profiler, Tracy CPU
 * and GPU zones, and RenderDoc/PIX annotations.
 */
struct ScopedGpuPass
{
	explicit ScopedGpuPass(std::string_view a_name);
	~ScopedGpuPass();

	ScopedGpuPass(const ScopedGpuPass&) = delete;
	ScopedGpuPass& operator=(const ScopedGpuPass&) = delete;
	ScopedGpuPass(ScopedGpuPass&&) = delete;
	ScopedGpuPass& operator=(ScopedGpuPass&&) = delete;

private:
#ifdef TRACY_ENABLE
	TracyCZoneCtx cpuZoneCtx{};
	std::optional<tracy::D3D11ZoneScope> gpuZone;
#endif
	bool annotationOpen = false;
	bool profilerActive = false;
};

#define CS_GPU_PASS_CONCAT_IMPL(a, b) a##b
#define CS_GPU_PASS_CONCAT(a, b) CS_GPU_PASS_CONCAT_IMPL(a, b)

/** @brief Instruments one render pass across every supported timing sink. */
#define CS_GPU_PASS(name) \
	ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { name }
