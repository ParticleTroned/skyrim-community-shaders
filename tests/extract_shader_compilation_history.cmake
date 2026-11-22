if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Utils/FileSystem.cpp" _filesystem)
extract_between("${_source}" "\t\tconstexpr size_t kMaximumCompileTimeHistoryBytes"
    "\n\t\tUtil::CompileDedupe::Registry& GetCompileDedupe()" "compile_history_constants.h")
extract_between("${_header}" "\t\t/** Flushes pending timing updates"
    "\n\t\tint64_t estimateBatchStartQpc" "compile_history_state.h")
extract_between("${_source}" "\tvoid CompilationSet::LoadEstimateHistory()"
    "\n\tShaderCompilationEstimate::Snapshot CompilationSet::GetEstimate()" "compile_history_persistence.h")
extract_between("${_source}" "\tstd::string CompilationSet::GetStatsString("
    "\n\tUpdateListener::UpdateListener(" "compile_history_display.h")
extract_between("${_source}" "\tvoid CompilationSet::Complete("
    "\n\tvoid CompilationSet::NotifyIfComplete()" "compile_history_completion.h")
extract_between("${_source}" "\tShaderCompilationEstimate::Snapshot CompilationSet::GetEstimate()"
    "\n\tstd::string CompilationSet::GetStatsString(" "compile_history_estimate.h")
extract_between("${_filesystem}" "\t\tbool WriteTextFileAtomic("
    "\n\t\tstd::string SanitizeFileName(" "compile_history_atomic.h")
