set(_d3d_shader_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/d3d-shader-tests"
)
set(_d3d_shader_test_naming_header
    "${_d3d_shader_test_dir}/d3d_resource_naming.h"
)
add_custom_command(
    OUTPUT "${_d3d_shader_test_naming_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_d3d_shader_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_d3d_resource_naming.cmake"
    DEPENDS src/Utils/D3D.cpp tests/extract_d3d_resource_naming.cmake
    VERBATIM
)
add_custom_target(
    d3d_shader_test_headers
    DEPENDS "${_d3d_shader_test_naming_header}"
)

function(add_d3d_shader_test TARGET_NAME TEST_NAME SOURCE_FILE)
    add_controller_test(${TARGET_NAME} ${TEST_NAME} ${SOURCE_FILE})
    add_dependencies(${TARGET_NAME} d3d_shader_test_headers)
    target_include_directories(${TARGET_NAME} PRIVATE "${_d3d_shader_test_dir}")
    target_link_libraries(${TARGET_NAME} PRIVATE d3d11.lib d3dcompiler.lib)
    set_tests_properties(
        ${TEST_NAME}
        PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 60
    )
endfunction()
