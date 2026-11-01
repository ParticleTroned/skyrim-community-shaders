add_controller_test(compile_dedupe_test CompileDedupe tests/compile_dedupe_test.cpp)
target_link_libraries(compile_dedupe_test PRIVATE xxHash::xxhash)
target_compile_definitions(compile_dedupe_test PRIVATE CSX_COMPILE_DEDUPE_TESTING)
set_tests_properties(CompileDedupe PROPERTIES TIMEOUT 30)

set(_shared_compile_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shared-compile")
set(_shared_compile_headers)
foreach(_header IN ITEMS shared_compile_includes.h shared_compile_snapshot.h shared_compile_counter.h shared_compile_generation.h)
    list(APPEND _shared_compile_headers "${_shared_compile_dir}/${_header}")
endforeach()
add_custom_command(
    OUTPUT ${_shared_compile_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_shared_compile_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shared_shader_compile.cmake"
    DEPENDS src/ShaderCache.cpp src/ShaderCache.h tests/extract_shared_shader_compile.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(shared_shader_compile_test SharedShaderCompile tests/shared_shader_compile_test.cpp)
target_sources(shared_shader_compile_test PRIVATE ${_shared_compile_headers} src/Utils/ShaderInclude.cpp)
target_include_directories(shared_shader_compile_test PRIVATE "${_shared_compile_dir}")
target_link_libraries(shared_shader_compile_test PRIVATE xxHash::xxhash spdlog::spdlog d3dcompiler.lib)
target_compile_definitions(shared_shader_compile_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_tests_properties(SharedShaderCompile PROPERTIES TIMEOUT 30)
foreach(_target IN ITEMS compile_dedupe_test shared_shader_compile_test)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /UNDEBUG /utf-8)
    else()
        target_compile_options(${_target} PRIVATE -UNDEBUG)
    endif()
endforeach()
