include_guard(GLOBAL)

function(csx_add_neural_rendering_capture_tests repository_root register_test)
    set(targets
        screenshot_neural_evidence_test
        screenshot_neural_diagnostics_test
        neural_rendering_request_test
        neural_feature_settings_test
    )
    set(test_names
        ScreenshotNeuralEvidence
        ScreenshotNeuralDiagnostics
        NeuralRenderingRequest
        NeuralFeatureSettings
    )
    foreach(target test_name IN ZIP_LISTS targets test_names)
        cmake_language(CALL ${register_test}
            ${target} ${test_name} "${repository_root}/tests/${target}.cpp")
        target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endforeach()

    target_sources(screenshot_neural_diagnostics_test PRIVATE
        "${repository_root}/src/Features/ScreenshotNeuralDiagnostics.cpp")

    foreach(kind IN ITEMS neural_rendering_request neural_feature_settings)
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
        "${repository_root}/src/Features/NeuralRenderingFeature.cpp")
endfunction()
