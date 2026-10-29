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
extract_between("${source}" "TuningLimits Runtime::GetTuningLimits() const"
    "std::string Runtime::Version() const" "neural_runtime_tuning_under_test.h")
