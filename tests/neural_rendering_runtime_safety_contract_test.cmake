cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_upscaling_path "${PROJECT_ROOT}/src/Features/Upscaling.cpp")
set(
    _renderer_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp"
)
foreach(_required_path IN ITEMS "${_upscaling_path}" "${_renderer_path}")
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR
            "Required Neural Rendering safety input is missing: ${_required_path}"
        )
    endif()
endforeach()

file(READ "${_upscaling_path}" _upscaling)
file(READ "${_renderer_path}" _renderer)

string(FIND
    "${_upscaling}"
    "void Upscaling::NotifyVRMenuPresentationContextChange"
    _notify_begin
)
string(FIND
    "${_upscaling}"
    "bool Upscaling::SealVRMenuFrameTransaction"
    _notify_end
)
if(_notify_begin EQUAL -1 OR _notify_end EQUAL -1 OR
   _notify_end LESS_EQUAL _notify_begin)
    message(FATAL_ERROR "Could not isolate the VR menu context-change contract")
endif()
math(EXPR _notify_length "${_notify_end} - ${_notify_begin}")
string(SUBSTRING "${_upscaling}" ${_notify_begin} ${_notify_length} _notify)

foreach(_notify_contract IN ITEMS
    [[g_neuralMenuQueryEpoch.fetch_add(1, std::memory_order_acq_rel)]]
    [[neuralPresentationContractEpoch.fetch_add(1, std::memory_order_acq_rel)]]
    [[if (settings.neuralRenderingEnabled) {]]
    [[RequestNeuralHistoryReset();]]
    [[RequestHistoryReset();]]
    [[InvalidateVRMenuCommittedLayer(a_reason);]]
)
    string(FIND "${_notify}" "${_notify_contract}" _notify_position)
    if(_notify_position EQUAL -1)
        message(FATAL_ERROR
            "VR menu context-change contract is missing: ${_notify_contract}"
        )
    endif()
endforeach()
string(REGEX MATCHALL "RequestHistoryReset\\(\\)" _notify_shared_resets "${_notify}")
list(LENGTH _notify_shared_resets _notify_shared_reset_count)
if(NOT _notify_shared_reset_count EQUAL 1)
    message(FATAL_ERROR
        "VR menu context changes must contain exactly one NR-gated shared history reset"
    )
endif()

string(FIND
    "${_upscaling}"
    "void Upscaling::ObserveNeuralRenderingMenuSuppression"
    _suppression_begin
)
string(FIND
    "${_upscaling}"
    "void Upscaling::RequestNeuralHistoryReset"
    _suppression_end
)
if(_suppression_begin EQUAL -1 OR _suppression_end EQUAL -1 OR
   _suppression_end LESS_EQUAL _suppression_begin)
    message(FATAL_ERROR "Could not isolate the NR menu-suppression contract")
endif()
math(EXPR _suppression_length "${_suppression_end} - ${_suppression_begin}")
string(
    SUBSTRING
    "${_upscaling}"
    ${_suppression_begin}
    ${_suppression_length}
    _suppression
)
foreach(_suppression_contract IN ITEMS
    [[neuralPresentationContractEpoch.fetch_add(1, std::memory_order_acq_rel)]]
    [[if (a_suppressed && settings.neuralRenderingEnabled) {]]
    [[RequestNeuralHistoryReset();]]
    [[RequestHistoryReset();]]
)
    string(FIND
        "${_suppression}"
        "${_suppression_contract}"
        _suppression_position
    )
    if(_suppression_position EQUAL -1)
        message(FATAL_ERROR
            "NR menu-suppression contract is missing: ${_suppression_contract}"
        )
    endif()
endforeach()

foreach(_slot_contract IN ITEMS
    [[std::uint32_t activeFeatureSlot_ = Runtime::kFeatureSlotCount]]
    [[SetActiveFeatureSlotLocked(a_args[1].featureSlot);]]
    [[SetActiveFeatureSlotLocked(a_args[index].featureSlot);]]
    [[SetActiveFeatureSlotLocked(args.featureSlot);]]
    [[SetActiveFeatureSlotLocked(stereoArgs[1].featureSlot);]]
    [[SetActiveFeatureSlotLocked(synchronizedArgs[0].featureSlot);]]
    [[SetActiveFeatureSlotLocked(synchronizedArgs[1].featureSlot);]]
    [[SetActiveFeatureSlotLocked(a_slot);
		if (a_countApplyFailure) {]]
    [[SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (ValidateLocked(a_args[index], resources[index]))]]
    [[SetActiveFeatureSlotLocked(args.featureSlot);
			ValidationFailure validation{};]]
    [[SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (!EnsureSlotLocked(a_args[index].featureSlot, resources[index]))]]
    [[SetActiveFeatureSlotLocked(args.featureSlot);
			const bool forcedReset = stereoBatch ?]]
    [[state_->ActiveFeatureSlotOrLocked(a_args[0].featureSlot)]]
)
    string(FIND "${_renderer}" "${_slot_contract}" _slot_position)
    if(_slot_position EQUAL -1)
        message(FATAL_ERROR
            "Renderer active feature-slot contract is missing: ${_slot_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_renderer}"
    [[state_->snapshot_.featureSlot < Runtime::kFeatureSlotCount]]
    _stale_snapshot_attribution
)
if(NOT _stale_snapshot_attribution EQUAL -1)
    message(FATAL_ERROR
        "Renderer exception attribution still relies on snapshot request telemetry"
    )
endif()

string(
    REGEX MATCHALL
    "ActiveFeatureSlotOrLocked\\(a_args\\[0\\]\\.featureSlot\\)"
    _stereo_catch_attribution
    "${_renderer}"
)
list(LENGTH _stereo_catch_attribution _stereo_catch_attribution_count)
if(NOT _stereo_catch_attribution_count EQUAL 2)
    message(FATAL_ERROR
        "Both batched and sequential stereo catches must use active-slot attribution"
    )
endif()

message(STATUS "Neural Rendering runtime safety contract passed")
