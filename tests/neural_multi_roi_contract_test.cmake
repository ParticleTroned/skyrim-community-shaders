cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_upscaling_header_path "${PROJECT_ROOT}/src/Features/Upscaling.h")
set(_upscaling_source_path "${PROJECT_ROOT}/src/Features/Upscaling.cpp")
set(
    _renderer_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.h"
)
set(
    _character_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.h"
)
set(
    _character_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp"
)
set(
    _renderer_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
)
set(
    _runtime_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h"
)
set(
    _devbench_bridge_path
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"
)
foreach(_required_path IN ITEMS
    "${_upscaling_header_path}"
    "${_upscaling_source_path}"
    "${_renderer_header_path}"
    "${_character_header_path}"
    "${_character_source_path}"
    "${_renderer_source_path}"
    "${_runtime_header_path}"
    "${_devbench_bridge_path}"
)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required multi-ROI contract input is missing: ${_required_path}")
    endif()
endforeach()

file(READ "${_upscaling_header_path}" _upscaling_header)
file(READ "${_upscaling_source_path}" _upscaling_source)
file(READ "${_renderer_header_path}" _renderer_header)
file(READ "${_character_header_path}" _character_header)
file(READ "${_character_source_path}" _character_source)
file(READ "${_renderer_source_path}" _renderer_source)
file(READ "${_runtime_header_path}" _runtime_header)
file(READ "${_devbench_bridge_path}" _devbench_bridge)
set(
    _contract_text
    "${_upscaling_header}\n${_upscaling_source}\n${_renderer_header}\n${_character_header}\n${_character_source}\n${_renderer_source}\n${_runtime_header}"
)

foreach(_settings_contract IN ITEMS
    [[bool neuralCharacterMultiRoiEnabled = false;]]
    [[OP(neuralCharacterMultiRoiEnabled)]]
    [[settings.neuralCharacterMultiRoiEnabled = false;]]
    [[o_json.erase("neuralCharacterMultiRoiEnabled");]]
    [[.multiRoi = a_settings.neuralCharacterMultiRoiEnabled,]]
    [[add(a_settings.neuralCharacterMultiRoiEnabled);]]
)
    string(FIND "${_contract_text}" "${_settings_contract}"
        _settings_contract_position)
    if(_settings_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI persistence/default/key contract is missing: ${_settings_contract}"
        )
    endif()
endforeach()

foreach(_telemetry_contract IN ITEMS
    [[rendererSnapshot.performance.lastFeatureLogicalEyeCount]]
    [=[attributedPreparation->computeRegionCounts[featureSlot]]=]
    [["physicalSlotMask"]]
    [["logicalSlotMask"]]
    [["computeRegionCount"]]
    [["computeRegions"]]
    [["multiRoiPixels"]]
    [["multiRoiFallback"]]
    [["multiRoiReason"]]
)
    string(FIND "${_devbench_bridge}" "${_telemetry_contract}"
        _telemetry_contract_position)
    if(_telemetry_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI DevBench attribution contract is missing: ${_telemetry_contract}"
        )
    endif()
endforeach()

