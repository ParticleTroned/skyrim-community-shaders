set(_compilation_scheduling_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shader-compilation-scheduling"
)
set(_compilation_scheduling_headers)
foreach(_header IN ITEMS
    shader_compilation_priority_under_test.h
    shader_compilation_completion_under_test.h
    shader_compilation_worker_under_test.h
    shader_compilation_admission_under_test.h
    shader_compilation_mode_under_test.h
    shader_compilation_phase_under_test.h
    shader_compilation_comparison_under_test.h
)
    list(APPEND _compilation_scheduling_headers
        "${_compilation_scheduling_dir}/${_header}"
    )
endforeach()
add_custom_command(
    OUTPUT ${_compilation_scheduling_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_compilation_scheduling_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_shader_compilation_scheduling.cmake"
    DEPENDS
        src/ShaderCache.cpp src/ShaderCache.h src/Utils/WinApi.cpp
        tests/extract_shader_compilation_scheduling.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(
    shader_compilation_priority_test ShaderCompilationPriority
    tests/shader_compilation_priority_test.cpp
)
add_controller_test(
    shader_compilation_scheduling_test ShaderCompilationScheduling
    tests/shader_compilation_scheduling_test.cpp
)
foreach(_target IN ITEMS
    shader_compilation_priority_test shader_compilation_scheduling_test
)
    target_sources(${_target} PRIVATE ${_compilation_scheduling_headers})
    target_include_directories(${_target} PRIVATE "${_compilation_scheduling_dir}")
    target_compile_definitions(${_target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endforeach()
set_tests_properties(
    ShaderCompilationPriority ShaderCompilationScheduling PROPERTIES TIMEOUT 15
)

target_link_libraries(shader_compilation_scheduling_test PRIVATE Microsoft::CppWinRT magic_enum::magic_enum)
