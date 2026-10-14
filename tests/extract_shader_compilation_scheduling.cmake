if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _cache)
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(READ "${PROJECT_ROOT}/src/Utils/WinApi.cpp" _windows)
extract_between(
    "${_windows}" "\tint GetCooperativeBackgroundThreadPriority("
    "\n\tstd::optional<std::filesystem::path> GetPicturesPath("
    "shader_compilation_priority_under_test.h"
)
extract_between(
    "${_cache}" "\tvoid ShaderCache::ProcessCompilationSet("
    "\n\tShaderCompilationTask::ShaderCompilationTask("
    "shader_compilation_worker_under_test.h"
)
extract_between(
    "${_cache}" "\tstd::optional<ShaderCompilationTask> CompilationSet::WaitTake("
    "\n\tvoid CompilationSet::Complete("
    "shader_compilation_admission_under_test.h"
)
extract_between(
    "${_cache}" "\tbool ShaderCache::SetBackgroundCompilation("
    "\n\tstatic const std::filesystem::path& DiskCachePath("
    "shader_compilation_mode_under_test.h"
)
extract_between(
    "${_cache}" "\tbool ShaderCache::IsCompiling()"
    "\n\tvoid ShaderCache::StopCompilation()"
    "shader_compilation_phase_under_test.h"
)
extract_between(
    "${_header}" "struct TaskPriorityLess"
    "\nnamespace SIE" "shader_compilation_comparison_under_test.h"
)
