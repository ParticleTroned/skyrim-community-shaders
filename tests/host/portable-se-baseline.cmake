# Include from a C++ project after enable_testing(). The existing Windows host
# target already covers this inventory; standalone portable builds add it here.
include_guard(GLOBAL)
if(TARGET performance_tuning_tests OR TARGET se_baseline_tests)
    return()
endif()
get_filename_component(CSX_SE_BASELINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET Catch2::Catch2)
    find_package(Catch2 3 CONFIG QUIET)
endif()
option(CSX_BASELINE_FETCH_CATCH2 "Fetch pinned Catch2 3 for existing SE baseline tests" OFF)
if(NOT TARGET Catch2::Catch2 AND CSX_BASELINE_FETCH_CATCH2)
    include(FetchContent)
    FetchContent_Declare(Catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.8.1
        URL_HASH SHA256=18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(Catch2)
endif()
if(NOT TARGET Catch2::Catch2)
    message(FATAL_ERROR
        "SE baseline tests require genuine Catch2 3: provide Catch2_DIR or set "
        "CSX_BASELINE_FETCH_CATCH2=ON. An offline source can be supplied with "
        "FETCHCONTENT_SOURCE_DIR_CATCH2.")
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.12 CONFIG QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_path(CSX_SE_BASELINE_JSON_INCLUDE nlohmann/json.hpp
        HINTS "${CSX_JSON_INCLUDE}" REQUIRED)
    if(NOT EXISTS "${CSX_SE_BASELINE_JSON_INCLUDE}/nlohmann/json_fwd.hpp")
        message(FATAL_ERROR "SE baseline JSON tests require both official json.hpp and json_fwd.hpp")
    endif()
    add_library(csx_se_baseline_json INTERFACE)
    target_include_directories(csx_se_baseline_json INTERFACE "${CSX_SE_BASELINE_JSON_INCLUDE}")
    add_library(nlohmann_json::nlohmann_json ALIAS csx_se_baseline_json)
endif()

add_executable(se_baseline_tests
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dlssg_resource_retention_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_generation_eligibility_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/light_limit_fix_shadow_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/performance_tuning_measurement_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vanity_camera_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/performance_tuning_json_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_compilation_scheduling_policy_tests.cpp"
    "${CSX_SE_BASELINE_ROOT}/src/Menu/PerformanceTuningJson.cpp"
    "${CSX_SE_BASELINE_ROOT}/src/Menu/PerformanceTuningMeasurement.cpp")
target_compile_features(se_baseline_tests PRIVATE cxx_std_23)
target_include_directories(se_baseline_tests PRIVATE "${CSX_SE_BASELINE_ROOT}/src")
target_link_libraries(se_baseline_tests PRIVATE Catch2::Catch2 nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(se_baseline_tests PRIVATE /W4 /WX /permissive- /UNDEBUG)
else()
    target_compile_options(se_baseline_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()

add_test(NAME SEBaseline COMMAND se_baseline_tests --reporter compact)
set_tests_properties(SEBaseline PROPERTIES TIMEOUT 30 LABELS "SEPort;Host;UnitTests;SEBaseline")
list(APPEND CSX_PORTABLE_TEST_TARGETS se_baseline_tests)
