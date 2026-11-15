set(_map_lifecycle_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/depth-culling-map")
set(_map_lifecycle_test_header "${_map_lifecycle_test_dir}/vr_depth_culling_map_under_test.h")
add_custom_command(
    OUTPUT "${_map_lifecycle_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_map_lifecycle_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_depth_culling_map.cmake"
    DEPENDS
        src/Features/VRDepthCullingTemporal.cpp
        tests/extract_source_region.cmake
        tests/extract_vr_depth_culling_map.cmake
    VERBATIM
)
add_controller_test(
    vr_depth_culling_map_lifecycle_test
    VRDepthCullingMapLifecycle
    tests/vr_depth_culling_map_lifecycle_test.cpp
)
target_sources(vr_depth_culling_map_lifecycle_test PRIVATE "${_map_lifecycle_test_header}")
target_include_directories(vr_depth_culling_map_lifecycle_test PRIVATE "${_map_lifecycle_test_dir}")
