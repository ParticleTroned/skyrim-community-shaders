set(_standalone_cache_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/standalone-cache"
)
set(_standalone_cache_headers)
foreach(
    _header
    IN
    ITEMS
        standalone_source_digest.h
        standalone_file_direct.h
        standalone_file_atomic.h
        standalone_compile_key.h
        standalone_cache_under_test.h
        standalone_d3d_under_test.h
        standalone_failures_under_test.h
)
    list(APPEND _standalone_cache_headers "${_standalone_cache_dir}/${_header}")
endforeach()
add_custom_command(
    OUTPUT ${_standalone_cache_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_standalone_cache_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_standalone_shader_cache.cmake"
    DEPENDS
        src/ShaderCache.cpp
        src/Utils/D3D.cpp
        src/Utils/FileSystem.cpp
        tests/extract_standalone_shader_cache.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_d3d_shader_test(standalone_shader_cache_test StandaloneShaderCache tests/standalone_shader_cache_test.cpp)
target_sources(
    standalone_shader_cache_test
    PRIVATE
        ${_standalone_cache_headers}
        src/Utils/ShaderInclude.cpp
        src/Utils/CryptoHash.cpp
)
target_include_directories(
    standalone_shader_cache_test
    PRIVATE "${_standalone_cache_dir}"
)
target_link_libraries(
    standalone_shader_cache_test
    PRIVATE
        xxHash::xxhash
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        bcrypt.lib
)
target_compile_definitions(
    standalone_shader_cache_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
set_tests_properties(
    StandaloneShaderCache
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" TIMEOUT 120
)
if(MSVC)
    target_compile_options(standalone_shader_cache_test PRIVATE /UNDEBUG /utf-8)
else()
    target_compile_options(standalone_shader_cache_test PRIVATE -UNDEBUG)
endif()
