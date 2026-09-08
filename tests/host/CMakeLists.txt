cmake_minimum_required(VERSION 4.2)

if(NOT TARGET Catch2::Catch2)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

add_executable(
    performance_tuning_tests
    main.cpp
    frame_generation_eligibility_policy_tests.cpp
    light_limit_fix_shadow_policy_tests.cpp
    neural_rendering_pipeline_policy_tests.cpp
    performance_tuning_measurement_tests.cpp
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

add_custom_target(
    run_host_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L Host --output-on-failure
    DEPENDS performance_tuning_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running host-side C++ unit tests..."
    VERBATIM
)

# Standalone character policies stay independent of the game and vendor runtime.
set(CHARACTER_POLICY_TESTS
    compute_subrect
    character_actor_policy
    character_region_policy
    character_multi_roi
    character_mask_work_policy
    character_settings
)
foreach(policy IN LISTS CHARACTER_POLICY_TESTS)
    add_executable(${policy}_test ${policy}_test.cpp)
    target_compile_features(${policy}_test PRIVATE cxx_std_23)
    target_include_directories(${policy}_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(${policy}_test PRIVATE /W4 /WX /permissive-)
    endif()
    add_dependencies(run_host_tests ${policy}_test)
    add_test(NAME ${policy} COMMAND ${policy}_test)
    set_tests_properties(${policy} PROPERTIES LABELS "Host;UnitTests;NeuralRendering" TIMEOUT 30)
endforeach()

target_link_libraries(character_settings_test PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
    add_executable(character_mask_gpu_test character_mask_gpu_test.cpp)
    target_compile_features(character_mask_gpu_test PRIVATE cxx_std_23)
    target_link_libraries(character_mask_gpu_test PRIVATE d3d11 d3dcompiler)
    add_test(NAME character_mask_gpu COMMAND character_mask_gpu_test
        "${CMAKE_SOURCE_DIR}/package/Shaders"
        "${CMAKE_SOURCE_DIR}/features/Upscaling/Shaders/Upscaling/NeuralRendering/CompositeCharactersCS.hlsl")
    set_tests_properties(character_mask_gpu PROPERTIES LABELS "GPU;NeuralRendering" TIMEOUT 120)
endif()
