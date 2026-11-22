set(_compile_history_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/compile-history")
set(_compile_history_headers)
foreach(_header IN ITEMS constants state persistence display atomic completion estimate)
    list(APPEND _compile_history_headers "${_compile_history_dir}/compile_history_${_header}.h")
endforeach()
add_custom_command(
    OUTPUT ${_compile_history_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_compile_history_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_compilation_history.cmake"
    DEPENDS src/ShaderCache.h src/ShaderCache.cpp src/Utils/FileSystem.cpp
        tests/extract_shader_compilation_history.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(shader_compilation_history_test ShaderCompilationHistory
    tests/shader_compilation_history_test.cpp)
target_sources(shader_compilation_history_test PRIVATE ${_compile_history_headers})
target_include_directories(shader_compilation_history_test PRIVATE "${_compile_history_dir}")
target_link_libraries(shader_compilation_history_test PRIVATE nlohmann_json::nlohmann_json fmt::fmt)
target_compile_definitions(shader_compilation_history_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_tests_properties(ShaderCompilationHistory PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" TIMEOUT 30)
