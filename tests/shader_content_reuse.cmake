set(_content_reuse_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shader-content-reuse"
)
set(_content_reuse_headers)
foreach(
    _header
    IN
    ITEMS
        content_blob_io.h
        content_verified_read.h
        content_pack_metadata.h
        content_pack_reuse.h
        content_persist.h
        content_record_digest.h
        content_abi_snapshot.h
)
    list(APPEND _content_reuse_headers "${_content_reuse_dir}/${_header}")
endforeach()
add_custom_command(
    OUTPUT ${_content_reuse_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_content_reuse_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_content_reuse.cmake"
    DEPENDS
        src/ShaderCache.cpp
        tests/extract_shader_content_reuse.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(shader_content_reuse_test ShaderContentReuse tests/shader_content_reuse_test.cpp)
target_sources(
    shader_content_reuse_test
    PRIVATE
        ${_content_reuse_headers}
        src/Utils/ShaderCachePack.cpp
        src/Utils/CryptoHash.cpp
        src/Api/ShaderCompatibilityRegistry.cpp
)
target_include_directories(
    shader_content_reuse_test
    PRIVATE "${_content_reuse_dir}" "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
    shader_content_reuse_test
    PRIVATE
        xxHash::xxhash
        nlohmann_json::nlohmann_json
        bcrypt.lib
        d3dcompiler.lib
)
target_compile_definitions(
    shader_content_reuse_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
set_tests_properties(ShaderContentReuse PROPERTIES TIMEOUT 60)
if(MSVC)
    target_compile_definitions(shader_content_reuse_test PRIVATE _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
    target_compile_options(shader_content_reuse_test PRIVATE /UNDEBUG /utf-8)
else()
    target_compile_options(shader_content_reuse_test PRIVATE -UNDEBUG)
endif()
