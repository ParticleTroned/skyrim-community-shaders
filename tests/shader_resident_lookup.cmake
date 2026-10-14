set(_shader_resident_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shader-resident-lookup")
set(_shader_resident_headers
    "${_shader_resident_dir}/shader_resident_lookup_under_test.h"
    "${_shader_resident_dir}/shader_resident_category.h"
    "${_shader_resident_dir}/shader_resident_release_under_test.h")
add_custom_command(OUTPUT ${_shader_resident_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_shader_resident_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_resident_lookup.cmake"
    DEPENDS src/ShaderCache.cpp src/ShaderCache.h src/Utils/ResidentShaderCache.h
        tests/extract_shader_resident_lookup.cmake tests/extract_source_region.cmake
    VERBATIM)
add_controller_test(shader_resident_lookup_test ShaderResidentLookup
    tests/shader_resident_lookup_test.cpp)
target_sources(shader_resident_lookup_test PRIVATE ${_shader_resident_headers})
target_include_directories(shader_resident_lookup_test PRIVATE "${_shader_resident_dir}")
add_controller_test(resident_shader_cache_test ResidentShaderCache
    tests/resident_shader_cache_test.cpp)
