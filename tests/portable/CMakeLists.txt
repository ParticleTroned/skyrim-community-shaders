cmake_minimum_required(VERSION 4.2)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CSXPortableTests LANGUAGES CXX)
    enable_testing()
endif()

get_filename_component(CSX_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(CSX_JSON_INCLUDE "" CACHE PATH "Include directory containing nlohmann/json.hpp")
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.12 CONFIG QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_path(CSX_JSON_INCLUDE_PATH nlohmann/json.hpp HINTS "${CSX_JSON_INCLUDE}" REQUIRED)
    add_library(csx_test_json INTERFACE)
    target_include_directories(csx_test_json INTERFACE "${CSX_JSON_INCLUDE_PATH}")
    add_library(nlohmann_json::nlohmann_json ALIAS csx_test_json)
    set(CSX_JSON_INCLUDE "${CSX_JSON_INCLUDE_PATH}")
elseif(NOT CSX_JSON_INCLUDE)
    get_target_property(CSX_JSON_INCLUDE nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    list(GET CSX_JSON_INCLUDE 0 CSX_JSON_INCLUDE)
endif()

function(csx_add_portable_test name)
    if(TARGET ${name})
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SOURCE" "")
    if(NOT ARG_SOURCE)
        set(ARG_SOURCE "${CSX_PROJECT_ROOT}/tests/${name}.cpp")
    endif()
    add_executable(${name} "${ARG_SOURCE}" ${ARG_UNPARSED_ARGUMENTS})
    target_compile_features(${name} PRIVATE cxx_std_23)
    target_include_directories(${name} PRIVATE "${CSX_PROJECT_ROOT}/src" "${CSX_PROJECT_ROOT}/include")
    target_link_libraries(${name} PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX /permissive- /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_compile_definitions(${name} PRIVATE __cdecl=)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60 LABELS "SEPort;Host;UnitTests")
    set(CSX_PORTABLE_TEST_TARGETS ${CSX_PORTABLE_TEST_TARGETS} ${name} PARENT_SCOPE)
endfunction()

foreach(name IN ITEMS
    fidelityfx_api_compat engine_stutter engine_asset_load engine_stutter_off scene_inventory
    api_editor_contract api_feature_contract api_profiler_contract api_screenshot_contract
    api_shader_service_policy api_weather_service_policy
    main_thread_dispatch_policy main_thread_dispatch_state devbench_main_thread_dispatch
    world_load_transition_policy runtime_toggle shader_cache_enable_policy
    menu_depth_of_field_settings_policy
    grass_bucket_preparation grass_pass_cache grass_draw_batch grass_telemetry
    shadow_culling_snapshot shadow_culling_bounds sun_receiver_policy texture_streaming_policy se_culling_contract
    motion_sharpening_policy motion_sharpening_settings fsr_temporal_tuning_policy fsr_temporal_tuning_serialization
    nr_packed_crop_policy neural_memory_recovery neural_color_policy neural_exposure_policy neural_mono_settings
    fsr_shared_guides_policy neural_stall_recovery_policy neural_mono_compression neural_mono_prewarm input_combo_policy
    native_evaluation_layout character_actor_policy neural_mono_policy mono_upscaling_api_policy
    reverse_z_depth neural_native_memory neural_reference_policy neural_mono_experiments
    performance_tuning_controller performance_tuning_statistics
    pbr_material_registry volumetric_lighting_tuning_policy
    screenshot_mono_policy screenshot_manifest_snapshot)
    csx_add_portable_test(${name}_test)
endforeach()
target_compile_definitions(grass_telemetry_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
target_compile_definitions(menu_depth_of_field_settings_policy_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
target_compile_definitions(neural_exposure_policy_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
target_compile_definitions(engine_stutter_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
target_compile_definitions(engine_asset_load_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
csx_add_portable_test(neural_reference_devbench_test SOURCE "${CSX_PROJECT_ROOT}/tests/neural_reference_policy_test.cpp")
target_compile_definitions(neural_reference_devbench_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
csx_add_portable_test(neural_mono_experiments_devbench_test SOURCE "${CSX_PROJECT_ROOT}/tests/neural_mono_experiments_test.cpp")
target_compile_definitions(neural_mono_experiments_devbench_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
target_sources(api_shader_service_policy_test PRIVATE "${CSX_PROJECT_ROOT}/src/Api/ShaderServicePolicy.cpp")
target_sources(api_weather_service_policy_test PRIVATE "${CSX_PROJECT_ROOT}/src/Api/WeatherServicePolicy.cpp")
target_sources(mono_upscaling_api_policy_test PRIVATE
    "${CSX_PROJECT_ROOT}/src/Api/UpscalingContract.cpp" "${CSX_PROJECT_ROOT}/src/Api/UpscalingServicePolicy.cpp")

csx_add_portable_test(api_service_foundation_test "${CSX_PROJECT_ROOT}/src/Api/ServiceFoundation.cpp")
csx_add_portable_test(api_service_registry_test "${CSX_PROJECT_ROOT}/src/Api/ServiceRegistry.cpp")
csx_add_portable_test(api_runtime_thread_affinity_test "${CSX_PROJECT_ROOT}/src/Api/RuntimeThreadAffinity.cpp")
csx_add_portable_test(accepted_draw_registry_test "${CSX_PROJECT_ROOT}/src/Api/AcceptedDrawRegistry.cpp")
csx_add_portable_test(water_appearance_migration_test)
csx_add_portable_test(preset_compatibility_test "${CSX_PROJECT_ROOT}/src/PresetCompatibility.cpp")

foreach(name IN ITEMS profiler_capture_test se_frame_timing_test)
    csx_add_portable_test(${name}
        "${CSX_PROJECT_ROOT}/src/Profiler.cpp"
        "${CSX_PROJECT_ROOT}/src/SEFrameTiming.cpp"
        "${CSX_PROJECT_ROOT}/src/RenderTargetStats.cpp")
    target_include_directories(${name} BEFORE PRIVATE "${CSX_PROJECT_ROOT}/tests/profiler_stubs")
endforeach()

csx_add_portable_test(profiler_timing_test)
if(WIN32)
    csx_add_portable_test(fidelityfx_runtime_dispatch_test)
    target_compile_definitions(fidelityfx_runtime_dispatch_test PRIVATE _WINDOWS)
    csx_add_portable_test(device_context_state_test)
    target_link_libraries(device_context_state_test PRIVATE d3d11 d3dcompiler runtimeobject)
    if(NOT TARGET spdlog::spdlog)
        find_package(spdlog CONFIG REQUIRED)
    endif()
    target_sources(profiler_timing_test PRIVATE "${CSX_PROJECT_ROOT}/src/Profiler.cpp"
        "${CSX_PROJECT_ROOT}/src/SEFrameTiming.cpp" "${CSX_PROJECT_ROOT}/src/RenderTargetStats.cpp")
    target_link_libraries(profiler_timing_test PRIVATE d3d11 spdlog::spdlog runtimeobject)
    target_compile_definitions(profiler_timing_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_include_directories(profiler_timing_test BEFORE PRIVATE "${CSX_PROJECT_ROOT}/tests/profiler_stubs")
endif()

# Extraction compiles the production functions, with only engine/graphics owners
# supplied by each fixture. Dependencies keep those functions current on rebuild.
function(csx_extract_cmake name script)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "HEADERS;SOURCES")
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${name}")
    set(outputs)
    foreach(header IN LISTS ARG_HEADERS)
        list(APPEND outputs "${output_dir}/${header}")
    endforeach()
    list(GET outputs 0 output_file)
    add_custom_command(OUTPUT ${outputs}
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CSX_PROJECT_ROOT}"
            "-DOUTPUT_DIRECTORY=${output_dir}" "-DOUTPUT_FILE=${output_file}"
            -P "${CSX_PROJECT_ROOT}/tests/${script}"
        DEPENDS "${CSX_PROJECT_ROOT}/tests/${script}" ${ARG_SOURCES}
        VERBATIM)
    csx_add_portable_test(${name} ${outputs})
    target_include_directories(${name} PRIVATE "${output_dir}")
    set(CSX_PORTABLE_TEST_TARGETS ${CSX_PORTABLE_TEST_TARGETS} PARENT_SCOPE)
endfunction()

set(CSX_NR_SDK_INCLUDE "${NEURAL_RENDERING_SDK_INCLUDE_DIR}" CACHE PATH
    "Verified official NR SDK include directory for portable NGX contract tests")
include("${CSX_PROJECT_ROOT}/cmake/NeuralRenderingSDKPin.cmake")
set(_nr_sdk_header "${CSX_NR_SDK_INCLUDE}/nvsdk_ngx_defs_dlssnr.h")
if(NOT EXISTS "${_nr_sdk_header}")
    message(FATAL_ERROR
        "NR contract tests require CSX_NR_SDK_INCLUDE to point at the official SDK "
        "headers cached by the main build's NeuralRendering-Runtime.cmake module.")
endif()
file(SHA256 "${_nr_sdk_header}" _nr_sdk_header_hash)
if(NOT _nr_sdk_header_hash STREQUAL NEURAL_RENDERING_SDK_HEADER_SHA256)
    message(FATAL_ERROR "NR contract tests require the pinned original SDK parameter header")
endif()
set(_nr_test_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/neural-native-creation")
set(_nr_test_headers)
foreach(name IN ITEMS neural_native_key neural_runtime_minimum neural_runtime_probe
    neural_native_creation neural_installed_metadata neural_runtime_tuning neural_runtime_logging
    neural_runtime_detail neural_runtime_reset neural_evaluation_success_detail neural_sdk_parameters
    neural_ngx_initialize neural_ngx_shutdown neural_ngx_statistics)
    list(APPEND _nr_test_headers "${_nr_test_directory}/${name}_under_test.h")
endforeach()
add_custom_command(OUTPUT ${_nr_test_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CSX_PROJECT_ROOT}"
        "-DOUTPUT_DIRECTORY=${_nr_test_directory}" "-DNR_SDK_INCLUDE_DIR=${CSX_NR_SDK_INCLUDE}"
        -P "${CSX_PROJECT_ROOT}/tests/extract_neural_native_creation.cmake"
    DEPENDS "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h"
        "${_nr_sdk_header}"
        "${CSX_PROJECT_ROOT}/extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx_defs.h"
        "${CSX_PROJECT_ROOT}/tests/extract_neural_native_creation.cmake"
        "${CSX_PROJECT_ROOT}/tests/extract_source_region.cmake"
    VERBATIM)
add_custom_target(neural_native_test_sources DEPENDS ${_nr_test_headers})
foreach(name IN ITEMS neural_native_creation neural_runtime_admission neural_ngx_initialization neural_ngx_statistics)
    csx_add_portable_test(${name}_test)
    add_dependencies(${name}_test neural_native_test_sources)
    target_include_directories(${name}_test PRIVATE "${_nr_test_directory}")
endforeach()
target_compile_definitions(neural_ngx_statistics_test PRIVATE DEVBENCH_BRIDGE_ENABLED)

csx_extract_cmake(actor_shape_bounds_test extract_actor_shape_bounds.cmake
    HEADERS actor_shape_bounds_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Utils/ActorUtils.cpp"
        "${CSX_PROJECT_ROOT}/src/Utils/ActorUtils.h")
csx_extract_cmake(neural_mono_startup_test extract_neural_mono_startup.cmake
    HEADERS neural_mono_startup_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoRenderer.cpp"
        "${CSX_PROJECT_ROOT}/tests/extract_source_region.cmake")
csx_extract_cmake(neural_mono_evidence_test extract_neural_mono_evidence.cmake
    HEADERS neural_mono_evidence_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoRenderer.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoExecutionEvidence.h"
        "${CSX_PROJECT_ROOT}/tests/extract_source_region.cmake")
target_compile_definitions(neural_mono_evidence_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
csx_extract_cmake(scene_depth_test extract_scene_depth.cmake
    HEADERS scene_depth_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Deferred.cpp" "${CSX_PROJECT_ROOT}/src/Utils/D3D.cpp")
csx_extract_cmake(skylighting_lifecycle_test extract_skylighting_lifecycle.cmake
    HEADERS skylighting_buffer_under_test.h skylighting_lifecycle_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/Skylighting.cpp" "${CSX_PROJECT_ROOT}/src/Features/Skylighting.h")
csx_extract_cmake(terrain_variation_mesh_test extract_terrain_variation_mesh.cmake
    HEADERS terrain_variation_mesh_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/TerrainVariation.cpp")
csx_extract_cmake(pbr_texture_enumeration_test extract_pbr_texture_enumeration.cmake
    HEADERS pbr_texture_constants_under_test.h pbr_texture_collection_under_test.h pbr_texture_visitor_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/TruePBR/BSLightingShaderMaterialPBRLandscape.h"
        "${CSX_PROJECT_ROOT}/src/TruePBR/BSLightingShaderMaterialPBRLandscape.cpp"
        "${CSX_PROJECT_ROOT}/src/TruePBR/TextureEnumeration.cpp" "${CSX_PROJECT_ROOT}/tests/extract_source_region.cmake")
csx_extract_cmake(screenshot_dispatch_test extract_screenshot_dispatch.cmake
    HEADERS screenshot_dispatch_under_test.h SOURCES "${CSX_PROJECT_ROOT}/src/Features/ScreenshotApi.cpp")
csx_extract_cmake(api_discovery_lifecycle_test extract_api_discovery.cmake
    HEADERS api_discovery_under_test.h SOURCES "${CSX_PROJECT_ROOT}/src/XSEPlugin.cpp")
csx_extract_cmake(accepted_draw_d3d_test extract_accepted_draw_d3d.cmake
    HEADERS accepted_draw_d3d_under_test.h SOURCES "${CSX_PROJECT_ROOT}/src/Api/AcceptedDrawD3D.cpp")
csx_extract_cmake(subrect_settings_test extract_subrect_settings.cmake
    HEADERS subrect_settings_types.h subrect_settings_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Utils/Subrect.cpp" "${CSX_PROJECT_ROOT}/src/Utils/Subrect.h")
csx_extract_cmake(settings_actions_test extract_settings_actions.cmake
    HEADERS settings_actions_types.h settings_actions_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Menu/SettingsActions.cpp" "${CSX_PROJECT_ROOT}/src/Menu/SettingsActions.h"
        "${CSX_PROJECT_ROOT}/tests/extract_source_region.cmake")

function(csx_extract_python name script)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "HEADERS;SOURCES")
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${name}")
    set(outputs)
    foreach(header IN LISTS ARG_HEADERS)
        list(APPEND outputs "${output_dir}/${header}")
    endforeach()
    add_custom_command(OUTPUT ${outputs}
        COMMAND "${Python3_EXECUTABLE}" "${CSX_PROJECT_ROOT}/tests/${script}"
            --source-dir "${CSX_PROJECT_ROOT}" --output-dir "${output_dir}"
        DEPENDS "${CSX_PROJECT_ROOT}/tests/${script}" ${ARG_SOURCES}
        VERBATIM)
    csx_add_portable_test(${name} ${outputs})
    target_include_directories(${name} PRIVATE "${output_dir}")
    set(CSX_PORTABLE_TEST_TARGETS ${CSX_PORTABLE_TEST_TARGETS} PARENT_SCOPE)
endfunction()

csx_extract_python(screenshot_neural_frame_test extract_screenshot_neural_frame.py
    HEADERS screenshot_frame_members.h screenshot_neural_types.h screenshot_neural_frame_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Hooks.cpp" "${CSX_PROJECT_ROOT}/src/Features/ScreenshotFeature.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/ScreenshotNeuralDiagnostics.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoRenderer.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoRenderer.h"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(neural_mono_scene_test extract_neural_mono_scene.py
    HEADERS neural_hdr_members.h neural_scene_types.h neural_upscaling_members.h neural_mono_scene_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/Upscaling.cpp" "${CSX_PROJECT_ROOT}/src/Features/Upscaling.h"
        "${CSX_PROJECT_ROOT}/src/Features/HDRDisplay.cpp" "${CSX_PROJECT_ROOT}/src/Utils/D3D.cpp"
        "${CSX_PROJECT_ROOT}/src/Utils/Game.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoRenderer.h"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(settings_navigation_test extract_settings_navigation.py
    HEADERS settings_navigation_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Menu/SettingsPage.cpp" "${CSX_PROJECT_ROOT}/src/Menu/SettingsPage.h"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(shader_cache_lifetime_test extract_shader_cache_lifetime.py
    HEADERS shader_cache_lifetime_members.h shader_cache_lifetime_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/ShaderCache.cpp" "${CSX_PROJECT_ROOT}/src/ShaderCache.h"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(shader_cache_readiness_test extract_shader_cache_readiness.py
    HEADERS shader_cache_readiness_members.h shader_cache_readiness_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/ShaderCache.cpp" "${CSX_PROJECT_ROOT}/src/ShaderCache.h"
        "${CSX_PROJECT_ROOT}/src/State.cpp" "${CSX_PROJECT_ROOT}/src/XSEPlugin.cpp"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(editor_water_diagnostics_test extract_editor_water_diagnostics.py
    HEADERS editor_water_diagnostics_members.h editor_water_policy_under_test.h editor_water_diagnostics_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/CSEditor/LightEditor.h" "${CSX_PROJECT_ROOT}/src/CSEditor/LightEditor.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/LightLimitFix.h" "${CSX_PROJECT_ROOT}/src/Features/LightLimitFix.cpp"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(editor_localization_test extract_editor_localization.py
    HEADERS editor_localization_tracker.h editor_localization_under_test.h editor_weather_status_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/CSEditor/WeatherUtils.h" "${CSX_PROJECT_ROOT}/src/CSEditor/WeatherUtils.cpp"
        "${CSX_PROJECT_ROOT}/src/CSEditor/EditorWindow.cpp" "${CSX_PROJECT_ROOT}/src/CSEditor/Widget.cpp"
        "${CSX_PROJECT_ROOT}/src/WeatherManager.h" "${CSX_PROJECT_ROOT}/src/WeatherManager.cpp"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(adaptive_balance_dof_test extract_adaptive_balance_dof.py
    HEADERS adaptive_balance_dof_engine_types.h adaptive_balance_dof_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/AdaptiveBalanceDepthOfField.cpp"
        "${CSX_PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/I/ImageSpaceData.h"
        "${CSX_PROJECT_ROOT}/tests/extract_adaptive_balance_toggle.py")
csx_extract_python(adaptive_balance_toggle_test extract_adaptive_balance_toggle.py
    HEADERS linear_lighting_members.h adaptive_balance_effect_permutation_members.h adaptive_balance_members.h
        adaptive_balance_weather_hook_under_test.h adaptive_balance_under_test.h
        adaptive_balance_point_light_members.h adaptive_balance_point_lights_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Features/AdaptiveBrightness.h" "${CSX_PROJECT_ROOT}/src/Features/AdaptiveBrightness.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/LinearLighting.h" "${CSX_PROJECT_ROOT}/src/Features/Bloom.cpp"
        "${CSX_PROJECT_ROOT}/src/Features/WaterAppearance.cpp" "${CSX_PROJECT_ROOT}/src/MenuDevBenchBridge.cpp"
        "${CSX_PROJECT_ROOT}/src/SettingsMigrations.cpp" "${CSX_PROJECT_ROOT}/src/State.cpp" "${CSX_PROJECT_ROOT}/src/State.h")
csx_extract_python(nvperf_policy_test extract_nvperf_policy.py HEADERS nvperf_policy_extracted.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Diagnostics/NvPerf.cpp" "${CSX_PROJECT_ROOT}/src/Diagnostics/NvPerf.h")

include("${CMAKE_CURRENT_LIST_DIR}/ui-dependencies.cmake")
csx_extract_python(adaptive_preset_folder_ui_test extract_adaptive_preset_folder_ui.py
    HEADERS adaptive_preset_folder_ui_under_test.h
    SOURCES "${CSX_PROJECT_ROOT}/src/Menu/AdaptiveBalancePresets.cpp"
        "${CSX_PROJECT_ROOT}/src/Menu/AdaptiveBalancePresets.h")
target_link_libraries(adaptive_preset_folder_ui_test PRIVATE imgui::imgui fmt::fmt)

if(MSVC)
    csx_add_portable_test(adaptive_balance_point_lights_test)
    target_include_directories(adaptive_balance_point_lights_test PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/adaptive_balance_toggle_test")
    add_dependencies(adaptive_balance_point_lights_test adaptive_balance_toggle_test)
endif()

foreach(script IN ITEMS grass_collision_settings_test screenshot_mono_settings_test screenshot_verify_test
    d3d_texture_lifetime_test se_performance_ownership_test se_hdr_ui_test se_upscaling_ui_test
    se_upscaling_resources_test se_upscaling_depth_contract_test reverse_z_initialization_test volumetric_shadow_resources_test
    unified_water_duplicates_test subsurface_helper_region_test render_feature_contracts_test
    feature_issue_scanner_test neural_payload_guard_test streamline_runtime_cache_test shader_deploy_build_rule_test
    preset_export_write_test neural_sdk_cache_test
    shader_cache_pack_builder_test shader_cache_packaging_test shader_cache_se_test)
    add_test(NAME ${script} COMMAND "${CMAKE_COMMAND}" -E env
        "CXX=${CMAKE_CXX_COMPILER}" "CSX_JSON_INCLUDE=${CSX_JSON_INCLUDE}"
        "CMAKE=${CMAKE_COMMAND}"
        "${Python3_EXECUTABLE}" "${CSX_PROJECT_ROOT}/tests/${script}.py")
    set_tests_properties(${script} PROPERTIES TIMEOUT 120 LABELS "SEPort;Host;UnitTests")
    get_filename_component(CSX_CMAKE_BIN_DIRECTORY "${CMAKE_COMMAND}" DIRECTORY)
    set_tests_properties(${script} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CSX_CMAKE_BIN_DIRECTORY}")
endforeach()

add_test(NAME runtime_download_policy_test COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${CSX_PROJECT_ROOT}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/runtime-download-policy"
    -P "${CSX_PROJECT_ROOT}/tests/runtime_download_policy_test.cmake")
set_tests_properties(runtime_download_policy_test PROPERTIES TIMEOUT 120 LABELS "SEPort;Host;UnitTests")

option(CSX_PORTABLE_SE_BASELINE "Include the existing SE host tests in a standalone portable build" OFF)
if(CSX_PORTABLE_SE_BASELINE)
    include("${CSX_PROJECT_ROOT}/tests/host/portable-se-baseline.cmake")
endif()

add_custom_target(run_portable_tests
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
        -C "$<CONFIG>" --output-on-failure -L SEPort
    DEPENDS ${CSX_PORTABLE_TEST_TARGETS}
    COMMENT "Running SE port policy, API and production-function tests"
    USES_TERMINAL VERBATIM)
