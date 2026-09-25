add_controller_test(
    profiler_capture_test
    ProfilerCapture
    tests/profiler_capture_test.cpp
)
target_sources(profiler_capture_test PRIVATE src/Profiler.cpp)
target_include_directories(
    profiler_capture_test
    BEFORE
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/profiler_stubs"
)
