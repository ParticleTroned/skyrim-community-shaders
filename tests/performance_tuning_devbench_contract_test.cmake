if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ
    "${PROJECT_ROOT}/src/PerformanceTuningDevBenchBridge.cpp"
    _bridge
)
file(READ
    "${PROJECT_ROOT}/src/Menu/PerformanceTuningRenderer.cpp"
    _renderer
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling.h"
    _upscaling_header
)
file(READ "${PROJECT_ROOT}/src/XSEPlugin.cpp" _plugin)

string(REGEX MATCH
    "R\"json\\((\\{[^\r\n]*\\})\\)json\""
    _descriptor_match
    "${_bridge}"
)
if(NOT _descriptor_match)
    message(FATAL_ERROR "Performance-tuning DevBench descriptor was not found")
endif()
set(_descriptor "${CMAKE_MATCH_1}")
string(JSON _descriptor_type ERROR_VARIABLE _descriptor_error TYPE "${_descriptor}")
if(_descriptor_error OR NOT _descriptor_type STREQUAL "OBJECT")
    message(FATAL_ERROR
        "Invalid performance-tuning descriptor JSON: ${_descriptor_error}"
    )
endif()

foreach(_schema_contract IN ITEMS
    "start_quick_scan"
    "invalidGpuFrames"
    "invalidCpuFrames"
    "renderConfiguration"
    "start_feature_cost"
    "start_feature_costs"
    "set_feature_enabled"
    "enabled"
    "start_upscaling_sweep"
    "cancel"
    "featureShortName"
    "matrix"
    "nvidia"
    "amd"
    "dlssPreset"
    "traceAfterSequence"
    "maximumTraceSamples"
    "expectedBuildId"
    "six 500 ms current windows"
    "three-second Off settling (ten seconds for Upscaling None)"
    "five-second inter-feature cooldown"
    "five-second inter-case cooldown"
    "measurementIntervalMs"
    "measurementBlockCount"
    "upscalingComparisonWaitMs"
    "restoreWaitMs"
    "postRunCooldownMs"
    "expectedRunMs"
    "active measurement and availableFeatureCosts report the actual comparisonWaitMs"
)
    string(FIND "${_descriptor}" "${_schema_contract}" _schema_position)
    if(_schema_position EQUAL -1)
        message(FATAL_ERROR
            "Performance-tuning schema is missing: ${_schema_contract}"
        )
    endif()
endforeach()

string(JSON _toggle_type GET
    "${_descriptor}" inputSchema properties enabled type
)
if(NOT _toggle_type STREQUAL "boolean")
    message(FATAL_ERROR "Runtime feature toggles require a boolean enabled value")
endif()
string(JSON _action_count LENGTH
    "${_descriptor}" inputSchema properties action enum
)
set(_registered_actions)
math(EXPR _action_last "${_action_count} - 1")
foreach(_index RANGE 0 ${_action_last})
    string(JSON _action GET
        "${_descriptor}" inputSchema properties action enum ${_index}
    )
    list(APPEND _registered_actions "${_action}")
endforeach()
foreach(_required_action IN ITEMS start_quick_scan start_feature_costs set_feature_enabled)
    list(FIND _registered_actions "${_required_action}" _required_action_index)
    if(_required_action_index EQUAL -1)
        message(FATAL_ERROR "Missing registered tuning action: ${_required_action}")
    endif()
endforeach()

set(_excluded_features WeatherPicker PerformanceOverlay Screenshot UnifiedWater)
string(JSON _excluded_count LENGTH
    "${_descriptor}" inputSchema properties featureShortName not enum
)
list(LENGTH _excluded_features _expected_excluded_count)
if(NOT _excluded_count EQUAL _expected_excluded_count)
    message(FATAL_ERROR "Performance-tuning schema has unexpected exclusions")
endif()
math(EXPR _excluded_last "${_excluded_count} - 1")
foreach(_index RANGE 0 ${_excluded_last})
    string(JSON _excluded_feature GET
        "${_descriptor}" inputSchema properties featureShortName not enum ${_index}
    )
    list(FIND _excluded_features "${_excluded_feature}" _excluded_position)
    if(_excluded_position EQUAL -1)
        message(FATAL_ERROR "Unexpected tuning exclusion: ${_excluded_feature}")
    endif()
    list(REMOVE_ITEM _excluded_features "${_excluded_feature}")
    set(_feature_file "${_excluded_feature}")
    if(_feature_file STREQUAL "Screenshot")
        set(_feature_file "ScreenshotFeature")
    endif()
    file(READ "${PROJECT_ROOT}/src/Features/${_feature_file}.h" _feature_header)
    file(READ "${PROJECT_ROOT}/src/Features/${_feature_file}.cpp" _feature_source)
    if("${_feature_header}${_feature_source}" MATCHES
        "SupportsPerformanceCostMeasurement|IsPerformanceCostMeasurement|SetPerformanceCostMeasurement|GetPerformanceCostMeasurement|IsPerformanceToggleEnabled|HasPerformanceSettings|DrawPerformanceSettings|CapturePerformanceSettingsState")
        message(FATAL_ERROR "${_excluded_feature} still has a tuning adapter")
    endif()
    string(FIND "${_renderer}" "\"${_excluded_feature}\"" _feature_position)
    if(NOT _feature_position EQUAL -1)
        message(FATAL_ERROR "Tuning still registers ${_excluded_feature}")
    endif()
