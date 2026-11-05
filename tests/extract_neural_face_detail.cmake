if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp" source)
extract_between("${source}" "RE::NiPoint3 GetProjectionEyePosition(" "inline constexpr std::uint32_t kCharacterMaskRoiTileSize"
    "face_detail_camera_under_test.h")
extract_between("${source}" "void CharacterRendering::ObserveFaceDetail(" "bool CharacterRendering::ShouldAuthorActor("
    "face_detail_observation_under_test.h")
extract_between("${source}" "struct FaceDetailFrame" "CharacterCategoryFramePolicy categoryFramePolicy_"
    "face_detail_state_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.h" source)
extract_between("${source}" "struct CharacterActorBound" "/** Stable actor/head bounds"
    "face_detail_bound_under_test.h")
file(READ "${PROJECT_ROOT}/src/Utils/Game.h" source)
extract_between("${source}" "constexpr float GAME_UNIT_TO_CM" "// Wind speed conversions"
    "face_detail_units_constants_under_test.h")
extract_between("${source}" "inline float GameUnitsToMeters(" "inline float GameUnitsToCm("
    "face_detail_units_under_test.h")
