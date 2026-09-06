if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ
    "${PROJECT_ROOT}/src/Features/VR/InSceneOverlay.cpp"
    _overlay_source
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling.cpp"
    _upscaling_source
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling.h"
    _upscaling_header
)
file(READ
    "${PROJECT_ROOT}/src/Features/Upscaling/VRSubmitInputFreshnessPolicy.h"
    _freshness_policy
)

set(_contract
    "${_overlay_source}\n${_upscaling_source}\n${_upscaling_header}\n${_freshness_policy}"
)
string(REGEX REPLACE "[\r\n\t ]+" " " _contract "${_contract}")

foreach(_required_contract IN ITEMS
    "struct BSOpenVR_Submit"
    "stl::write_vfunc<0x03, BSOpenVR_Submit>"
    "ResolveOuterBoundaryToken("
    "SubmitVRUpscaledFrame(eEye, compositorCycleToken, submitPairBoundaryToken"
    "ResolveProducerProof( submitInputAdmission)"
    "CanConsumePeerInputs( submitInputProof, eyeIndex)"
    "peerInputFreshnessProven && upscaleMethod == UpscaleMethod::kFSR"
    "peerInputFreshnessProven && otherEyeStateBeforeFullEncode.ready"
    "MatchesProducerProof( targetEyeState.inputProof, submitInputProof)"
    "MatchesProducerProof( cachedEyeState.inputProof, submitInputProof)"
    "submitStageRuntimeFSRStereoState.Matches( submitInputProof"
    "submitStageRuntimeFSRStereoState.Record( submitInputProof"
    "submitStageRuntimeFSRStereoState = {}"
    "sanitizeSubmitStageInputEye(otherEyeIndex, otherSourceRegion)"
    "if (!peerInputSanitized) { replayOtherEyeFromFoveated = false; runtimeFSRStereoResourcesReady = false; }"
    "otherEyeState = otherEyeStateBeforeFullEncode"
    "activeSourceIdentity == a_observation.nestedHandleIdentity"
    "a_admission.lastWorldRenderFrame != a_admission.submitFrame"
    "a_admission.lastCompletedWorldRenderFrame != a_admission.submitFrame"
    "eyeMask & (1u << eye)"
)
    string(FIND "${_contract}" "${_required_contract}" _contract_position)
    if(_contract_position EQUAL -1)
        message(FATAL_ERROR
            "VR submit input freshness contract is missing: ${_required_contract}"
        )
    endif()
endforeach()

foreach(_forbidden_contract IN ITEMS
    "const bool runtimeFSRStereoRequested = upscaleMethod == UpscaleMethod::kFSR && sourceContainsBothEyes"
    "const bool replayOtherEyeFromFoveated = submitStageVendorEyeState[otherEyeIndex].ready"
)
    string(FIND "${_contract}" "${_forbidden_contract}" _contract_position)
    if(NOT _contract_position EQUAL -1)
        message(FATAL_ERROR
            "Peer input is still admitted without freshness proof: ${_forbidden_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_upscaling_source}"
    "if (runtimeFSRStereoResourcesReady)"
    _peer_guard_position
)
string(FIND
    "${_upscaling_source}"
    "const auto& otherSourceRegion ="
    _peer_region_position
)
string(FIND
    "${_upscaling_source}"
    "sanitizeSubmitStageInputEye(otherEyeIndex, otherSourceRegion)"
    _peer_sanitization_position
)
string(FIND
    "${_upscaling_source}"
    "fidelityFX.UpscaleStereoRegions("
    _stereo_dispatch_position
)
if(_peer_guard_position EQUAL -1 OR _peer_region_position EQUAL -1 OR
   _peer_sanitization_position EQUAL -1 OR _stereo_dispatch_position EQUAL -1 OR
   _peer_guard_position GREATER_EQUAL _peer_region_position OR
   _peer_region_position GREATER_EQUAL _peer_sanitization_position OR
   _peer_sanitization_position GREATER_EQUAL _stereo_dispatch_position)
    message(FATAL_ERROR
        "Freshness admission no longer dominates the peer copy and stereo dispatch"
    )
endif()

message(STATUS "VR submit peer inputs require exact producer freshness")
