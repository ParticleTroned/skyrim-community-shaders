#pragma once

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyD3D11.hpp>

#include <optional>
#include <string_view>

/**
 * @brief Fans one render-pass name out to the internal profiler, Tracy CPU
 * and GPU zones, and RenderDoc/PIX annotations.
 */
struct ScopedGpuPass
{
	/** @brief Opens a pass with a dynamic Tracy source location. */
	explicit ScopedGpuPass(std::string_view a_name);
#ifdef TRACY_ENABLE
	/** @brief Opens a pass with an allocation-free static Tracy source location. */
	ScopedGpuPass(const tracy::SourceLocationData* a_sourceLocation, std::string_view a_name);
#endif
	~ScopedGpuPass();

	ScopedGpuPass(const ScopedGpuPass&) = delete;
	ScopedGpuPass& operator=(const ScopedGpuPass&) = delete;
	ScopedGpuPass(ScopedGpuPass&&) = delete;
	ScopedGpuPass& operator=(ScopedGpuPass&&) = delete;

private:
#ifdef TRACY_ENABLE
	std::optional<tracy::ScopedZone> cpuZone;
	std::optional<tracy::D3D11ZoneScope> gpuZone;
#endif
	bool annotationOpen = false;
	bool profilerActive = false;
};

#define CS_GPU_PASS_CONCAT_IMPL(a, b) a##b
#define CS_GPU_PASS_CONCAT(a, b) CS_GPU_PASS_CONCAT_IMPL(a, b)

#ifdef TRACY_ENABLE
#	define CS_GPU_PASS(name)                                                                                                                                \
		static constexpr tracy::SourceLocationData CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__){ name, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0 }; \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { &CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__), name }

#	define CS_GPU_PASS_SELECT(condition, firstName, secondName)                                                                                                                         \
		static constexpr tracy::SourceLocationData CS_GPU_PASS_CONCAT(cs_gpu_pass_source_first_, __LINE__){ firstName, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0 };                  \
		static constexpr tracy::SourceLocationData CS_GPU_PASS_CONCAT(cs_gpu_pass_source_second_, __LINE__){ secondName, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0 };                \
		const bool CS_GPU_PASS_CONCAT(cs_gpu_pass_condition_, __LINE__) = (condition);                                                                                                   \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__)                                                                                                                         \
		{                                                                                                                                                                                \
			CS_GPU_PASS_CONCAT(cs_gpu_pass_condition_, __LINE__) ? &CS_GPU_PASS_CONCAT(cs_gpu_pass_source_first_, __LINE__) : &CS_GPU_PASS_CONCAT(cs_gpu_pass_source_second_, __LINE__), \
				CS_GPU_PASS_CONCAT(cs_gpu_pass_condition_, __LINE__) ? std::string_view(firstName) : std::string_view(secondName)                                                        \
		}
#else
#	define CS_GPU_PASS(name) \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { name }

#	define CS_GPU_PASS_SELECT(condition, firstName, secondName) \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { (condition) ? std::string_view(firstName) : std::string_view(secondName) }
#endif

/** @brief Instruments a pass whose name is generated at runtime. */
#define CS_GPU_PASS_DYNAMIC(name) \
	ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { name }
