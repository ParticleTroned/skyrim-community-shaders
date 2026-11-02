if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.cpp" source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${source}" "constexpr REL::Version kMinimumRuntimeVersion"
    "constexpr auto kFeatureDlssNr" "neural_runtime_minimum_under_test.h")
extract_between("${source}" "bool Runtime::ProbeLocked("
    "bool Runtime::Probe(" "neural_runtime_probe_under_test.h")
extract_between("${source}" "bool Runtime::FeatureConfiguration::Matches("
    "Runtime& Runtime::Instance()" "neural_native_key_under_test.h")
extract_between("${source}" "bool Runtime::PrepareFeature("
    "bool Runtime::ResetFeatureLocked(" "neural_native_creation_under_test.h")
extract_between("${source}" "bool Runtime::Initialize("
    "bool Runtime::PrepareScratchLocked(" "neural_ngx_initialize_under_test.h")
extract_between("${source}" "bool Runtime::ShutdownLocked()"
    "void Runtime::AbandonLocked()" "neural_ngx_shutdown_under_test.h")
extract_between("${source}" "NgxAllocationStatistics Runtime::QueryAllocationStatistics() const"
    "#endif" "neural_ngx_statistics_under_test.h")
extract_between("${source}" "struct InstalledRuntimeMetadata"
    "bool ComputeFileSha256(" "neural_installed_metadata_under_test.h")
extract_between("${source}" "TuningLimits Runtime::GetTuningLimits(std::uint32_t selection) const"
    "std::string Runtime::Version() const" "neural_runtime_tuning_under_test.h")
extract_between("${source}" "void Runtime::SetFailureLocked("
    "bool Runtime::IsInstalled()" "neural_runtime_logging_under_test.h")
extract_between("${source}" "std::string Runtime::DetailLocked() const"
    "std::uint32_t Runtime::NgxResult() const" "neural_runtime_detail_under_test.h")
extract_between("${source}" "bool Runtime::ResetFeatureLocked("
    "bool Runtime::HasCompatibleFeature(" "neural_runtime_reset_under_test.h")
extract_between("${source}" "bool Runtime::ResetFeaturesLocked("
    "bool Runtime::ShutdownLocked()" "neural_runtime_retirement_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h" header)
extract_between("${header}" "struct EvaluationSuccessDetail\n"
    "struct Backend;\n" "neural_evaluation_success_detail_under_test.h")

file(READ "${NR_SDK_INCLUDE_DIR}/nvsdk_ngx_defs_dlssnr.h" nr_sdk)
file(READ "${PROJECT_ROOT}/extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx_defs.h" ngx_sdk)
string(REGEX MATCHALL "#define[ \t]+NVSDK_NGX_Parameter_[^\r\n]+" sdk_parameters "${nr_sdk}\n${ngx_sdk}")
list(JOIN sdk_parameters "\n" sdk_parameter_header)
file(WRITE "${OUTPUT_DIRECTORY}/neural_sdk_parameters_under_test.h" "#pragma once\n${sdk_parameter_header}\n")