foreach(_prepare_contract IN ITEMS
    [[CharacterComputeRegionPlan computeRegions{};]]
    [[a_args.computeRegions = {};]]
    [[a_args.computeRegions = result.computeRegions;]]
    [[GetPreparedComputeRegions(]]
    [[FindPreparedSlot(]]
)
    string(FIND "${_contract_text}" "${_prepare_contract}"
        _prepare_contract_position)
    if(_prepare_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI prepare/result threading contract is missing: ${_prepare_contract}"
        )
    endif()
endforeach()

foreach(_execution_contract IN ITEMS
    [[static constexpr std::size_t kFeatureSlotCount = 8;]]
    [[PhysicalRegionFeatureSlot(logical.featureSlot, region)]]
    [[physical.computeSubrect = plan.regions[region];]]
    [[resources[index].historyKey.regionIdentity = regionIdentities[index];]]
    [[.logicalEyeCount = logicalEyeCount,]]
    [[AggregateRegionEvaluationMask(]]
)
    string(FIND "${_contract_text}" "${_execution_contract}"
        _execution_contract_position)
    if(_execution_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI physical-evaluation contract is missing: ${_execution_contract}"
        )
    endif()
endforeach()

foreach(_copy_contract IN ITEMS
    [[bool CopyNeuralOutputRegions(]]
    [[a_regions.count > 2u]]
    [[ContainsComputeSubrect(a_enclosure, region)]]
    [[GetPreparedComputeRegions(]]
    [[computeSubrect, computeRegions);]]
    [[preparedSubrect, preparedRegions);]]
    [[computeSubrect, neuralArgs[eye].computeRegions))]]
)
    string(FIND "${_upscaling_source}" "${_copy_contract}"
        _copy_contract_position)
    if(_copy_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI defined-output copy contract is missing: ${_copy_contract}"
        )
    endif()
endforeach()

string(REGEX MATCHALL [[CopyNeuralOutputRegions\(]]
    _copy_helper_occurrences "${_upscaling_source}")
list(LENGTH _copy_helper_occurrences _copy_helper_occurrence_count)
if(NOT _copy_helper_occurrence_count EQUAL 4)
    message(FATAL_ERROR
        "Expected one exact-region copy helper plus its three staged-output callers; found ${_copy_helper_occurrence_count} occurrences"
    )
endif()

foreach(_ui_contract IN ITEMS
    [["Experimental Multi-ROI"]]
    [[&settings.neuralCharacterMultiRoiEnabled]]
    [[Uses at most two persistent Feature 18 regions per eye]]
    [[Multi-ROI uses separate feature instances.]]
)
    string(FIND "${_upscaling_source}" "${_ui_contract}"
        _ui_contract_position)
    if(_ui_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI user-facing contract is missing: ${_ui_contract}"
        )
    endif()
endforeach()

string(FIND "${_upscaling_source}"
    [[bool Upscaling::HandleNeuralRenderingSettingsTransition(]]
    _settings_transition_begin)
string(FIND "${_upscaling_source}"
    [[Upscaling::GetLatchedNeuralRenderingInsertionPoint()]]
    _settings_transition_end)
if(_settings_transition_begin EQUAL -1 OR _settings_transition_end EQUAL -1 OR
   _settings_transition_end LESS_EQUAL _settings_transition_begin)
    message(FATAL_ERROR "Unable to isolate the Neural Rendering settings transition")
endif()
math(EXPR _settings_transition_length
    "${_settings_transition_end} - ${_settings_transition_begin}")
string(SUBSTRING "${_upscaling_source}" ${_settings_transition_begin}
    ${_settings_transition_length} _settings_transition)
string(REGEX REPLACE "[\r\n\t ]+" " "
    _settings_transition_normalized "${_settings_transition}")

foreach(_transition_contract IN ITEMS
    [[const bool multiRoiChanged = a_previousSettings.neuralCharacterMultiRoiEnabled != settings.neuralCharacterMultiRoiEnabled;]]
    [[if (!multiRoiChanged && HasSameNeuralRenderingSettingsKey(a_previousSettings, settings))]]
    [[RequestHistoryReset();]]
    [[!insertionPointChanged && !multiRoiChanged]]
    [[NeuralRendering::Renderer::Instance().Reset();]]
)
    string(FIND "${_settings_transition_normalized}" "${_transition_contract}"
        _transition_contract_position)
    if(_transition_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Multi-ROI reset-on-toggle contract is missing: ${_transition_contract}"
        )
    endif()
endforeach()

string(FIND "${_settings_transition_normalized}"
    [[const bool multiRoiChanged =]] _multi_roi_changed_position)
string(FIND "${_settings_transition_normalized}"
    [[if (!multiRoiChanged && HasSameNeuralRenderingSettingsKey]]
    _same_key_early_return_position)
string(FIND "${_settings_transition_normalized}"
    [[NeuralRendering::Renderer::Instance().Reset();]]
    _renderer_reset_position)
if(NOT _multi_roi_changed_position LESS _same_key_early_return_position OR
   NOT _same_key_early_return_position LESS _renderer_reset_position)
    message(FATAL_ERROR
        "Multi-ROI toggle detection must precede early returns and force renderer retirement"
    )
endif()

message(STATUS "Neural multi-ROI contract passed")
