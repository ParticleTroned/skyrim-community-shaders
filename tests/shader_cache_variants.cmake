set(_shader_variants_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shader-cache-variants"
)
set(_shader_variants_headers)
foreach(
    _header
    IN
    ITEMS
        shader_publication_under_test.h
        shader_retirement_under_test.h
        shader_full_clear_under_test.h
        shader_type_clear_under_test.h
        shader_clear_metadata_under_test.h
        shader_result_traits_under_test.h
        shader_add_completed_under_test.h
        shader_release_under_test.h
        shader_feature_clear_under_test.h
        shader_generation_check.h
        shader_bytecode_map.h
        shader_bytecode_hooks.h
        shader_category_flag.h
        shader_category_prewarm.h
        shader_category_demand.h
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
        src/Hooks.cpp
        src/ShaderCacheSmartClear.cpp
        src/Features/Upscaling.cpp
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

set(_category_fallback_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/category-fallback")
set(_category_fallback_headers)
foreach(_header IN ITEMS pixel_lookup.h category_flag.h pixel_binding.h begin_technique.h
    diagnostic_increment.h preparation_failure.h preparation_failure_type.h grass_material_admission.h
    lighting_flags.h pixel_prewarm.h pbr_prewarm.h)
    list(APPEND _category_fallback_headers "${_category_fallback_dir}/${_header}")
endforeach()
add_custom_command(OUTPUT ${_category_fallback_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_category_fallback_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_category_fallback.cmake"
    DEPENDS src/ShaderCache.cpp src/ShaderCache.h src/Hooks.cpp src/TruePBR.cpp
        src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp
        src/Features/Upscaling/NeuralRendering/CharacterRendering.h
        tests/extract_shader_category_fallback.cmake tests/extract_source_region.cmake
    VERBATIM)
foreach(_bridge IN ITEMS off on)
    add_controller_test(shader_category_fallback_${_bridge}_test ShaderCategoryFallback_${_bridge}
        tests/shader_category_fallback_test.cpp)
    target_sources(shader_category_fallback_${_bridge}_test PRIVATE ${_category_fallback_headers})
    target_include_directories(shader_category_fallback_${_bridge}_test PRIVATE "${_category_fallback_dir}")
    target_link_libraries(shader_category_fallback_${_bridge}_test PRIVATE Microsoft::CppWinRT)
endforeach()
target_compile_definitions(shader_category_fallback_on_test PRIVATE DEVBENCH_BRIDGE_ENABLED)

add_controller_test(
    shader_publication_test ShaderPublication tests/shader_publication_test.cpp
)
target_sources(shader_publication_test PRIVATE ${_shader_variants_headers})
target_include_directories(shader_publication_test PRIVATE "${_shader_variants_dir}")
target_link_libraries(shader_publication_test PRIVATE
    unordered_dense::unordered_dense magic_enum::magic_enum Microsoft::CppWinRT)
target_compile_definitions(shader_publication_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_tests_properties(ShaderPublication PROPERTIES TIMEOUT 30)
