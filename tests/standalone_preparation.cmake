set(_standalone_preparation_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/standalone-preparation"
)
set(_standalone_preparation_headers)
foreach(
    _header
    IN
    ITEMS
        standalone_preparation_under_test.h
        standalone_compiling_under_test.h
        standalone_stop_under_test.h
        standalone_shutdown_under_test.h
)
    list(APPEND _standalone_preparation_headers
        "${_standalone_preparation_dir}/${_header}"
    )
endforeach()
add_custom_command(
    OUTPUT ${_standalone_preparation_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_standalone_preparation_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_standalone_preparation.cmake"
    DEPENDS
        src/ShaderCache.cpp tests/extract_standalone_preparation.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_d3d_shader_test(
    standalone_preparation_test StandalonePreparation
    tests/standalone_preparation_test.cpp
)
target_sources(standalone_preparation_test PRIVATE ${_standalone_preparation_headers})
target_include_directories(
    standalone_preparation_test
    PRIVATE "${_standalone_preparation_dir}" ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
)
target_link_libraries(
    standalone_preparation_test
    PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog windowsapp.lib
)
target_compile_definitions(standalone_preparation_test PRIVATE NOMINMAX)
