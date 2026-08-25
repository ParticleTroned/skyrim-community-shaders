cmake_minimum_required(VERSION 4.2)

if(NOT TARGET Catch2::Catch2)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

add_executable(
    performance_tuning_tests
    main.cpp
    light_limit_fix_shadow_policy_tests.cpp
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
    COMMAND $<TARGET_FILE:performance_tuning_tests> --reporter compact
    DEPENDS performance_tuning_tests
    WORKING_DIRECTORY $<TARGET_FILE_DIR:performance_tuning_tests>
    COMMENT "Running host-side C++ unit tests..."
    VERBATIM
)