endforeach()

string(JSON _preset_count LENGTH
    "${_descriptor}" inputSchema properties dlssPreset enum
)
if(NOT _preset_count EQUAL 6)
    message(FATAL_ERROR "DLSS prompt must expose all six supported profiles")
endif()
set(_expected_presets J K L M F E)
math(EXPR _preset_last "${_preset_count} - 1")
foreach(_index RANGE 0 ${_preset_last})
    string(JSON _preset GET
        "${_descriptor}" inputSchema properties dlssPreset enum ${_index}
    )
    list(GET _expected_presets ${_index} _expected_preset)
    if(NOT _preset STREQUAL _expected_preset)
        message(FATAL_ERROR
            "DLSS profile ${_index} must be ${_expected_preset}, got ${_preset}"
        )
    endif()
endforeach()

foreach(_bridge_contract IN ITEMS
    "communityshaders.performance_tuning"
    "BuildProvenance::ValidateExpectedBuild(args)"
    "BuildProvenance::AttachProducer(output)"
    "RunOnMainThread"
    "PerformanceTuningRenderer::StartDevBenchFeatureCostMeasurement(featureShortName)"
    "PerformanceTuningRenderer::StartDevBenchUpscalingCostSweep(matrix, dlssPreset)"
    "PerformanceTuningRenderer::GetDevBenchMeasurementStatus("
    "PerformanceTuningRenderer::CancelDevBenchMeasurements()"
    "PerformanceTuningRenderer::StartDevBenchFeatureCostBatch()"
    "PerformanceTuningRenderer::StartDevBenchQuickScan()"
    "PerformanceTuningRenderer::SetDevBenchFeatureEnabled(featureShortName, enabled)"
)
    string(FIND "${_bridge}" "${_bridge_contract}" _bridge_position)
    if(_bridge_position EQUAL -1)
        message(FATAL_ERROR
            "Performance-tuning bridge is missing: ${_bridge_contract}"
        )
    endif()
endforeach()

foreach(_forbidden_startup_mutation IN ITEMS
    "MenuDevBenchBridge"
    "prepare_coc"
    "SetLogLevel("
    "settings.foveatedVendorDispatch ="
    "cameraFOV"
)
    string(FIND
        "${_bridge}"
        "${_forbidden_startup_mutation}"
        _forbidden_startup_mutation_position
    )
    if(NOT _forbidden_startup_mutation_position EQUAL -1)
        message(FATAL_ERROR
            "Performance-tuning bridge contains unrelated startup mutation: ${_forbidden_startup_mutation}"
        )
    endif()
endforeach()

foreach(_renderer_contract IN ITEMS
    "constexpr double kFeatureCostMeasurementSeconds = 3.0"
    "constexpr double kFeatureCostIntervalMilliseconds = 500.0"
    "constexpr double kFeatureCostInitialWaitSeconds = 2.0"
    "constexpr double kFeatureCostComparisonWaitSeconds = 3.0"
    "constexpr double kFeatureCostRestoreWaitSeconds = 1.0"
    "constexpr double kFeatureCostRestartCooldownSeconds = 5.0"
    "constexpr double kFeatureCostTraceIntervalSeconds = 0.1"
    "kFeatureCostMeasurementBlockCount == 6"
    "g_quickScan.controller.phase == PerformanceQuickScan::Phase::Complete"
    "scan.controller.Poll(now, QuickScanReady(), CaptureQuickScanRenderConfiguration())"
    "BuildNvidiaUpscalingCostSweepCases"
    "BuildAmdUpscalingCostSweepCases"
    "for (const bool fsr4RuntimeEnabled : { false, true })"
    "fidelityFX.IsRuntimeFsr4Available()"
    "GetDLSSPresetChoicesJson()"
    "response[\"promptRequired\"] = true"
    "response[\"allowedDlssPresets\"] = GetDLSSPresetChoicesJson()"
    "Upscaling::TryParseDLSSPresetName(a_dlssPreset, dlssPreset)"
    "GetFeatureCostRestartCooldownRemaining(currentTime)"
    "CaptureUpscalingCostSweepReadiness(currentTime)"
    "IsFsr4UpscalingCostSweepAvailable()"
    "IsUpscalingCostSweepFsr4ProviderReady"
    "Upscaling::ResolvePerformanceCostMeasurementMethod("
    "IsUpscalingCostSweepStateSelected(sweep.originalState)"
    "g_upscalingCostSweep.mainMenuWasOpen"
    "g_upscalingCostSweep.editorWasOpen"
    "RecordFeatureCostTrace("
    "StartDevBenchFeatureCostMeasurement("
    "\"availableFeatureCosts\""
    "\"featureResults\""
    "\"featureBatch\""
    "\"costPercent\""
    "\"costPercentBasis\""
    "\"measurementEnabled\""
    "\"toggleBlockReason\""
    "\"devbench_feature_cost\""
    "\"relativeTo\", \"none\""
    "\"frameMs\""
    "\"gameGpuMs\""
    "\"gameCpuMs\""
)
    string(FIND "${_renderer}" "${_renderer_contract}" _renderer_position)
    if(_renderer_position EQUAL -1)
        message(FATAL_ERROR
            "Performance-tuning renderer is missing: ${_renderer_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL
    "cases\\.reserve\\(15\\)"
    _matrix_case_reservations
    "${_renderer}"
)
list(LENGTH _matrix_case_reservations _matrix_case_reservation_count)
if(NOT _matrix_case_reservation_count EQUAL 2)
    message(FATAL_ERROR
        "NVIDIA and AMD sweeps must each reserve exactly 15 cases"
    )
