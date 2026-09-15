cmake_minimum_required(VERSION 3.20)
project(NRDevBenchSupportTests LANGUAGES CXX)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(support_test target name)
    add_executable(${target} "${REPO}/tests/${target}.cpp")
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_include_directories(${target} PRIVATE "${REPO}/src")
    target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

support_test(main_thread_dispatch_state_test MainThreadDispatchState)
support_test(devbench_main_thread_dispatch_test DevBenchMainThreadDispatch)
support_test(screenshot_api_policy_test ScreenshotApiPolicy)
support_test(screenshot_manifest_snapshot_test ScreenshotManifestSnapshot)
support_test(screenshot_dispatch_test ScreenshotDispatch)
support_test(screenshot_capture_present_test ScreenshotCapturePresent)
support_test(profiler_timing_test ProfilerTimingWARP)
target_sources(profiler_timing_test PRIVATE "${REPO}/src/Profiler.cpp")
target_link_libraries(profiler_timing_test PRIVATE d3d11 runtimeobject)
support_test(d3d_context_protection_test D3DContextProtectionWARP)
target_sources(d3d_context_protection_test PRIVATE "${REPO}/src/Utils/D3DContextProtection.cpp")
target_link_libraries(d3d_context_protection_test PRIVATE d3d11)

foreach(kind IN ITEMS dispatch capture_present)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${kind}")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${REPO}"
        "-DOUTPUT_DIRECTORY=${dir}" -P "${REPO}/tests/extract_screenshot_${kind}.cmake"
        COMMAND_ERROR_IS_FATAL ANY)
    target_include_directories(screenshot_${kind}_test PRIVATE "${dir}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${REPO}/src/Features/ScreenshotFeature.cpp"
        "${REPO}/src/Features/ScreenshotFeature.h"
        "${REPO}/src/Features/ScreenshotApi.cpp")
endforeach()
add_test(NAME BuildProvenance COMMAND "${Python3_EXECUTABLE}" "${REPO}/tests/build_provenance_test.py")
