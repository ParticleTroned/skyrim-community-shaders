set(_linear_geometry_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/linear-geometry")
set(_linear_geometry_headers)
foreach(_header IN ITEMS payload resources upload)
    list(APPEND _linear_geometry_headers "${_linear_geometry_dir}/linear_geometry_${_header}.h")
endforeach()
add_custom_command(
    OUTPUT ${_linear_geometry_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_linear_geometry_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_linear_geometry_upload.cmake"
    DEPENDS src/Features/LinearLighting.cpp src/Features/LinearLighting.h
        tests/extract_linear_geometry_upload.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(linear_geometry_upload_test LinearGeometryUpload tests/linear_geometry_upload_test.cpp)
target_sources(linear_geometry_upload_test PRIVATE ${_linear_geometry_headers})
target_include_directories(linear_geometry_upload_test PRIVATE "${_linear_geometry_dir}")
