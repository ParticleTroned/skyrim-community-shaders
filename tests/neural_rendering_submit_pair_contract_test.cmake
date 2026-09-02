cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_overlay_path "${PROJECT_ROOT}/src/Features/VR/InSceneOverlay.cpp")
set(_upscaling_header_path "${PROJECT_ROOT}/src/Features/Upscaling.h")
set(_upscaling_source_path "${PROJECT_ROOT}/src/Features/Upscaling.cpp")
set(
    _pipeline_policy_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/PipelinePolicy.h"
)
foreach(_required_path IN ITEMS
    "${_overlay_path}"
    "${_upscaling_header_path}"
    "${_upscaling_source_path}"
    "${_pipeline_policy_path}"
)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required submit-pair contract input is missing: ${_required_path}")
    endif()
endforeach()

file(READ "${_overlay_path}" _overlay)
file(READ "${_upscaling_header_path}" _upscaling_header)
file(READ "${_upscaling_source_path}" _upscaling_source)
file(READ "${_pipeline_policy_path}" _pipeline_policy)
set(
    _contract_text
    "${_overlay}\n${_upscaling_header}\n${_upscaling_source}\n${_pipeline_policy}"
)

foreach(_required_contract IN ITEMS
    [[struct BSOpenVR_Submit]]
    [[stl::write_vfunc<0x03, BSOpenVR_Submit>]]
    [[thread_local uint64_t g_neuralSubmitPairBoundaryToken = 0]]
    [[BeginNeuralSubmitPairBoundary(]]
    [[EndNeuralSubmitPairBoundary(]]
    [[ObserveNeuralSubmitPairBoundaryEye(]]
    [[matchedSubmitPairBoundaryToken]]
    [[a_submitPairBoundaryToken]]
    [[ResolveCachedStereoPairReuse(]]
    [[CachedStereoPairReuse::BypassPresentedEye]]
    [[presentedEyeMask]]
    [[retainedNeuralSubmitPairBoundaryToken]]
)
    string(FIND "${_contract_text}" "${_required_contract}" _contract_position)
    if(_contract_position EQUAL -1)
        message(FATAL_ERROR "Submit-pair boundary contract is missing: ${_required_contract}")
    endif()
endforeach()

string(FIND "${_overlay}" [[struct BSOpenVR_Submit]] _outer_submit_begin)
string(FIND "${_overlay}" [[struct IVRCompositor_Submit]] _inner_submit_begin)
if(_outer_submit_begin EQUAL -1 OR _inner_submit_begin EQUAL -1 OR
   _inner_submit_begin LESS_EQUAL _outer_submit_begin)
    message(FATAL_ERROR "Unable to isolate the outer submit hook contract")
endif()
math(EXPR _outer_submit_length "${_inner_submit_begin} - ${_outer_submit_begin}")
string(SUBSTRING
    "${_overlay}"
    ${_outer_submit_begin}
    ${_outer_submit_length}
    _outer_submit
)
foreach(_outer_contract IN ITEMS
    [[ID3D11Texture2D* pTexture]]
    [[reinterpret_cast<uintptr_t>(pTexture)]]
    [[BeginNeuralSubmitPairBoundary(]]
    [[g_neuralSubmitPairBoundaryToken = submitPairBoundaryToken]]
    [[g_neuralSubmitPairBoundaryToken = previousSubmitPairBoundaryToken]]
)
    string(FIND "${_outer_submit}" "${_outer_contract}" _outer_contract_position)
    if(_outer_contract_position EQUAL -1)
        message(FATAL_ERROR "Outer submit ABI contract is missing: ${_outer_contract}")
    endif()
endforeach()
foreach(_unsafe_outer_contract IN ITEMS
    [[vr::Texture_t]]
    [[pTexture->handle]]
    [[pTexture->eType]]
    [[TryGetTexture2DDesc(]]
    [[QueryInterface(]]
)
    string(FIND
        "${_outer_submit}"
        "${_unsafe_outer_contract}"
        _unsafe_outer_contract_position
    )
    if(NOT _unsafe_outer_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Outer submit hook dereferences the raw engine texture as OpenVR metadata: ${_unsafe_outer_contract}"
        )
    endif()
endforeach()
string(FIND
    "${_outer_submit}"
    [[return func(_this, pTexture, pBounds, nSubmitFlags);]]
    _outer_forward_position
)
if(_outer_forward_position EQUAL -1)
    message(FATAL_ERROR "Outer submit hook does not forward the original ABI arguments unchanged")
endif()
string(FIND
    "${_outer_submit}"
    [[g_neuralSubmitPairBoundaryToken = submitPairBoundaryToken]]
    _outer_token_set_position
)
string(FIND
    "${_outer_submit}"
    [[const SKSE::stl::scope_exit endSubmitPairBoundary]]
    _outer_scope_exit_position
)
string(FIND
    "${_outer_submit}"
    [[upscaling.EndNeuralSubmitPairBoundary(submitPairBoundaryToken)]]
    _outer_boundary_end_position
)
string(FIND
    "${_outer_submit}"
    [[g_neuralSubmitPairBoundaryToken = previousSubmitPairBoundaryToken]]
    _outer_token_restore_position
)
if(_outer_token_set_position GREATER_EQUAL _outer_scope_exit_position OR
   _outer_scope_exit_position GREATER_EQUAL _outer_boundary_end_position OR
   _outer_boundary_end_position GREATER_EQUAL _outer_token_restore_position OR
   _outer_token_restore_position GREATER_EQUAL _outer_forward_position)
    message(FATAL_ERROR "Outer submit token lifetime no longer encloses the original call")
endif()

string(FIND "${_overlay}" [[void VR::InitInSceneResources()]] _inner_submit_end)
if(_inner_submit_end EQUAL -1 OR _inner_submit_end LESS_EQUAL _inner_submit_begin)
    message(FATAL_ERROR "Unable to isolate the nested OpenVR submit hook contract")
endif()
math(EXPR _inner_submit_length "${_inner_submit_end} - ${_inner_submit_begin}")
string(SUBSTRING
    "${_overlay}"
    ${_inner_submit_begin}
    ${_inner_submit_length}
    _inner_submit
)
foreach(_inner_contract IN ITEMS
    [[const vr::Texture_t* pTexture]]
    [[ObserveNeuralSubmitPairBoundaryEye(]]
    [[g_neuralSubmitPairBoundaryToken]]
)
    string(FIND "${_inner_submit}" "${_inner_contract}" _inner_contract_position)
    if(_inner_contract_position EQUAL -1)
        message(FATAL_ERROR "Nested submit token handoff is missing: ${_inner_contract}")
    endif()
endforeach()
string(REGEX REPLACE "[\r\n\t ]+" " " _inner_submit_normalized "${_inner_submit}")
string(FIND
    "${_inner_submit_normalized}"
    [[ObserveNeuralSubmitPairBoundaryEye( g_neuralSubmitPairBoundaryToken, compositorCycleToken, eEye, pTexture, nSubmitFlags);]]
    _inner_token_handoff_position
)
if(_inner_token_handoff_position EQUAL -1)
    message(FATAL_ERROR "Nested submit observer does not consume the active thread token")
endif()

string(FIND
    "${_upscaling_source}"
    [[uint64_t Upscaling::BeginNeuralSubmitPairBoundary(]]
    _boundary_begin
)
string(FIND
    "${_upscaling_source}"
    [[void Upscaling::EndNeuralSubmitPairBoundary(]]
    _boundary_end
)
if(_boundary_begin EQUAL -1 OR _boundary_end EQUAL -1 OR
   _boundary_end LESS_EQUAL _boundary_begin)
    message(FATAL_ERROR "Unable to isolate the outer boundary implementation")
endif()
math(EXPR _boundary_length "${_boundary_end} - ${_boundary_begin}")
string(SUBSTRING
    "${_upscaling_source}"
    ${_boundary_begin}
    ${_boundary_length}
    _boundary_implementation
)
foreach(_boundary_contract IN ITEMS
    [[uintptr_t a_sourceIdentity]]
    [[if (active.active || a_sourceIdentity == 0)]]
    [[active.sourceIdentity = a_sourceIdentity]]
)
    string(FIND
        "${_boundary_implementation}"
        "${_boundary_contract}"
        _boundary_contract_position
    )
    if(_boundary_contract_position EQUAL -1)
        message(FATAL_ERROR "Outer boundary contract is missing: ${_boundary_contract}")
    endif()
endforeach()
foreach(_unsafe_boundary_contract IN ITEMS
    [[vr::Texture_t]]
    [[TryGetTexture2DDesc(]]
    [[QueryInterface(]]
    [[copy_from(]]
    [[a_texture->]]
)
    string(FIND
        "${_boundary_implementation}"
        "${_unsafe_boundary_contract}"
        _unsafe_boundary_contract_position
    )
    if(NOT _unsafe_boundary_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Outer submit boundary dereferences or retains its opaque source: ${_unsafe_boundary_contract}"
        )
    endif()
endforeach()
string(FIND
    "${_boundary_implementation}"
    [[if (active.active || a_sourceIdentity == 0)]]
    _boundary_collision_guard_position
)
string(FIND
    "${_boundary_implementation}"
    [[active = {};]]
    _boundary_reset_position
)
if(_boundary_collision_guard_position GREATER_EQUAL _boundary_reset_position)
    message(FATAL_ERROR "Outer boundary reset can overwrite an active submit scope")
endif()

string(FIND
    "${_upscaling_header}"
    [[struct NeuralSubmitPairBoundaryState]]
    _boundary_state_begin
)
string(FIND
    "${_upscaling_header}"
    [[mutable std::mutex neuralSubmitPairBoundaryMutex]]
    _boundary_state_end
)
if(_boundary_state_begin EQUAL -1 OR _boundary_state_end EQUAL -1 OR
   _boundary_state_end LESS_EQUAL _boundary_state_begin)
    message(FATAL_ERROR "Unable to isolate the submit-pair boundary state")
endif()
math(EXPR _boundary_state_length "${_boundary_state_end} - ${_boundary_state_begin}")
string(SUBSTRING
    "${_upscaling_header}"
    ${_boundary_state_begin}
    ${_boundary_state_length}
    _boundary_state
)
string(FIND
    "${_boundary_state}"
    [[uintptr_t sourceIdentity = 0]]
    _boundary_identity_position
)
if(_boundary_identity_position EQUAL -1)
    message(FATAL_ERROR "Submit-pair boundary does not retain opaque source identity")
endif()
foreach(_owned_boundary_state IN ITEMS
    [[vr::Texture_t]]
    [[D3D11_TEXTURE2D_DESC]]
    [[com_ptr]]
    [[sourceValid]]
)
    string(FIND
        "${_boundary_state}"
        "${_owned_boundary_state}"
        _owned_boundary_state_position
    )
    if(NOT _owned_boundary_state_position EQUAL -1)
        message(FATAL_ERROR
            "Submit-pair boundary retains or interprets its opaque source: ${_owned_boundary_state}"
        )
    endif()
endforeach()

string(FIND
    "${_upscaling_source}"
    [[uint64_t Upscaling::ObserveNeuralSubmitPairBoundaryEye(]]
    _boundary_observer_begin
)
string(FIND
    "${_upscaling_source}"
    [[const char* Upscaling::GetNeuralStereoRouteRoleName(]]
    _boundary_observer_end
)
if(_boundary_observer_begin EQUAL -1 OR _boundary_observer_end EQUAL -1 OR
   _boundary_observer_end LESS_EQUAL _boundary_observer_begin)
    message(FATAL_ERROR "Unable to isolate the nested-eye boundary observer")
endif()
math(EXPR
    _boundary_observer_length
    "${_boundary_observer_end} - ${_boundary_observer_begin}"
)
string(SUBSTRING
    "${_upscaling_source}"
    ${_boundary_observer_begin}
    ${_boundary_observer_length}
    _boundary_observer
)
foreach(_observer_contract IN ITEMS
    [[uint64_t a_expectedToken]]
    [[a_expectedToken == 0]]
    [[active.token != a_expectedToken]]
    [[a_texture && a_texture->handle]]
    [[a_texture->eType == vr::TextureType_DirectX]]
    [[reinterpret_cast<uintptr_t>(a_texture->handle)]]
    [[active.sourceIdentity != 0]]
    [[active.sourceIdentity == sourceIdentity]]
    [[if (!proven)]]
)
    string(FIND
        "${_boundary_observer}"
        "${_observer_contract}"
        _observer_contract_position
    )
    if(_observer_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Nested-eye boundary validation is missing: ${_observer_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_overlay}"
    [[SubmitVRUpscaledFrame(eEye, compositorCycleToken, pTexture]]
    _stale_call_position
)
if(NOT _stale_call_position EQUAL -1)
    message(FATAL_ERROR "Submit path bypasses the engine-owned pair-boundary token")
endif()

message(STATUS "Neural Rendering submit-pair contract passed")
