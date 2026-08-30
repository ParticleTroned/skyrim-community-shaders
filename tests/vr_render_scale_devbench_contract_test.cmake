if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_bridge_path
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"
)
file(READ "${_bridge_path}" _bridge)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling/VRRenderScaleReplacementTelemetryPolicy.h"
    _replacement_policy
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling.h"
    _upscaling_header
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling.cpp"
    _upscaling_source
)
file(READ
    "${PROJECT_ROOT}/src/State.cpp"
    _state_source
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.h"
    _streamline_header
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.cpp"
    _streamline_source
)

foreach(_action IN ITEMS
    qualification_status
    qualification_begin
    qualification_dispatch
    qualification_wait
    qualification_cancel
    cpu_performance_status
    cpu_performance_start
    cpu_performance_stop
    cpu_performance_reset
	gpu_performance_status
	gpu_performance_start
	gpu_performance_stop
	gpu_performance_reset
    dlss_trace_status
    dlss_trace_start
    dlss_trace_read
    dlss_trace_stop
    dlss_trace_reset
    stop
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
    "ownerId"
	"startPerformanceTelemetry"
    "expectedSessionId"
    "expectedStartFrame"
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

string(JSON _target_method_count ERROR_VARIABLE _target_method_error
    LENGTH "${_descriptor}" inputSchema properties target properties method enum
)
if(_target_method_error)
    message(FATAL_ERROR
        "Render-scale target method enum is invalid: ${_target_method_error}"
    )
endif()
math(EXPR _target_method_last "${_target_method_count} - 1")
foreach(_native_target_method IN ITEMS none taa)
    set(_native_target_found FALSE)
    foreach(_target_method_index RANGE 0 ${_target_method_last})
        string(JSON _target_method GET
            "${_descriptor}"
            inputSchema properties target properties method enum
            ${_target_method_index}
        )
        if(_target_method STREQUAL _native_target_method)
            set(_native_target_found TRUE)
        endif()
    endforeach()
    if(NOT _native_target_found)
        message(FATAL_ERROR
            "Render-scale target schema is missing native method: ${_native_target_method}"
        )
    endif()
endforeach()

foreach(_milestone_contract IN ITEMS
    "descriptor[\"inputSchema\"][\"properties\"][\"milestone\"]"
    "json::array({ \"strict\", \"presentation\", \"cleanup\" })"
    "{ \"default\", \"strict\" }"
    "QualificationMilestone milestone = QualificationMilestone::Strict"
    "invalid_milestone"
)
    string(FIND "${_bridge}" "${_milestone_contract}" _milestone_position)
    if(_milestone_position EQUAL -1)
        message(FATAL_ERROR
            "Render-scale milestone contract is missing: ${_milestone_contract}"
        )
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

string(JSON _timeout_description ERROR_VARIABLE _timeout_description_error
    GET "${_descriptor}" inputSchema properties timeoutMs description
)
foreach(_timeout_contract IN ITEMS
    "Maximum qualification deadline"
    "returns immediately"
)
    string(FIND "${_timeout_description}" "${_timeout_contract}" _timeout_contract_position)
    if(_timeout_description_error OR _timeout_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Render-scale timeout contract is ambiguous: ${_timeout_description_error}"
        )
    endif()
endforeach()

string(JSON _target_description ERROR_VARIABLE _target_description_error
    GET "${_descriptor}" inputSchema properties target description
)
string(FIND
    "${_target_description}"
    "Omit it for an externally owned selection"
    _target_observation_position
)
if(_target_description_error OR _target_observation_position EQUAL -1)
    message(FATAL_ERROR
        "Render-scale target does not document external observation mode: ${_target_description_error}"
    )
endif()

string(JSON _start_performance_default ERROR_VARIABLE _start_performance_error
    GET "${_descriptor}" inputSchema properties startPerformanceTelemetry default
)
if(_start_performance_error OR _start_performance_default)
    message(FATAL_ERROR
        "Render-scale dispatch telemetry default is invalid: ${_start_performance_error}"
    )
endif()

string(FIND
    "${_bridge}"
    "descriptor[\"inputSchema\"][\"properties\"][\"cocCellEditorId\"]"
    _coc_cell_schema_position
)
string(FIND
    "${_bridge}"
    "same main-thread operation"
    _coc_cell_timing_position
)
if(_coc_cell_schema_position EQUAL -1 OR _coc_cell_timing_position EQUAL -1)
    message(FATAL_ERROR
        "Render-scale COC command timing schema is incomplete"
    )
endif()

string(JSON _expected_session_minimum ERROR_VARIABLE _expected_session_error
    GET "${_descriptor}" inputSchema properties expectedSessionId minimum
)
if(_expected_session_error OR NOT _expected_session_minimum EQUAL 1)
    message(FATAL_ERROR
        "Render-scale expectedSessionId minimum is invalid: ${_expected_session_error}"
    )
endif()

string(JSON _expected_session_description ERROR_VARIABLE _expected_session_description_error
    GET "${_descriptor}" inputSchema properties expectedSessionId description
)
string(FIND
    "${_expected_session_description}"
    "cpu_performance_stop"
    _expected_session_cpu_position
)
if(_expected_session_description_error OR _expected_session_cpu_position EQUAL -1)
    message(FATAL_ERROR
        "Render-scale expectedSessionId does not document CPU ownership: ${_expected_session_description_error}"
    )
endif()

string(JSON _expected_start_frame_minimum ERROR_VARIABLE _expected_start_frame_error
    GET "${_descriptor}" inputSchema properties expectedStartFrame minimum
)
if(_expected_start_frame_error OR NOT _expected_start_frame_minimum EQUAL 0)
    message(FATAL_ERROR
        "Render-scale expectedStartFrame minimum is invalid: ${_expected_start_frame_error}"
    )
endif()

string(JSON _expected_start_frame_description ERROR_VARIABLE _expected_start_frame_description_error
    GET "${_descriptor}" inputSchema properties expectedStartFrame description
)
string(FIND
    "${_expected_start_frame_description}"
	"gpu_performance_stop"
	_expected_start_frame_gpu_position
)
string(FIND
	"${_expected_start_frame_description}"
	"legacy secondary guard for cpu_performance_stop"
	_expected_start_frame_cpu_position
)
if(_expected_start_frame_description_error OR
	_expected_start_frame_gpu_position EQUAL -1 OR
	_expected_start_frame_cpu_position EQUAL -1)
    message(FATAL_ERROR
		"Render-scale expectedStartFrame ownership guards are incomplete: ${_expected_start_frame_description_error}"
    )
endif()

foreach(_required_behavior IN ITEMS
	"PreparationTelemetryJson"
	"request_to_prepared"
	"prepared_to_creator"
	"shader_cache_busy_wait"
	"optionsGeneration"
	"d3dObjectCreationQpcTicks"
    "QualificationPolicy::SaturatingDeadlineTick"
	"QualificationPolicy::EvaluateMilestones"
	"QualificationPolicy::IsMilestoneSatisfied"
	"QualificationPolicy::UsesVendorEvaluation"
	"ReplacementTelemetry::ClassifyPresentationProof"
	"QualificationPolicy::HasCoherentNativeVendorEvaluation"
	"case CSX::UpscalingAPI::Method::kNone"
	"case CSX::UpscalingAPI::Method::kTAA"
	"{ \"methodValue\", static_cast<uint32_t>(a_profile.method) }"
	"RecordQualificationMilestones"
	"QualificationPolicy::ExactObservationTarget"
	"QualificationPolicy::HasCoherentPresentationFrames"
    "QualificationPolicy::HasRequiredPresentationHistory"
    "QualificationPolicy::IsFoveationInvariantViolation"
    "QualificationPolicy::IsTransientObservationDispatchError"
    "QualificationPolicy::OwnsTransitionInstance"
    "TryParseQualificationOwnerID"
    "TryParseOptionalIntegerExpectation"
    "QualificationEvidenceOwnedBy"
    "TryParseQualificationCocCellEditorID"
    "invalid_coc_cell_editor_id"
    "stress_session_mismatch"
    "dlss_trace_session_mismatch"
    "cpu_performance_session_mismatch"
    "cpu_performance_start_frame_mismatch"
	"gpu_performance_start_frame_mismatch"
	"performance_telemetry_already_active"
	"performance_telemetry_dispatch_frame_mismatch"
	"cpuStartFrame != frame || gpuStartFrame != frame"
	"result[\"performanceTelemetry\"]"
    "cpu_performance_session_id_unavailable"
    "GetVRRenderScaleCPUPerformanceSessionID()"
    "{ \"sessionId\", sessionID }"
    "sessionID != 0 ? \"stopped\" : \"reset\""
    "StopDLSSDevBenchTrace(expectedSessionID.value_or(0))"
    "a_transition.dispatchTick"
    "a_transition.dispatchFrame"
    "elapsedOrigin"
    "coc_command"
    "RE::Console::ExecuteCommand(command.c_str())"
    "QualificationMonotonicRegressionsJson"
    "BuildAdapterIdentity"
    "{ \"adapter\", BuildAdapterIdentity() }"
    "CheckInterfaceSupport(__uuidof(ID3D11Device)"
    "QueryPerformanceCounter"
    "expectedCellEditorId"
    "kElapsedMillisecondsReceiptField"
    "kElapsedFramesReceiptField"
	"presentationFailureMask"
	"presentationElapsedMs"
	"presentationElapsedFrames"
	"cleanupFailureMask"
	"cleanupElapsedMs"
	"cleanupElapsedFrames"
	"strictFailureMask"
	"strictElapsedMs"
	"strictElapsedFrames"
	"milestoneTimings"
	"presentationToCleanupMs"
	"cleanupTailMs"
	"sameObservation"
	"replacementTimeline"
	"schemaRevision"
	"presentationProof"
	"exact_vendor_evaluation"
	"exact_native_presentation"
	"validated_completed_output_hold"
	"preparationAdmission"
	"replacementMutationAdmission"
	"mutationExpectation"
	"SeedNativeTargetMutationExpectation"
	"MergeQualificationMutationExpectation"
	"MergeMutationExpectation"
	"OwnsMutationBoundary"
	"IsAtOrAfterMutationBoundary"
	"native_contract_reuse"
	"scaled_contract_retirement"
	"RecordPhysicalMutationBoundary"
	"provider_resource_invalidation"
	"stressSessionId"
	"qualificationTransitionId"
	"ownershipToken"
	"ImportQualificationReplacementTimeline"
	"firstPostMutation"
	"firstNewGenerationProven"
	"mutationNotRequiredTerminalProof"
	"boundarySpanningStereoCycles"
	"expectedReplacementRequestID"
	"replacementContractGeneration"
	"replacementDeviceIdentity"
	"IsExactTargetProofKind"
	"OptionalNonNegativeIntegerOrZero"
	"TryRecordQualificationReplacementTimeline"
	"exactStableAfterMutation"
	"presentationCycleAudit"
	"eyeObservations"
	"incompleteStereoCycles"
	"preMutationExactPresentationSuppressed"
	"preMutationStretchWithoutMutation"
	"postMutationOldGenerationPresented"
	"postMutationUnprovenStereoSubmitted"
	"phaseDurations"
	"currentPresentationProven"
	"currentPresentationGeneration"
	"replacementAdmissionBlocked"
	"replacementAdmissionBlockReasons"
	"physicalMutationStarted"
	"selectedPresentationDisposition"
	"outstandingCleanupDebt"
	"timedOutMilestone"
	"resourcePublicationCurrent"
	"GetCurrentMainRenderTargetResourcePublicationDiagnostics()"
	"\"resourcePublication\""
	"\"current\""
	"\"evaluated\""
	"\"currentGeneration\""
	"\"completedGeneration\""
	"\"publishedGeneration\""
	"\"expectedWidth\""
	"\"expectedHeight\""
	"\"publishedWidth\""
	"\"publishedHeight\""
	"\"loadedFeatureSetupCount\""
	"\"present\""
	"\"complete\""
	"\"deferredSetupAcknowledged\""
	"\"generationMatchesCurrent\""
	"\"generationMatchesCompleted\""
	"\"dimensionsMatch\""
	"\"deviceMatches\""
	"\"contextMatches\""
	"providerTerminalClear"
	"\"nativeVendorExecution\""
	"\"actualBackend\""
	"\"native_vendor_frames\""
    "controller.retirement.nextCleanupFrame == 0"
	"BuildProvenance::ValidateExpectedBuild"
)
    string(FIND "${_bridge}\n${_replacement_policy}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR "Render-scale qualification behavior is missing: ${_required_behavior}")
    endif()
endforeach()

string(FIND
	"${_upscaling_source}"
	"PhysicalMutationBoundarySource::\n\t\t\t\t\tProviderInvalidation"
	_provider_invalidation_boundary_position
)
if(_provider_invalidation_boundary_position EQUAL -1)
	message(FATAL_ERROR
		"Provider resource invalidation does not emit the explicit DevBench boundary"
	)
endif()

string(FIND
	"${_upscaling_source}"
	"void Upscaling::RecordVRVendorRuntimeLifecycle"
	_lifecycle_function_start
)
string(FIND
	"${_upscaling_source}"
	"void Upscaling::ArchiveVRRenderScaleTransitionMetricsLocked"
	_lifecycle_function_end
)
if(_lifecycle_function_start EQUAL -1 OR
	_lifecycle_function_end EQUAL -1 OR
	_lifecycle_function_end LESS_EQUAL _lifecycle_function_start)
	message(FATAL_ERROR "Provider lifecycle function boundaries were not found")
endif()
math(EXPR _lifecycle_function_length
	"${_lifecycle_function_end} - ${_lifecycle_function_start}")
string(SUBSTRING
	"${_upscaling_source}"
	${_lifecycle_function_start}
	${_lifecycle_function_length}
	_lifecycle_function
)
string(FIND
	"${_lifecycle_function}"
	"RecordPhysicalMutationBoundary"
	_lifecycle_boundary_position
)
if(NOT _lifecycle_boundary_position EQUAL -1)
	message(FATAL_ERROR
		"Dirty, WaitingForDrain, or Creating lifecycle state can still publish the destructive boundary"
	)
endif()

string(FIND
	"${_upscaling_source}"
	"PhysicalMutationBoundarySource::\n\t\t\t\tEngineTargetCreator"
	_engine_creator_boundary_position
)
if(_engine_creator_boundary_position EQUAL -1)
	message(FATAL_ERROR
		"Engine-target creator entry does not emit the explicit DevBench boundary"
	)
endif()

string(FIND
	"${_bridge}"
	"VendorLifecycleMutationStarted"
	_lifecycle_mutation_authority_position
)
if(NOT _lifecycle_mutation_authority_position EQUAL -1)
	message(FATAL_ERROR
		"Provider lifecycle phases still act as a destructive mutation authority"
	)
endif()

foreach(_unsafe_nullable_proof_read IN ITEMS
	"dispatchProof->value(\"contractGeneration\""
	"proof->value(\"contractGeneration\""
	"dispatchProof.value(\"contractGeneration\""
	"dispatchProof.value(\"transitionEpoch\""
	"dispatchProof.value(\"deviceIdentity\""
	"dispatchProof.value(\"resourceRevision\""
	"dispatchProof.value(\"methodValue\""
)
	string(FIND "${_bridge}" "${_unsafe_nullable_proof_read}" _unsafe_nullable_proof_position)
	if(NOT _unsafe_nullable_proof_position EQUAL -1)
		message(FATAL_ERROR
			"Nullable replacement proof field is read as a required number: ${_unsafe_nullable_proof_read}"
		)
	endif()
endforeach()

string(FIND
	"${_upscaling_source}"
	"#ifdef DEVBENCH_BRIDGE_ENABLED\n\tVRRenderScaleDevBenchBridge::RecordPresentationAuditObservation"
	_audit_compile_guard_position
)
if(_audit_compile_guard_position EQUAL -1)
	message(FATAL_ERROR
		"Authoritative presentation auditing is not compiled out without DevBench"
	)
endif()

foreach(_required_controller_behavior IN ITEMS
	"apiControllerPublicationRequired"
	"ArmVRNativeRestorePresentationGuard(request.transitionEpoch)"
	"requiresNativePresentationStabilization"
	"GetVRRenderScaleBackendFromFSRDispatchPath"
	"vendorDispatchFrame ="
	"vendorDispatchSerial"
)
	string(FIND
		"${_upscaling_source}"
		"${_required_controller_behavior}"
		_controller_behavior_position
	)
	if(_controller_behavior_position EQUAL -1)
		message(FATAL_ERROR
			"VR API controller publication behavior is missing: ${_required_controller_behavior}"
		)
	endif()
endforeach()

foreach(_required_vendor_presentation_evidence IN ITEMS
	"captureSubmitStageVendorDispatchEvidence"
	"applySubmitStageVendorDispatchEvidence"
	"setVendorPresentationObservation(cachedEyeState)"
	"captureSubmitStageVendorDispatchEvidence("
	"submitStageVendorEyeState[eyeIndex])"
	"ReplacementTelemetry::HasCoherentVendorDispatch"
	".vendorDispatchProven = vendorDispatchProven"
	"sharedFSRDispatchRequired"
	".vendorDispatchFrame = published.vendorDispatchFrame"
	".vendorDispatchSerial = published.vendorDispatchSerial"
	".vendorRuntimeFallback = published.vendorRuntimeFallback"
	"leftVendorDispatchFrame"
	"rightVendorDispatchSerial"
	"observationVendorDispatchProven"
	".vendorDispatchProven = observationVendorDispatchProven"
	"MatchesTargetContractGeneration("
	"target.renderScaleMode"
)
	string(FIND
		"${_upscaling_source}\n${_bridge}"
		"${_required_vendor_presentation_evidence}"
		_vendor_presentation_evidence_position
	)
	if(_vendor_presentation_evidence_position EQUAL -1)
		message(FATAL_ERROR
			"Submit-stage vendor presentation evidence is incomplete: ${_required_vendor_presentation_evidence}"
		)
	endif()
endforeach()

foreach(_publication_source_contract IN ITEMS
    "State::GetCurrentMainRenderTargetResourcePublicationDiagnostics() const noexcept"
    "const auto mainRenderTargetSize = GetMainRenderTargetSize();"
    "return GetRenderTargetResourcePublicationDiagnostics(width, height);"
)
    string(FIND "${_state_source}" "${_publication_source_contract}" _publication_source_position)
    if(_publication_source_position EQUAL -1)
        message(FATAL_ERROR
            "Render-target publication diagnostics do not use the physical main target: ${_publication_source_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_bridge}"
    "a_controller.physicalPhase != Upscaling::VRRenderScalePhysicalPhase::ContractPublished"
    _transient_physical_phase_position
)
if(NOT _transient_physical_phase_position EQUAL -1)
    message(FATAL_ERROR
        "Qualification still requires the transient physical publication phase"
    )
endif()

foreach(_required_cpu_session_behavior IN ITEMS
    "GetVRRenderScaleCPUPerformanceSessionID() const noexcept"
    "StartVRRenderScaleCPUPerformanceTelemetry() noexcept"
    "nextVRRenderScaleCPUPerformanceSessionID{ 1 }"
    "nextVRRenderScaleCPUPerformanceSessionID.compare_exchange_weak"
    "sessionID == std::numeric_limits<uint64_t>::max()"
    "vrRenderScaleCPUPerformanceSessionID.store("
    "vrRenderScaleCPUPerformanceSessionID.store(\n\t\t0,"
)
    string(FIND
        "${_upscaling_header}\n${_upscaling_source}"
        "${_required_cpu_session_behavior}"
        _cpu_session_behavior_position
    )
    if(_cpu_session_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "CPU telemetry session behavior is missing: ${_required_cpu_session_behavior}"
        )
    endif()
endforeach()

string(REGEX MATCH
    "void Upscaling::StopVRRenderScaleCPUPerformanceTelemetry\\(\\) noexcept[\r\n\t ]*\\{[^}]*\\}"
    _cpu_stop_implementation
    "${_upscaling_source}"
)
if(NOT _cpu_stop_implementation)
    message(FATAL_ERROR "CPU telemetry stop implementation was not found")
endif()
string(FIND
    "${_cpu_stop_implementation}"
    "vrRenderScaleCPUPerformanceSessionID"
    _cpu_stop_session_clear_position
)
if(NOT _cpu_stop_session_clear_position EQUAL -1)
    message(FATAL_ERROR "CPU telemetry stop must retain the current session ID")
endif()

string(FIND
    "${_upscaling_source}"
    "nextVRRenderScaleCPUPerformanceSessionID.store("
    _cpu_session_allocator_rewind_position
)
if(NOT _cpu_session_allocator_rewind_position EQUAL -1)
    message(FATAL_ERROR "CPU telemetry reset must not rewind the session allocator")
endif()

foreach(_required_streamline_behavior IN ITEMS
    "StopDLSSDevBenchTrace(uint64_t a_expectedSessionID = 0)"
    "activeSessionID != a_expectedSessionID"
)
    string(FIND
        "${_streamline_header}\n${_streamline_source}"
        "${_required_streamline_behavior}"
        _streamline_behavior_position
    )
    if(_streamline_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "DLSS trace ownership behavior is missing: ${_required_streamline_behavior}"
        )
    endif()
endforeach()

message(STATUS "Render-scale DevBench qualification contract is coherent")
