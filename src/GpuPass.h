#pragma once

#include "Utils/PassTimingCapture.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/EngineStutterMonitor.h"
#endif

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyD3D11.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

/**
 * @brief Fans one render-pass name out to the internal profiler, Tracy CPU
 * and GPU zones, and RenderDoc/PIX annotations.
 */
struct ScopedGpuPass
{
	/** @brief Opens a pass with a dynamic Tracy source location. */
	explicit ScopedGpuPass(std::string_view a_name, const Util::PassTimingHandle& a_capture = {}, bool a_detailOnly = false);
#ifdef TRACY_ENABLE
	/** @brief Opens a pass with an allocation-free static Tracy source location. */
	ScopedGpuPass(const tracy::SourceLocationData* a_sourceLocation, std::string_view a_name, const Util::PassTimingHandle& a_capture = {}, bool a_detailOnly = false);
#endif
	~ScopedGpuPass();

	ScopedGpuPass(const ScopedGpuPass&) = delete;
	ScopedGpuPass& operator=(const ScopedGpuPass&) = delete;
	ScopedGpuPass(ScopedGpuPass&&) = delete;
	ScopedGpuPass& operator=(ScopedGpuPass&&) = delete;

private:
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::optional<CSX::Diagnostics::Stutters::Scope> stutterScope;
	uint64_t crashDiagnosticPass = 0;
#endif
#ifdef TRACY_ENABLE
	std::optional<tracy::ScopedZone> cpuZone;
	std::optional<tracy::D3D11ZoneScope> gpuZone;
#endif
	bool annotationOpen = false;
	bool profilerActive = false;
	bool detailOnly = false;
};

#define CS_GPU_PASS_CONCAT_IMPL(a, b) a##b
#define CS_GPU_PASS_CONCAT(a, b) CS_GPU_PASS_CONCAT_IMPL(a, b)

namespace GpuPassDetail
{
	/** @brief Selects a fixed pass entry, clamping out-of-range indices to the last entry. */
	template <class T, std::size_t N>
	constexpr const T& SelectPass(const std::array<T, N>& a_passes, std::size_t a_index)
	{
		static_assert(N > 0);
		return a_passes[a_index < N ? a_index : N - 1];
	}

#ifdef TRACY_ENABLE
	/** @brief Builds one static Tracy source location per literal pass name. */
	template <class... Names>
	constexpr auto MakeSourceLocations(const char* a_function, const char* a_file, std::uint32_t a_line, Names... a_names)
	{
		static_assert(sizeof...(Names) > 0);
		return std::array{ tracy::SourceLocationData{ a_names, a_function, a_file, a_line, 0 }... };
	}
#endif
}

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

/** @brief Instruments a fixed name set without formatting or dynamic Tracy source locations. */
#ifdef TRACY_ENABLE
#	define CS_GPU_PASS_INDEXED(index, ...)                                                                                                                                                       \
		static constexpr auto CS_GPU_PASS_CONCAT(cs_gpu_pass_sources_, __LINE__) = GpuPassDetail::MakeSourceLocations(__FUNCTION__, __FILE__, static_cast<std::uint32_t>(__LINE__), __VA_ARGS__); \
		const auto& CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__) = GpuPassDetail::SelectPass(CS_GPU_PASS_CONCAT(cs_gpu_pass_sources_, __LINE__), static_cast<std::size_t>(index));           \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { &CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__), CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__).name }
#else
#	define CS_GPU_PASS_INDEXED(index, ...)                                                          \
		static constexpr std::array CS_GPU_PASS_CONCAT(cs_gpu_pass_names_, __LINE__){ __VA_ARGS__ }; \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { GpuPassDetail::SelectPass(CS_GPU_PASS_CONCAT(cs_gpu_pass_names_, __LINE__), static_cast<std::size_t>(index)) }
#endif

#ifdef TRACY_ENABLE
#	define CS_GPU_CAPTURE_IMPL(name, handle, detail)                                                                                                        \
		static constexpr tracy::SourceLocationData CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__){ name, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0 }; \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { &CS_GPU_PASS_CONCAT(cs_gpu_pass_source_, __LINE__), name, handle, detail }
#else
#	define CS_GPU_CAPTURE_IMPL(name, handle, detail) \
		ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { name, handle, detail }
#endif

/** @brief Retains one existing pass's timing while preserving its legacy aggregate accounting. */
#define CS_GPU_PASS_CAPTURE(name, handle) CS_GPU_CAPTURE_IMPL(name, handle, false)
/** @brief Retains optional detail timing independently from legacy aggregate accounting. */
#define CS_GPU_DETAIL_PASS(name, handle) CS_GPU_CAPTURE_IMPL(name, handle, true)

/** @brief Instruments a pass whose name is generated at runtime. */
#define CS_GPU_PASS_DYNAMIC(name) \
	ScopedGpuPass CS_GPU_PASS_CONCAT(cs_gpu_pass_, __LINE__) { name }
