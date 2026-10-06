include_guard(GLOBAL)

function(csx_add_neural_rendering_capture_tests repository_root register_test)
    set(targets
        screenshot_neural_evidence_test
        screenshot_neural_diagnostics_test
        neural_rendering_request_test
        neural_feature_settings_test
        neural_rendering_ui_test
        neural_settings_key_test
        neural_transaction_evidence_test
    )
    set(test_names
        ScreenshotNeuralEvidence
        ScreenshotNeuralDiagnostics
        NeuralRenderingRequest
        NeuralFeatureSettings
        NeuralRenderingUI
        NeuralSettingsKey
        NeuralTransactionEvidence
    )
    foreach(target test_name IN ZIP_LISTS targets test_names)
        cmake_language(CALL ${register_test}
            ${target} ${test_name} "${repository_root}/tests/${target}.cpp")
        target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endforeach()

    foreach(target IN ITEMS neural_rendering_ui_test neural_feature_settings_test)
        target_compile_definitions(${target} PRIVATE DEVBENCH_BRIDGE_ENABLED)
    endforeach()

    cmake_language(CALL ${register_test} neural_feature_settings_off_test NeuralFeatureSettings_off
        "${repository_root}/tests/neural_feature_settings_test.cpp")
    target_link_libraries(neural_feature_settings_off_test PRIVATE nlohmann_json::nlohmann_json)
    target_include_directories(neural_feature_settings_off_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/neural_feature_settings")

    cmake_language(CALL ${register_test}
        neural_transaction_devbench_evidence_test NeuralTransactionDevBenchEvidence
        "${repository_root}/tests/neural_transaction_evidence_test.cpp")
    target_link_libraries(neural_transaction_devbench_evidence_test PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(neural_transaction_devbench_evidence_test PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN DEVBENCH_BRIDGE_ENABLED)
    set_tests_properties(NeuralTransactionDevBenchEvidence PROPERTIES TIMEOUT 30)

    cmake_language(CALL ${register_test} neural_controller_toggle_test NeuralControllerToggle
        "${repository_root}/tests/neural_controller_toggle_test.cpp")
    target_include_directories(neural_controller_toggle_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/neural_rendering_ui")

    target_sources(screenshot_neural_diagnostics_test PRIVATE
        "${repository_root}/src/Features/ScreenshotNeuralDiagnostics.cpp")

    foreach(kind IN ITEMS neural_rendering_request neural_feature_settings neural_rendering_ui neural_settings_key)
        set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/${kind}")
        set(extractor "${repository_root}/tests/extract_${kind}.cmake")
        execute_process(COMMAND "${CMAKE_COMMAND}"
            "-DPROJECT_ROOT=${repository_root}"
            "-DOUTPUT_DIRECTORY=${output_directory}" -P "${extractor}"
            COMMAND_ERROR_IS_FATAL ANY)
        target_include_directories(${kind}_test PRIVATE "${output_directory}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${extractor}")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${repository_root}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"
        "${repository_root}/src/Features/NeuralRenderingFeature.cpp"
        "${repository_root}/src/Features/Upscaling.cpp"
        "${repository_root}/src/Features/Upscaling.h"
        "${repository_root}/src/Features/VR/Input.cpp"
        "${repository_root}/src/State.cpp")
endfunction()
