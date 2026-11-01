if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.cpp" source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${source}" "bool Runtime::FeatureConfiguration::Matches("
    "Runtime& Runtime::Instance()" "neural_native_key_under_test.h")
extract_between("${source}" "bool Runtime::PrepareFeature("
    "bool Runtime::ResetFeatureLocked(" "neural_native_creation_under_test.h")
extract_between("${source}" "struct InstalledRuntimeMetadata"
    "bool QueryFileIdentity(" "neural_installed_metadata_under_test.h")
extract_between("${source}" "TuningLimits Runtime::GetTuningLimits(std::uint32_t selection) const"
    "std::string Runtime::Version() const" "neural_runtime_tuning_under_test.h")
extract_between("${source}" "void Runtime::SetFailureLocked("
    "bool Runtime::IsInstalled()" "neural_runtime_logging_under_test.h")
extract_between("${source}" "std::string Runtime::DetailLocked() const"
    "std::uint32_t Runtime::NgxResult() const" "neural_runtime_detail_under_test.h")
extract_between("${source}" "bool Runtime::ResetFeatureLocked("
    "bool Runtime::HasCompatibleFeature(" "neural_runtime_reset_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h" header)
extract_between("${header}" "struct EvaluationSuccessDetail\n"
    "struct RuntimeExports\n" "neural_evaluation_success_detail_under_test.h")
