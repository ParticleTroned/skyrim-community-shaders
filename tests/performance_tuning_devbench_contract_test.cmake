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
    "start_upscaling_sweep"
    "cancel"
    "matrix"
    "nvidia"
    "amd"
    "dlssPreset"
    "traceAfterSequence"
    "maximumTraceSamples"
    "expectedBuildId"
)
    string(FIND "${_descriptor}" "${_schema_contract}" _schema_position)
    if(_schema_position EQUAL -1)
        message(FATAL_ERROR
            "Performance-tuning schema is missing: ${_schema_contract}"
        )
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
    "PerformanceTuningRenderer::StartDevBenchUpscalingCostSweep(matrix, dlssPreset)"
    "PerformanceTuningRenderer::GetDevBenchMeasurementStatus("
    "PerformanceTuningRenderer::CancelDevBenchMeasurements()"
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
    "constexpr double kFeatureCostInitialWaitSeconds = 5.0"
    "constexpr double kFeatureCostComparisonWaitSeconds = 9.0"
    "constexpr double kFeatureCostRestartCooldownSeconds = 10.0"
    "constexpr double kFeatureCostTraceIntervalSeconds = 0.1"
    "kFeatureCostMeasurementBlockCount == 5"
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

message(STATUS "Performance-tuning DevBench contract is coherent")
