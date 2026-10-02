if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp" source)
function(extract start_token end_token output)
    string(FIND "${source}" "${start_token}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Missing prepared-selection boundary: ${start_token}")
    endif()
    string(SUBSTRING "${source}" ${start} -1 tail)
    string(FIND "${tail}" "${end_token}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Missing prepared-selection boundary: ${end_token}")
    endif()
    string(LENGTH "${end_token}" end_length)
    math(EXPR length "${end} + ${end_length}")
    string(SUBSTRING "${tail}" 0 ${length} body)
    file(WRITE "${OUTPUT_DIRECTORY}/${output}" "${body}\n")
endfunction()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract("\t\tbool UsesAuthoredMask(" "\n\t\t}"
    neural_authored_mode_under_test.h)
extract("\t\tstruct ProjectionEye" "\n\t\tvoid CaptureSourceGeometry"
    neural_source_types_under_test.h)
file(READ "${OUTPUT_DIRECTORY}/neural_source_types_under_test.h" source_types)
string(REPLACE "\n\t\tvoid CaptureSourceGeometry\n" "\n" source_types "${source_types}")
file(WRITE "${OUTPUT_DIRECTORY}/neural_source_types_under_test.h" "${source_types}")
extract("\t\tvoid CaptureSourceGeometry(" "\n\t\t}"
    neural_source_capture_under_test.h)
extract("\t\tconst ActorAdmission* FindCapturedAdmission(" "\n\t\t}"
    neural_source_admission_under_test.h)
extract("\t\tEarlyMaskReadback* FindCurrentSupport(" "\n\t\t}"
    neural_source_support_under_test.h)
extract("\tbool CharacterRendering::IsCurrentSelectionEmpty(" "\n\t}"
    neural_empty_preflight_under_test.h)
extract("\t\tComputeSubrect BuildFullComputeSubrect(" "\n\t\t}"
    neural_full_compute_subrect_under_test.h)
extract("\t\t[[nodiscard]] const Slot* FindPreparedSlot(" "\n\t\t}"
    neural_prepared_slot_under_test.h)
extract("\t\t[[nodiscard]] bool HasCurrentEmptyProof(" "\n\t\t}"
    neural_empty_proof_under_test.h)
extract("\tvoid CharacterRendering::ResolveFeature18Disposition(" "\n\t}"
    neural_disposition_under_test.h)
extract("\t\t[[nodiscard]] std::shared_ptr<const CharacterPreparationEvidence> FindPreparationEvidence(" "\n\t\t}"
    neural_preparation_evidence_under_test.h)
extract("\t\t[[nodiscard]] CharacterMaskPrepareResult BuildPreparedResult(" "\n\t\t}"
    neural_prepared_result_under_test.h)
extract("\tCharacterPreparedSelection CharacterRendering::GetPreparedSelection(" "\n\t}"
    neural_prepared_selection_under_test.h)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" source)
extract("\tuint32_t BuildNeuralCenterEyeMask(" "\n\t}"
    neural_eye_mask_under_test.h)
extract("\tNeuralStereoEvaluationSummary EvaluatePreparedNeuralStereo(" "\n\t}"
    neural_empty_dispatch_under_test.h)
