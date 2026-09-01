set(_shader_variants_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shader-cache-variants"
)
set(_shader_variants_headers)
foreach(
    _header
    IN
    ITEMS
        shader_task_declarations.h
        shader_task_comparison.h
        shader_capture_declarations.h
        shader_capture_state.h
        shader_task_identity_under_test.h
        shader_queue_add_under_test.h
        shader_capture_tracking_under_test.h
        shader_eviction_under_test.h
        shader_capture_under_test.h
)
    list(APPEND _shader_variants_headers "${_shader_variants_dir}/${_header}")
endforeach()
add_custom_command(
    OUTPUT ${_shader_variants_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_shader_variants_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_cache_variants.cmake"
    DEPENDS
        src/ShaderCache.h
        src/ShaderCache.cpp
        src/ShaderCacheSmartClear.cpp
        tests/extract_shader_cache_variants.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(
    shader_cache_variants_test
    ShaderCacheVariants
    tests/shader_cache_variants_test.cpp
)
target_sources(shader_cache_variants_test PRIVATE ${_shader_variants_headers})
target_include_directories(
    shader_cache_variants_test
    PRIVATE "${_shader_variants_dir}"
)
target_link_libraries(
    shader_cache_variants_test
    PRIVATE unordered_dense::unordered_dense
)
target_compile_definitions(
    shader_cache_variants_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
set_tests_properties(ShaderCacheVariants PROPERTIES TIMEOUT 30)
