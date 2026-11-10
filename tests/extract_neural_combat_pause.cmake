include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" source)
extract_between("${source}"
    "NeuralRendering::CombatPauseSnapshot Upscaling::GetNeuralCombatPauseSnapshot()"
    "uint32_t Upscaling::GetEffectiveUpscalingQualityMode()"
    "neural_combat_pause_under_test.h")
