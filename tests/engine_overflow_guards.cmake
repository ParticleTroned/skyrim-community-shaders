set(_overflow_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/engine-overflow-guards"
)
set(_overflow_test_headers
    "${_overflow_test_dir}/engine_fix_owner_under_test.h"
    "${_overflow_test_dir}/cull_pool_under_test.h"
)
add_custom_command(
    OUTPUT ${_overflow_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_overflow_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_engine_overflow_guards.cmake"
    DEPENDS
        src/EngineFix.cpp
        src/EngineFixes/CullPoolExhaustionFix.cpp
        tests/extract_engine_overflow_guards.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(
    engine_overflow_guards_test
    EngineOverflowGuards
    tests/engine_overflow_guards_test.cpp
)
target_sources(engine_overflow_guards_test PRIVATE ${_overflow_test_headers})
target_include_directories(
    engine_overflow_guards_test
    PRIVATE "${_overflow_test_dir}"
)
target_compile_definitions(
    engine_overflow_guards_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
