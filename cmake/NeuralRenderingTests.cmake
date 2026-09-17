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
    compute_subrect frame_telemetry_ring dlss_viewport_crop foveated_center_alignment)
    add_controller_test(${_test}_test ${_test} tests/${_test}_test.cpp)
endforeach()
target_link_libraries(character_settings_test PRIVATE nlohmann_json::nlohmann_json)

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
