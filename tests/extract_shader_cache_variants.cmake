if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
file(READ "${PROJECT_ROOT}/src/ShaderCacheSmartClear.cpp" _capture)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
extract_between(
    "${_header}" "\tenum class ShaderClass" "\n}"
    "shader_task_declarations.h"
)
extract_between(
    "${_header}" "struct TaskPriorityLess" "\nnamespace SIE"
    "shader_task_comparison.h"
)
extract_between(
    "${_header}" "\t\tstruct ActiveShaderInfo" "\n\t\tHANDLE managementThread"
    "shader_capture_declarations.h"
)
extract_between(
    "${_header}" "\t\tstd::atomic<uint32_t> activeShaderCaptureFramesRemaining"
    "\n\t\tstruct hlslRecord" "shader_capture_state.h"
)
extract_between(
    "${_source}" "\tsize_t ShaderCompilationTask::GetId() const"
    "\n\tstd::string ShaderCompilationTask::GetString() const"
    "shader_task_identity_under_test.h"
)
extract_between(
    "${_source}" "\tvoid CompilationSet::Add("
    "\n\tvoid CompilationSet::Complete(" "shader_queue_add_under_test.h"
)
extract_between(
    "${_source}" "\tvoid ShaderCache::TrackActiveShader("
    "\n\tvoid ShaderCache::ResetFrameShaderTracking("
    "shader_capture_tracking_under_test.h"
)
extract_between(
    "${_source}" "\ttemplate <typename ShaderType, typename MutexType>"
    "\n\tvoid ShaderCache::DeleteScopedDiskCacheEntries("
    "shader_eviction_under_test.h"
)
extract_between(
    "${_capture}" "\tbool ShaderCache::IsTrackingActiveShaders() const"
    "\n}" "shader_capture_under_test.h"
)

extract_between(
    "${_header}" "\t\tstatic constexpr uint32_t GetCharacterCategoryFlag("
    "\n\t\tRE::BSGraphics::PixelShader* GetPixelShader("
    "shader_category_flag.h"
)
extract_between(
    "${_source}" "\tvoid ShaderCache::PrewarmDeferredPixelShaders("
    "\n\tRE::BSGraphics::PixelShader* ShaderCache::GetPixelShader("
    "shader_category_prewarm.h"
)
extract_between(
    "${_upscaling}" "bool Upscaling::NeedsCharacterCategoryShaders() const"
    "\nbool Upscaling::IsCharacterNeuralRenderingRouteConfigured() const"
    "shader_category_demand.h"
)

extract_between(
    "${_source}" "\ttemplate <class EngineShader, class DeviceShader, class Map, class Mutex, class IsCurrent>"
    "\n\tstd::string ShaderCache::GetDefinesString("
    "shader_publication_under_test.h"
)

file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
extract_between(
    "${_hooks}" "std::unordered_map<void*, std::pair<std::unique_ptr<uint8_t[]>, size_t>> ShaderBytecodeMap;"
    "\nnamespace" "shader_bytecode_map.h"
)
extract_between(
    "${_hooks}" "void RegisterShaderBytecode(" "\nnamespace"
    "shader_bytecode_hooks.h"
)

extract_between(
    "${_header}" "\t\tbool IsTaskStale(" "\n\t\tvoid StartActiveShaderCaptureWindow("
    "shader_generation_check.h"
)
