add_controller_test(nr_model_resolution_policy_test NRModelResolutionPolicy tests/nr_model_resolution_policy_test.cpp)
set(_nr_accounting_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/nr-model-resolution")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
    "-DOUTPUT_DIRECTORY=${_nr_accounting_directory}" -P
    "${PROJECT_SOURCE_DIR}/tests/extract_nr_model_resolution_accounting.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
    "${PROJECT_SOURCE_DIR}/tests/extract_nr_model_resolution_accounting.cmake")
set(_nr_model_targets nr_model_resolution_shader_test nr_model_resolution_shader_bridge_test)
set(_nr_model_tests NRModelResolutionShadersWARP NRModelResolutionShadersWARPBridge)
foreach(_target _test IN ZIP_LISTS _nr_model_targets _nr_model_tests)
    add_d3d_shader_test(${_target} ${_test} tests/nr_model_resolution_shader_test.cpp)
    target_sources(${_target} PRIVATE src/Features/Upscaling/NeuralRendering/ModelResolution.cpp)
    target_compile_definitions(${_target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(${_target} PRIVATE nlohmann_json::nlohmann_json Microsoft::DirectXTex)
    target_include_directories(${_target} SYSTEM PRIVATE
        "$<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_INCLUDE_DIRECTORIES>")
    target_include_directories(${_target} PRIVATE "${_nr_accounting_directory}")
endforeach()
target_compile_definitions(nr_model_resolution_shader_bridge_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
add_controller_test(neural_memory_recovery_test NeuralMemoryRecovery tests/neural_memory_recovery_test.cpp)
add_test(NAME NeuralMemoryRecoveryContract COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/neural_memory_recovery_contract_test.py")
set_tests_properties(NeuralMemoryRecoveryContract PROPERTIES LABELS "ControllerTests" TIMEOUT 30)

include(NeuralRenderingCaptureTests)
foreach(_bridge IN ITEMS on off)
    add_controller_test(neural_production_policy_${_bridge}_test NeuralProductionPolicy_${_bridge}
        tests/neural_production_policy_test.cpp)
    target_link_libraries(neural_production_policy_${_bridge}_test PRIVATE nlohmann_json::nlohmann_json)
endforeach()
target_compile_definitions(neural_production_policy_on_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
foreach(_test IN ITEMS neural_compact_layout neural_execution_plan)
    add_controller_test(${_test}_test ${_test} tests/${_test}_test.cpp)
    target_compile_definitions(${_test}_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
    target_link_libraries(${_test}_test PRIVATE nlohmann_json::nlohmann_json)
endforeach()
csx_add_neural_rendering_capture_tests("${PROJECT_SOURCE_DIR}" add_controller_test)

foreach(_bridge IN ITEMS on off)
    add_controller_test(neural_source_transport_${_bridge}_test NeuralSourceTransport_${_bridge} tests/neural_source_transport_test.cpp)
    target_compile_definitions(neural_source_transport_${_bridge}_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(neural_source_transport_${_bridge}_test PRIVATE nlohmann_json::nlohmann_json)
endforeach()
target_compile_definitions(neural_source_transport_on_test PRIVATE DEVBENCH_BRIDGE_ENABLED)

set(_neural_renderer_ownership_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_renderer_ownership")
add_custom_command(
    OUTPUT "${_neural_renderer_ownership_dir}/neural_renderer_ownership_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_renderer_ownership_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_renderer_ownership.cmake"
    DEPENDS src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp tests/extract_neural_renderer_ownership.cmake
    VERBATIM)
add_controller_test(neural_renderer_ownership_test NeuralRendererOwnership tests/neural_renderer_ownership_test.cpp)
target_sources(neural_renderer_ownership_test PRIVATE "${_neural_renderer_ownership_dir}/neural_renderer_ownership_under_test.h")
target_include_directories(neural_renderer_ownership_test PRIVATE "${_neural_renderer_ownership_dir}")
target_include_directories(neural_renderer_ownership_test SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG/include")
target_compile_definitions(neural_renderer_ownership_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(neural_renderer_ownership_test PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)
set_tests_properties(NeuralRendererOwnership PROPERTIES TIMEOUT 10)

set(_neural_resource_key_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_resource_key_test")
add_custom_command(
    OUTPUT "${_neural_resource_key_test_dir}/neural_resource_key_under_test.h"
        "${_neural_resource_key_test_dir}/neural_resource_key_types.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_resource_key_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_resource_key.cmake"
    DEPENDS src/Features/Upscaling.cpp src/Features/Upscaling.h tests/extract_neural_resource_key.cmake
    VERBATIM
)
add_controller_test(neural_resource_key_test NeuralResourceKey tests/neural_resource_key_test.cpp)
target_sources(neural_resource_key_test PRIVATE
    "${_neural_resource_key_test_dir}/neural_resource_key_under_test.h"
    "${_neural_resource_key_test_dir}/neural_resource_key_types.h")
target_include_directories(neural_resource_key_test PRIVATE "${_neural_resource_key_test_dir}")

foreach(_policy IN ITEMS
    neural_rendering_pipeline character_region character_actor
    character_mask_work world_load_transition)
    add_controller_test(
        ${_policy}_policy_test ${_policy}_policy
        tests/${_policy}_policy_test.cpp
    )
endforeach()

foreach(_test IN ITEMS character_focus character_crop character_settings geometry_bounds
    compute_subrect roi_descriptor native_evaluation_layout frame_telemetry_ring dlss_viewport_crop foveated_region_plan)
    add_controller_test(${_test}_test ${_test} tests/${_test}_test.cpp)
endforeach()
target_link_libraries(character_settings_test PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(character_settings_test PRIVATE DEVBENCH_BRIDGE_ENABLED)

set(_character_material_test_dir "${CMAKE_CURRENT_BINARY_DIR}/character_material_test")
add_custom_command(
    OUTPUT "${_character_material_test_dir}/character_material_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_character_material_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_character_material.cmake"
    DEPENDS src/Utils/CharacterCategoryAuthoring.cpp tests/extract_character_material.cmake
    VERBATIM
)
add_controller_test(character_material_test character_material tests/character_material_test.cpp)
target_sources(character_material_test PRIVATE "${_character_material_test_dir}/character_material_under_test.h")
target_include_directories(character_material_test PRIVATE "${_character_material_test_dir}")

add_controller_test(neural_current_context_test NeuralCurrentContext tests/neural_current_context_test.cpp)
target_compile_definitions(neural_current_context_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
add_test(NAME NeuralCurrentContextBridgeGate COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/neural_current_context_bridge_gate_test.py" --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(NeuralCurrentContextBridgeGate PROPERTIES LABELS "ControllerTests" TIMEOUT 30)
add_test(NAME NeuralCurrentContextContract COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/neural_current_context_contract_test.py")
set_tests_properties(NeuralCurrentContextContract PROPERTIES LABELS "ControllerTests" TIMEOUT 30)

set(_neural_compute_guard_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_compute_guard_test")
add_custom_command(
    OUTPUT "${_neural_compute_guard_test_dir}/d3d_resource_naming.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_compute_guard_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_d3d_resource_naming.cmake"
    DEPENDS src/Utils/D3D.cpp tests/extract_d3d_resource_naming.cmake
    VERBATIM
)
add_controller_test(neural_compute_state_guard_test NeuralComputeStateGuard
    tests/neural_compute_state_guard_test.cpp)
target_sources(neural_compute_state_guard_test PRIVATE
    "${_neural_compute_guard_test_dir}/d3d_resource_naming.h")
target_include_directories(neural_compute_state_guard_test PRIVATE "${_neural_compute_guard_test_dir}")
target_compile_definitions(neural_compute_state_guard_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(neural_compute_state_guard_test PRIVATE d3d11 d3dcompiler)
set_tests_properties(NeuralComputeStateGuard PROPERTIES TIMEOUT 30)

set(_foveated_geometry_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/foveated_geometry_test"
)
add_custom_command(
    OUTPUT "${_foveated_geometry_test_dir}/foveated_mask_geometry_under_test.h"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_foveated_geometry_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_foveated_mask_geometry.cmake"
    DEPENDS
        src/Features/Upscaling.cpp
        tests/extract_foveated_mask_geometry.cmake
    VERBATIM
)
add_controller_test(foveated_mask_geometry_test FoveatedMaskGeometry tests/foveated_mask_geometry_test.cpp)
target_sources(
    foveated_mask_geometry_test
    PRIVATE "${_foveated_geometry_test_dir}/foveated_mask_geometry_under_test.h"
)
target_include_directories(
    foveated_mask_geometry_test
    PRIVATE "${_foveated_geometry_test_dir}"
)
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
set(_neural_presentation_hook_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_presentation_hook_test")
add_custom_command(
    OUTPUT "${_neural_presentation_hook_test_dir}/neural_presentation_hook_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_presentation_hook_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_presentation_hook.cmake"
    DEPENDS src/Features/Upscaling.cpp tests/extract_neural_presentation_hook.cmake
    VERBATIM
)
add_controller_test(neural_presentation_hook_test NeuralPresentationHook tests/neural_presentation_hook_test.cpp)
target_sources(neural_presentation_hook_test PRIVATE
    "${_neural_presentation_hook_test_dir}/neural_presentation_hook_under_test.h")
target_include_directories(neural_presentation_hook_test PRIVATE "${_neural_presentation_hook_test_dir}")
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
add_dependencies(character_mask_gpu_test d3d_shader_test_headers)
target_include_directories(character_mask_gpu_test PRIVATE "${_d3d_shader_test_dir}")

foreach(_contract IN ITEMS neural_rendering_devbench neural_rendering_submit_pair neural_single_roi)
    add_test(NAME ${_contract}_contract COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}" -P
        "${PROJECT_SOURCE_DIR}/tests/${_contract}_contract_test.cmake")
    set_tests_properties(${_contract}_contract PROPERTIES LABELS "ControllerTests")
endforeach()

set(_neural_reduced_selection_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_reduced_selection_test")
add_custom_command(
    OUTPUT "${_neural_reduced_selection_test_dir}/neural_reduced_selection_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_reduced_selection_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_reduced_selection.cmake"
    DEPENDS src/Features/Upscaling.cpp tests/extract_neural_reduced_selection.cmake
    VERBATIM
)
add_controller_test(neural_reduced_selection_test NeuralReducedSelection
    tests/neural_reduced_selection_test.cpp)
target_sources(neural_reduced_selection_test PRIVATE
    "${_neural_reduced_selection_test_dir}/neural_reduced_selection_under_test.h")
target_include_directories(neural_reduced_selection_test PRIVATE "${_neural_reduced_selection_test_dir}")

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
foreach(_suffix IN ITEMS "" "_bridge")
    add_controller_test(neural_color_route_latch${_suffix}_test NeuralColorRouteLatch${_suffix} tests/neural_color_route_latch_test.cpp)
    target_sources(neural_color_route_latch${_suffix}_test PRIVATE
        "${_neural_color_route_test_dir}/neural_color_route_latch_state.h"
        "${_neural_color_route_test_dir}/neural_color_route_latch_pipeline.h")
    target_include_directories(neural_color_route_latch${_suffix}_test PRIVATE "${_neural_color_route_test_dir}")
    target_link_libraries(neural_color_route_latch${_suffix}_test PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(neural_color_route_latch${_suffix}_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endforeach()
target_compile_definitions(neural_color_route_latch_bridge_test PRIVATE DEVBENCH_BRIDGE_ENABLED)

add_controller_test(neural_execution_evidence_test NeuralExecutionEvidence tests/neural_execution_evidence_test.cpp)
add_controller_test(neural_lifetime_diagnostics_test NeuralLifetimeDiagnostics tests/neural_lifetime_diagnostics_test.cpp)
target_link_libraries(neural_lifetime_diagnostics_test PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(neural_lifetime_diagnostics_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
add_test(NAME NeuralLifetimeBridgeGate COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/neural_lifetime_bridge_gate_test.py" --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(NeuralLifetimeBridgeGate PROPERTIES LABELS "ControllerTests" TIMEOUT 30)
set(_neural_execution_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_execution_test")
add_custom_command(
    OUTPUT "${_neural_execution_test_dir}/neural_execution_wait_scope.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_execution_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_execution_wait.cmake"
    DEPENDS src/Features/Upscaling/NeuralRendering/D3D12Interop.cpp tests/extract_neural_execution_wait.cmake
    VERBATIM
)
target_sources(neural_execution_evidence_test PRIVATE "${_neural_execution_test_dir}/neural_execution_wait_scope.h")
target_include_directories(neural_execution_evidence_test PRIVATE "${_neural_execution_test_dir}")

add_controller_test(neural_character_evidence_test NeuralCharacterEvidence tests/neural_character_evidence_test.cpp)
target_link_libraries(neural_character_evidence_test PRIVATE nlohmann_json::nlohmann_json)

set(_neural_selection_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_selection_test")
set(_neural_selection_headers
    "${_neural_selection_test_dir}/neural_source_types_under_test.h"
    "${_neural_selection_test_dir}/neural_source_capture_under_test.h"
    "${_neural_selection_test_dir}/neural_source_admission_under_test.h"
    "${_neural_selection_test_dir}/neural_source_support_under_test.h"
    "${_neural_selection_test_dir}/neural_authored_mode_under_test.h"
    "${_neural_selection_test_dir}/neural_empty_preflight_under_test.h"
    "${_neural_selection_test_dir}/neural_full_compute_subrect_under_test.h"
    "${_neural_selection_test_dir}/neural_prepared_slot_under_test.h"
    "${_neural_selection_test_dir}/neural_empty_proof_under_test.h"
    "${_neural_selection_test_dir}/neural_disposition_under_test.h"
    "${_neural_selection_test_dir}/neural_eye_mask_under_test.h"
    "${_neural_selection_test_dir}/neural_empty_dispatch_under_test.h"
    "${_neural_selection_test_dir}/neural_preparation_evidence_under_test.h"
    "${_neural_selection_test_dir}/neural_prepared_result_under_test.h"
    "${_neural_selection_test_dir}/neural_prepared_selection_under_test.h")
add_custom_command(
    OUTPUT ${_neural_selection_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_selection_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_prepared_selection.cmake"
    DEPENDS src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp
        src/Features/Upscaling.cpp
        tests/extract_neural_prepared_selection.cmake
    VERBATIM)
add_custom_target(neural_prepared_selection_fixture DEPENDS ${_neural_selection_headers})
add_controller_test(neural_prepared_selection_test NeuralPreparedSelection tests/neural_prepared_selection_test.cpp)
add_controller_test(neural_prepared_selection_bridge_test NeuralPreparedSelectionBridge tests/neural_prepared_selection_test.cpp)
add_controller_test(neural_empty_dispatch_test NeuralEmptyDispatch tests/neural_empty_dispatch_test.cpp)
add_controller_test(neural_empty_preflight_test NeuralEmptyPreflight tests/neural_empty_preflight_test.cpp)
add_controller_test(neural_empty_preflight_bridge_test NeuralEmptyPreflight_bridge tests/neural_empty_preflight_test.cpp)
target_compile_definitions(neural_empty_preflight_bridge_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
add_controller_test(neural_source_transaction_test NeuralSourceTransaction tests/neural_source_transaction_test.cpp)
foreach(_target IN ITEMS neural_prepared_selection_test neural_prepared_selection_bridge_test neural_empty_dispatch_test neural_empty_preflight_test neural_empty_preflight_bridge_test neural_source_transaction_test)
    add_dependencies(${_target} neural_prepared_selection_fixture)
    target_sources(${_target} PRIVATE ${_neural_selection_headers})
    target_include_directories(${_target} PRIVATE "${_neural_selection_test_dir}")
    target_compile_definitions(${_target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endforeach()
target_compile_definitions(neural_prepared_selection_bridge_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
add_test(NAME NeuralPreparedSelectionContract COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/neural_prepared_selection_contract_test.py" --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(NeuralPreparedSelectionContract PROPERTIES LABELS "ControllerTests" TIMEOUT 30)

add_controller_test(neural_replay_capture_test NeuralReplayCapture tests/neural_replay_capture_test.cpp)
target_sources(neural_replay_capture_test PRIVATE
    src/Features/Upscaling/NeuralRendering/ReplayCapture.cpp src/Utils/CryptoHash.cpp)
target_compile_definitions(neural_replay_capture_test PRIVATE DEVBENCH_BRIDGE_ENABLED NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(neural_replay_capture_test PRIVATE nlohmann_json::nlohmann_json d3d11 dxgi dxguid bcrypt)
set_tests_properties(NeuralReplayCapture PROPERTIES TIMEOUT 30)
set(_neural_replay_request_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_replay_request")
add_custom_command(
    OUTPUT "${_neural_replay_request_dir}/neural_replay_request_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_replay_request_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_replay_request.cmake"
    DEPENDS src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp tests/extract_neural_replay_request.cmake
    VERBATIM)
add_controller_test(neural_replay_request_test NeuralReplayRequest tests/neural_replay_request_test.cpp)
target_sources(neural_replay_request_test PRIVATE "${_neural_replay_request_dir}/neural_replay_request_under_test.h")
target_include_directories(neural_replay_request_test PRIVATE "${_neural_replay_request_dir}")
target_link_libraries(neural_replay_request_test PRIVATE nlohmann_json::nlohmann_json)

set(_neural_controls_test_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_controls_test")
add_custom_command(
    OUTPUT "${_neural_controls_test_dir}/neural_rendering_controls_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_controls_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_rendering_controls.cmake"
    DEPENDS src/Features/Upscaling.cpp tests/extract_neural_rendering_controls.cmake
    VERBATIM)
add_controller_test(neural_rendering_controls_test NeuralRenderingControls tests/neural_rendering_controls_test.cpp)
target_include_directories(neural_rendering_controls_test PRIVATE "${_neural_controls_test_dir}")
target_sources(neural_rendering_controls_test PRIVATE "${_neural_controls_test_dir}/neural_rendering_controls_under_test.h")
