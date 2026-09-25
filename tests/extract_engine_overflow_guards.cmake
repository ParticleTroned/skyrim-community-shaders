if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/EngineFix.cpp" _registry)
extract_between(
    "${_registry}"
    "bool EngineFix::IsInstalledByEngineFixes("
    "void EngineFix::InstallOnPostPostLoadFixes("
    "engine_fix_owner_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/EngineFixes/CullPoolExhaustionFix.cpp" _source)
string(FIND "${_source}" "namespace\n{" _start)
if(_start EQUAL -1)
    message(FATAL_ERROR "Culling pool test cannot find implementation")
endif()
string(SUBSTRING "${_source}" ${_start} -1 _implementation)
file(WRITE "${OUTPUT_DIRECTORY}/cull_pool_under_test.h" "${_implementation}")
