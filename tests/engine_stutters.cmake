add_controller_test(engine_stutter_test EngineStutterRecorder tests/engine_stutter_test.cpp)
target_compile_definitions(engine_stutter_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
add_controller_test(engine_stutter_off_test EngineStutterOff tests/engine_stutter_off_test.cpp)
target_sources(engine_stutter_off_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/Diagnostics/EngineStutterMonitor.cpp"
    "${PROJECT_SOURCE_DIR}/src/Diagnostics/EngineStutterCapture.cpp")
add_controller_test(engine_stutter_capture_test EngineStutterCapture tests/engine_stutter_capture_test.cpp)
target_sources(engine_stutter_capture_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/Diagnostics/EngineStutterCapture.cpp"
    "${PROJECT_SOURCE_DIR}/src/Api/ServiceFoundation.cpp")
target_compile_definitions(engine_stutter_capture_test PRIVATE DEVBENCH_BRIDGE_ENABLED NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(engine_stutter_capture_test PRIVATE nlohmann_json::nlohmann_json Microsoft::CppWinRT advapi32)
set_tests_properties(EngineStutterRecorder EngineStutterCapture EngineStutterOff PROPERTIES TIMEOUT 30)
