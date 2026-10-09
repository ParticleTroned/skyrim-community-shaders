include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" source)
extract_between("${source}" "bool Upscaling::IsTextureStreamingTransitionActive() const noexcept"
    "bool Upscaling::IsNeuralRenderingInsertionTransitionBlocked()" "texture_streaming_gate_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" source)
extract_between("${source}" "enum class VRRenderScaleTransitionState : uint8_t"
    "// Orthogonal diagnostic state." "texture_streaming_states_under_test.h")
