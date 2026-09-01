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

string(FIND
    "${_overlay}"
    [[SubmitVRUpscaledFrame(eEye, compositorCycleToken, pTexture]]
    _stale_call_position
)
if(NOT _stale_call_position EQUAL -1)
    message(FATAL_ERROR "Submit path bypasses the engine-owned pair-boundary token")
endif()

message(STATUS "Neural Rendering submit-pair contract passed")
