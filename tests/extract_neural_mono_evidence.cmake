if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/MonoRenderer.cpp" source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${source}" "nlohmann::json MonoRenderer::Diagnostics(std::uint32_t sourceFrame) const"
    "#endif" "neural_mono_evidence_under_test.h")
