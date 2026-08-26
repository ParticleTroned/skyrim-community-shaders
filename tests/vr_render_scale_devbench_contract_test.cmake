if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_bridge_path
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"
)
file(READ "${_bridge_path}" _bridge)

foreach(_action IN ITEMS
    qualification_status
    qualification_begin
    qualification_wait
    qualification_cancel
    dlss_trace_status
    dlss_trace_start
    dlss_trace_read
    dlss_trace_stop
    dlss_trace_reset
)
    string(FIND "${_bridge}" "if (action == \"${_action}\")" _handler_position)
    if(_handler_position EQUAL -1)
        message(FATAL_ERROR "Missing DevBench qualification handler: ${_action}")
    endif()
endforeach()

string(REGEX MATCH
    "R\"json\\((\\{[^\r\n]*\\})\\)json\""
    _descriptor_match
    "${_bridge}"
)
if(NOT _descriptor_match)
    message(FATAL_ERROR "Render-scale DevBench descriptor was not found")
endif()
set(_descriptor "${CMAKE_MATCH_1}")
string(JSON _descriptor_type ERROR_VARIABLE _descriptor_error TYPE "${_descriptor}")
if(_descriptor_error OR NOT _descriptor_type STREQUAL "OBJECT")
    message(FATAL_ERROR "Invalid render-scale DevBench descriptor JSON: ${_descriptor_error}")
endif()

foreach(_required_text IN ITEMS
    "qualification_status"
    "qualification_begin"
    "qualification_wait"
    "qualification_cancel"
    "transitionId"
    "expectedCellEditorId"
    "timeoutMs"
    "target"
    "foveation"
    "peripheryTAAEnable"
    "expectedBuildId"
)
    string(FIND "${_descriptor}" "${_required_text}" _schema_position)
    if(_schema_position EQUAL -1)
        message(FATAL_ERROR "Render-scale DevBench schema is missing: ${_required_text}")
    endif()
endforeach()

string(JSON _timeout_default ERROR_VARIABLE _timeout_error
    GET "${_descriptor}" inputSchema properties timeoutMs default
)
if(_timeout_error OR NOT _timeout_default EQUAL 120000)
    message(FATAL_ERROR
        "Render-scale qualification timeout default is invalid: ${_timeout_error}"
    )
endif()

foreach(_required_behavior IN ITEMS
    "QualificationPolicy::SaturatingDeadlineTick"
    "QualificationPolicy::EvaluateStability"
    "QualificationPolicy::HasRequiredPresentationHistory"
    "QualificationPolicy::IsFoveationInvariantViolation"
    "QualificationPolicy::OwnsTransitionInstance"
    "QualificationMonotonicRegressionsJson"
    "QueryPerformanceCounter"
    "expectedCellEditorId"
    "kElapsedMillisecondsReceiptField"
    "kElapsedFramesReceiptField"
    "BuildProvenance::ValidateExpectedBuild"
)
    string(FIND "${_bridge}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR "Render-scale qualification behavior is missing: ${_required_behavior}")
    endif()
endforeach()

message(STATUS "Render-scale DevBench qualification contract is coherent")
