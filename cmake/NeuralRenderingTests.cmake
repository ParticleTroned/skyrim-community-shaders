include(NeuralRenderingCaptureTests)
csx_add_neural_rendering_capture_tests("${PROJECT_SOURCE_DIR}" add_controller_test)

foreach(_policy IN ITEMS
    neural_rendering_pipeline character_region character_actor
    character_mask_work world_load_transition)
    add_controller_test(
        ${_policy}_policy_test ${_policy}_policy
        tests/${_policy}_policy_test.cpp
    )
endforeach()

foreach(_test IN ITEMS character_settings character_multi_roi character_mask_roi
    compute_subrect frame_telemetry_ring dlss_viewport_crop foveated_center_alignment foveated_region_plan)
    add_controller_test(${_test}_test ${_test} tests/${_test}_test.cpp)
endforeach()
target_link_libraries(character_settings_test PRIVATE nlohmann_json::nlohmann_json)

set(_neural_full_resolution_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_full_resolution_test")
add_custom_command(
    OUTPUT "${_neural_full_resolution_test_dir}/neural_full_resolution_preparation_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_full_resolution_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_full_resolution_preparation.cmake"
    DEPENDS src/Features/Upscaling.cpp tests/extract_neural_full_resolution_preparation.cmake
    VERBATIM
)
add_controller_test(neural_full_resolution_preparation_test NeuralFullResolutionPreparation
    tests/neural_full_resolution_preparation_test.cpp)
target_sources(neural_full_resolution_preparation_test PRIVATE
    "${_neural_full_resolution_test_dir}/neural_full_resolution_preparation_under_test.h")
target_include_directories(neural_full_resolution_preparation_test PRIVATE "${_neural_full_resolution_test_dir}")

add_controller_test(neural_main_depth_presentation_test NeuralMainDepthPresentation
    tests/neural_main_depth_presentation_test.cpp)
add_test(NAME NeuralMainDepthPresentationContract COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}" -P
    "${PROJECT_SOURCE_DIR}/tests/neural_main_depth_presentation_contract_test.cmake")
set_tests_properties(NeuralMainDepthPresentationContract PROPERTIES LABELS "ControllerTests")

foreach(_test IN ITEMS character_mask character_mask_bounds)
    add_executable(${_test}_gpu_test EXCLUDE_FROM_ALL tests/${_test}_gpu_test.cpp)
    target_compile_features(${_test}_gpu_test PRIVATE cxx_std_23)
    target_include_directories(${_test}_gpu_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${_test}_gpu_test PRIVATE d3d11 d3d12 d3dcompiler)
    add_test(NAME ${_test}_shaders COMMAND ${_test}_gpu_test "${PROJECT_SOURCE_DIR}/package/Shaders")
    set_tests_properties(${_test}_shaders PROPERTIES LABELS "ControllerTests" TIMEOUT 60)
endforeach()

foreach(_contract IN ITEMS neural_rendering_devbench neural_rendering_submit_pair neural_multi_roi)
    add_test(NAME ${_contract}_contract COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}" -P
        "${PROJECT_SOURCE_DIR}/tests/${_contract}_contract_test.cmake")
    set_tests_properties(${_contract}_contract PROPERTIES LABELS "ControllerTests")
endforeach()

add_test(NAME NeuralRenderingRuntimeStaging COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/neural_rendering_runtime_test.py")
set_tests_properties(NeuralRenderingRuntimeStaging PROPERTIES LABELS "ControllerTests" TIMEOUT 300)

set(_neural_full_resolution_fov_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_full_resolution_fov_test")
add_custom_command(
    OUTPUT "${_neural_full_resolution_fov_test_dir}/neural_full_resolution_fov_under_test.h"
        "${_neural_full_resolution_fov_test_dir}/neural_full_resolution_fov_types.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_full_resolution_fov_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_full_resolution_fov.cmake"
    DEPENDS src/Features/Upscaling.cpp src/Features/Upscaling.h tests/extract_neural_full_resolution_fov.cmake
    VERBATIM
)
add_controller_test(neural_full_resolution_fov_test NeuralFullResolutionFov
    tests/neural_full_resolution_fov_test.cpp)
target_sources(neural_full_resolution_fov_test PRIVATE
    "${_neural_full_resolution_fov_test_dir}/neural_full_resolution_fov_under_test.h"
    "${_neural_full_resolution_fov_test_dir}/neural_full_resolution_fov_types.h")
target_include_directories(neural_full_resolution_fov_test PRIVATE "${_neural_full_resolution_fov_test_dir}")

set(_neural_color_route_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_color_route_test")
add_custom_command(
    OUTPUT "${_neural_color_route_test_dir}/neural_color_route_latch_state.h"
        "${_neural_color_route_test_dir}/neural_color_route_latch_pipeline.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_color_route_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_color_route_latch.cmake"
    DEPENDS src/Features/Upscaling.cpp src/Features/Upscaling/NeuralRendering/Renderer.cpp
        src/Features/Upscaling/NeuralRendering/ColorPipeline.cpp tests/extract_neural_color_route_latch.cmake
    VERBATIM
)
add_controller_test(neural_color_route_latch_test NeuralColorRouteLatch tests/neural_color_route_latch_test.cpp)
target_sources(neural_color_route_latch_test PRIVATE
    "${_neural_color_route_test_dir}/neural_color_route_latch_state.h"
    "${_neural_color_route_test_dir}/neural_color_route_latch_pipeline.h")
target_include_directories(neural_color_route_latch_test PRIVATE "${_neural_color_route_test_dir}")
target_link_libraries(neural_color_route_latch_test PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(neural_color_route_latch_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
