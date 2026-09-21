cmake_minimum_required(VERSION 4.2)

if(NOT TARGET Catch2::Catch2)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

add_executable(
    performance_tuning_tests
    main.cpp
    light_limit_fix_shadow_policy_tests.cpp
    performance_tuning_measurement_tests.cpp
    vanity_camera_tests.cpp
    performance_tuning_json_tests.cpp
    shader_compilation_scheduling_policy_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/Menu/PerformanceTuningJson.cpp
    ${CMAKE_SOURCE_DIR}/src/Menu/PerformanceTuningJson.h
    ${CMAKE_SOURCE_DIR}/src/Menu/PerformanceTuningMeasurement.cpp
    ${CMAKE_SOURCE_DIR}/src/Menu/PerformanceTuningMeasurement.h
)

target_compile_features(performance_tuning_tests PRIVATE cxx_std_23)
target_include_directories(
    performance_tuning_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(
    performance_tuning_tests
    PRIVATE
        Catch2::Catch2
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(
        performance_tuning_tests
        PRIVATE
            /W4
            /WX
            /permissive-
    )
endif()

set_target_properties(
    performance_tuning_tests
    PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:performance_tuning_tests>"
)

add_test(
    NAME PerformanceTuningTests
    COMMAND performance_tuning_tests --reporter compact
    WORKING_DIRECTORY $<TARGET_FILE_DIR:performance_tuning_tests>
)
set_tests_properties(
    PerformanceTuningTests
    PROPERTIES
        TIMEOUT 30
        LABELS "Host;UnitTests;PerformanceTuning"
)

set(_scene_depth_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/scene-depth")
add_custom_command(
    OUTPUT "${_scene_depth_test_dir}/scene_depth_under_test.h"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_scene_depth_test_dir}" -P
        "${CMAKE_SOURCE_DIR}/tests/extract_scene_depth.cmake"
    DEPENDS
        ${CMAKE_SOURCE_DIR}/src/Deferred.cpp
        ${CMAKE_SOURCE_DIR}/src/Utils/D3D.cpp
        ${CMAKE_SOURCE_DIR}/tests/extract_scene_depth.cmake
    VERBATIM
)
add_executable(
    scene_depth_test
    ${CMAKE_SOURCE_DIR}/tests/scene_depth_test.cpp
    "${_scene_depth_test_dir}/scene_depth_under_test.h"
)
target_compile_features(scene_depth_test PRIVATE cxx_std_23)
target_include_directories(scene_depth_test PRIVATE "${_scene_depth_test_dir}")
if(MSVC)
    target_compile_options(scene_depth_test PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME SceneDepth COMMAND scene_depth_test)
set_tests_properties(SceneDepth PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;SceneDepth")

set(_flowmap_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/unified-water-flowmap")
set(_flowmap_test_header "${_flowmap_test_dir}/unified_water_flowmap_under_test.h")
add_custom_command(
    OUTPUT "${_flowmap_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_flowmap_test_dir}" -P
        "${CMAKE_SOURCE_DIR}/tests/extract_unified_water_flowmap.cmake"
    DEPENDS
        ${CMAKE_SOURCE_DIR}/src/Features/UnifiedWater/Flowmap.cpp
        ${CMAKE_SOURCE_DIR}/src/Features/UnifiedWater.cpp
        ${CMAKE_SOURCE_DIR}/src/Hooks.cpp
        ${CMAKE_SOURCE_DIR}/tests/extract_unified_water_flowmap.cmake
    VERBATIM
)
add_executable(
    unified_water_flowmap_test
    ${CMAKE_SOURCE_DIR}/tests/unified_water_flowmap_test.cpp
    "${_flowmap_test_header}"
)
target_compile_features(unified_water_flowmap_test PRIVATE cxx_std_23)
target_include_directories(
    unified_water_flowmap_test
    PRIVATE ${CMAKE_SOURCE_DIR}/src "${_flowmap_test_dir}"
)
if(MSVC)
    target_compile_options(unified_water_flowmap_test PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME UnifiedWaterFlowmap COMMAND unified_water_flowmap_test)
set_tests_properties(
    UnifiedWaterFlowmap
    PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;UnifiedWater"
)

add_executable(
    virtual_function_hook_test
    ${CMAKE_SOURCE_DIR}/tests/virtual_function_hook_test.cpp
)
target_compile_features(virtual_function_hook_test PRIVATE cxx_std_23)
target_include_directories(
    virtual_function_hook_test
    PRIVATE ${CMAKE_SOURCE_DIR}/src ${DETOURS_INCLUDE_DIRS}
)
target_compile_definitions(
    virtual_function_hook_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(virtual_function_hook_test PRIVATE ${DETOURS_LIBRARY})
if(MSVC)
    target_compile_options(virtual_function_hook_test PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME VirtualFunctionHook COMMAND virtual_function_hook_test)
set_tests_properties(
    VirtualFunctionHook
    PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;Hooks"
)

set(_vl_blur_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/volumetric-blur")
set(_vl_blur_test_header "${_vl_blur_test_dir}/volumetric_blur_under_test.h")
add_custom_command(
    OUTPUT "${_vl_blur_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_vl_blur_test_header}" -P
        "${CMAKE_SOURCE_DIR}/tests/extract_volumetric_lighting_blur.cmake"
    DEPENDS
        ${CMAKE_SOURCE_DIR}/src/Features/VolumetricLighting.cpp
        ${CMAKE_SOURCE_DIR}/tests/extract_volumetric_lighting_blur.cmake
    VERBATIM
)
add_executable(
    volumetric_lighting_blur_test
    ${CMAKE_SOURCE_DIR}/tests/volumetric_lighting_blur_test.cpp
    "${_vl_blur_test_header}"
)
target_compile_features(volumetric_lighting_blur_test PRIVATE cxx_std_23)
target_include_directories(
    volumetric_lighting_blur_test
    PRIVATE "${_vl_blur_test_dir}"
)
if(MSVC)
    target_compile_options(
        volumetric_lighting_blur_test
        PRIVATE /W4 /WX /permissive- "$<$<CONFIG:Release>:/fp:fast>"
    )
endif()
add_test(NAME VolumetricLightingBlur COMMAND volumetric_lighting_blur_test)
set_tests_properties(
    VolumetricLightingBlur
    PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;VolumetricLighting"
)

set(_skylighting_settings_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/skylighting-settings")
set(_skylighting_settings_test_header "${_skylighting_settings_test_dir}/skylighting_settings_under_test.h")
add_custom_command(
    OUTPUT "${_skylighting_settings_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_skylighting_settings_test_header}" -P
        "${CMAKE_SOURCE_DIR}/tests/extract_skylighting_settings.cmake"
    DEPENDS
        ${CMAKE_SOURCE_DIR}/src/Features/Skylighting.cpp
        ${CMAKE_SOURCE_DIR}/tests/extract_skylighting_settings.cmake
    VERBATIM
)
add_executable(
    skylighting_settings_test
    ${CMAKE_SOURCE_DIR}/tests/skylighting_settings_test.cpp
    "${_skylighting_settings_test_header}"
)
target_compile_features(skylighting_settings_test PRIVATE cxx_std_23)
target_include_directories(skylighting_settings_test PRIVATE "${_skylighting_settings_test_dir}")
if(MSVC)
    target_compile_options(skylighting_settings_test PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME SkylightingSettings COMMAND skylighting_settings_test)
set_tests_properties(
    SkylightingSettings
    PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;Skylighting"
)

set(_actor_bounds_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/actor-shape-bounds")
set(_actor_bounds_test_header "${_actor_bounds_test_dir}/actor_shape_bounds_under_test.h")
add_custom_command(
    OUTPUT "${_actor_bounds_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_actor_bounds_test_header}" -P
        "${CMAKE_SOURCE_DIR}/tests/extract_actor_shape_bounds.cmake"
    DEPENDS
        ${CMAKE_SOURCE_DIR}/src/Utils/ActorUtils.cpp
        ${CMAKE_SOURCE_DIR}/tests/extract_actor_shape_bounds.cmake
    VERBATIM
)
add_executable(
    actor_shape_bounds_test
    ${CMAKE_SOURCE_DIR}/tests/actor_shape_bounds_test.cpp
    "${_actor_bounds_test_header}"
)
target_compile_features(actor_shape_bounds_test PRIVATE cxx_std_23)
target_include_directories(actor_shape_bounds_test PRIVATE "${_actor_bounds_test_dir}")
if(MSVC)
    target_compile_options(
        actor_shape_bounds_test
        PRIVATE /W4 /WX /permissive- "$<$<CONFIG:Release>:/fp:fast>"
    )
endif()
add_test(NAME ActorShapeBounds COMMAND actor_shape_bounds_test)
set_tests_properties(
    ActorShapeBounds
    PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;Grass"
)

add_executable(
    volumetric_lighting_tuning_policy_test
    ${CMAKE_SOURCE_DIR}/tests/volumetric_lighting_tuning_policy_test.cpp
)
target_compile_features(volumetric_lighting_tuning_policy_test PRIVATE cxx_std_23)
target_include_directories(
    volumetric_lighting_tuning_policy_test
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(
    volumetric_lighting_tuning_policy_test
    PRIVATE nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(
        volumetric_lighting_tuning_policy_test
        PRIVATE /W4 /WX /permissive-
    )
endif()
add_test(NAME VolumetricLightingTuningPolicy COMMAND volumetric_lighting_tuning_policy_test)
set_tests_properties(
    VolumetricLightingTuningPolicy
    PROPERTIES TIMEOUT 30 LABELS "Host;Graphics;VolumetricLighting"
)

add_custom_target(
    run_host_tests
    COMMAND $<TARGET_FILE:performance_tuning_tests> --reporter compact
    COMMAND $<TARGET_FILE:scene_depth_test>
    COMMAND $<TARGET_FILE:unified_water_flowmap_test>
    COMMAND $<TARGET_FILE:virtual_function_hook_test>
    COMMAND $<TARGET_FILE:volumetric_lighting_blur_test>
    COMMAND $<TARGET_FILE:skylighting_settings_test>
    COMMAND $<TARGET_FILE:actor_shape_bounds_test>
    COMMAND $<TARGET_FILE:volumetric_lighting_tuning_policy_test>
    DEPENDS
        performance_tuning_tests
        scene_depth_test
        unified_water_flowmap_test
        virtual_function_hook_test
        volumetric_lighting_blur_test
        skylighting_settings_test
        actor_shape_bounds_test
        volumetric_lighting_tuning_policy_test
    WORKING_DIRECTORY $<TARGET_FILE_DIR:performance_tuning_tests>
    COMMENT "Running host-side C++ unit tests..."
    VERBATIM
)