endif()

foreach(_preset_contract IN ITEMS
    "GetDLSSPresetName"
    "TryParseDLSSPresetName"
    "ResolvePerformanceCostMeasurementMethod"
    "case 'J':"
    "case 'K':"
    "case 'L':"
    "case 'M':"
    "case 'F':"
    "case 'E':"
)
    string(FIND
        "${_upscaling_header}"
        "${_preset_contract}"
        _preset_contract_position
    )
    if(_preset_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Shared DLSS profile mapping is missing: ${_preset_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_plugin}"
    "PerformanceTuningDevBenchBridge::Install();"
    _install_position
)
if(_install_position EQUAL -1)
    message(FATAL_ERROR "Performance-tuning DevBench bridge is not installed")
endif()

function(assert_performance_feature_contract _relative_path _contract)
    file(READ "${PROJECT_ROOT}/${_relative_path}" _source)
    string(FIND "${_source}" "${_contract}" _contract_position)
    if(_contract_position EQUAL -1)
        message(FATAL_ERROR
            "${_relative_path} is missing performance contract: ${_contract}"
        )
    endif()
endfunction()

foreach(_feature_short_name IN ITEMS
    LinearLighting
    CloudShadows
    VolumetricShadows
    TruePBR
    ExtendedMaterials
    FoliageLighting
    GrassOptimizations
)
    string(FIND "${_renderer}" "\"${_feature_short_name}\"" _feature_position)
    if(_feature_position EQUAL -1)
        message(FATAL_ERROR
            "Performance Tuning does not surface ${_feature_short_name}"
        )
    endif()
endforeach()

foreach(_feature_file IN ITEMS
    "src/Features/LinearLighting"
    "src/Features/CloudShadows"
    "src/Features/VolumetricShadows"
    "src/Features/ExtendedMaterials"
    "src/Features/FoliageLighting"
    "src/Features/GrassOptimizations"
    "src/TruePBR"
)
    assert_performance_feature_contract(
        "${_feature_file}.h"
        "HasPerformanceSettings() const override { return true; }"
    )
    assert_performance_feature_contract(
        "${_feature_file}.h"
        "SupportsPerformanceCostMeasurement() const override { return true; }"
    )
    get_filename_component(_feature_type "${_feature_file}" NAME)
    assert_performance_feature_contract(
        "${_feature_file}.cpp"
        "void ${_feature_type}::DrawPerformanceSettings(bool"
    )
    assert_performance_feature_contract(
        "${_feature_file}.cpp"
        "json ${_feature_type}::CapturePerformanceSettingsState() const"
    )
endforeach()

assert_performance_feature_contract(
    "src/Features/ExtendedMaterials.cpp"
    "SanitizeSettings(settings);"
)
assert_performance_feature_contract(
    "src/Features/ExtendedMaterials.cpp"
    "settings.EnableParallaxWarpingFix"
)
assert_performance_feature_contract(
    "src/Features/CloudShadows.cpp"
    "IsCloudShadowSceneReady()"
)
assert_performance_feature_contract(
    "src/Features/VolumetricShadows.cpp"
    "HasActiveDirectionalShadows()"
)
foreach(_foliage_performance_setting IN ITEMS
    "DrawFoliageScatteringSetting();"
    "DrawFoliageAmbientBoostSetting(truePBRActive);"
    "DrawFoliageAmbientFlipSetting();"
    "DrawGrassScatteringSetting();"
)
    assert_performance_feature_contract(
        "src/Features/FoliageLighting.cpp"
        "${_foliage_performance_setting}"
    )
endforeach()
assert_performance_feature_contract(
    "src/Features/FoliageLighting.cpp"
    "settings.EnableFoliageAmbientBoost != 0 && IsTruePBRActive()"
)
assert_performance_feature_contract(
    "src/Utils/UI.cpp"
    "bool UIntCheckbox(const char* a_label, unsigned int& a_value)"
)

message(STATUS "Performance-tuning DevBench contract is coherent")
