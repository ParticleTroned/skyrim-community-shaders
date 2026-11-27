if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")

file(READ "${PROJECT_ROOT}/src/Menu/ProfilingRenderer.h" profiling)
extract_between("${profiling}" "struct PerformanceTimingTotals"
    "/** Shared runtime capture switch" "measurement_summary.h")
file(READ "${PROJECT_ROOT}/src/Menu/ProfilingRenderer.cpp" profiling_source)
extract_between("${profiling_source}" "static bool IsPositiveFinite"
    "static bool IsDisplayTimingSampleValid" "measurement_timing_validity.h")
extract_between("${profiling_source}" "static void NormalizeGameFrameTiming"
    "static int ScaleToUiInt" "measurement_cadence.h")
file(READ "${PROJECT_ROOT}/src/Menu/PerformanceTuningRenderer.cpp" source)
extract_between("${source}" "constexpr double kFeatureCostMeasurementSeconds"
    "constexpr std::array<std::string_view," "measurement_constants.h")
extract_between("${source}" "using FeatureCostMoments"
    "enum class UpscalingCostSweepPhase" "measurement_types.h")
extract_between("${source}" "struct FeatureCostBatchState"
    "static std::unordered_map<std::string, std::optional<DisabledFeatureConfiguration>>" "measurement_batch_type.h")
extract_between("${source}" "static FeatureCostBatchState g_featureCostBatch;"
    "static std::string g_featureCostUiMessage;" "measurement_batch_state.h")
extract_between("${source}" "bool IsFeatureCostMeasurementActive"
    "bool IsAnyFeatureCostMeasurementActive" "measurement_active.h")
extract_between("${source}" "double GetFeatureCostRestartCooldownRemaining"
    "\n\tstruct UpscalingCostSweepReadiness" "measurement_cooldown.h")
extract_between("${source}" "bool IsValidFeatureCostTiming"
    "void ResetFeatureCostTrace" "measurement_validity.h")
extract_between("${source}" "void AddFeatureCostMoment"
    "bool TryGetDisplayTimingMs" "measurement_statistics.h")
extract_between("${source}" "void ResolveFlatFeatureCostSamples"
    "RE::FormID GetMeasurementCellId" "measurement_samples.h")
extract_between("${source}" "void ApplyFeatureCostMeasurementTestState"
    "\n\tconst char* GetQualityModeId" "measurement_phases.h")


extract_between("${source}" "const char* GetFeatureCostPhaseName"
    "const char* GetUpscalingCostSweepPhaseName" "measurement_phase_names.h")
extract_between("${source}" "#ifdef DEVBENCH_BRIDGE_ENABLED\n\tstruct FeatureCostTraceSample"
    "static std::unordered_map<std::string, FeatureCostMeasurementState>" "measurement_trace_types.h")
extract_between("${source}" "#ifdef DEVBENCH_BRIDGE_ENABLED\n\tstatic std::deque<FeatureCostTraceSample>"
    "static double g_costMeasurementRestartAllowedTime" "measurement_trace_state.h")
extract_between("${source}" "void ResetFeatureCostTrace"
    "void AddFeatureCostMoment" "measurement_trace_helpers.h")
extract_between("${source}" "json FeatureCostMeasurementStatusJson"
    "json BuildMeasurementStatus" "measurement_status.h")

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" upscaling)
extract_between("${upscaling}" "double GetPerformanceCostMeasurementSettleSeconds"
    "virtual const char* GetPerformanceCostMeasurementWaitText" "measurement_upscaling_settle.h")
file(READ "${OUTPUT_DIRECTORY}/measurement_upscaling_settle.h" settle)

file(READ "${OUTPUT_DIRECTORY}/measurement_summary.h" summary)
file(WRITE "${OUTPUT_DIRECTORY}/performance_tuning_measurement_under_test.h"
    "#pragma once\nstruct Upscaling : Feature {\n${settle}\n};\nstruct ProfilingRenderer {\n${summary}\n};\n")
foreach(part IN ITEMS timing_validity cadence constants types batch_type batch_state active cooldown validity statistics samples phases phase_names trace_types trace_state trace_helpers)
    file(READ "${OUTPUT_DIRECTORY}/measurement_${part}.h" section)
    file(APPEND "${OUTPUT_DIRECTORY}/performance_tuning_measurement_under_test.h"
        "${section}\n")
endforeach()
file(READ "${OUTPUT_DIRECTORY}/measurement_status.h" status)
file(APPEND "${OUTPUT_DIRECTORY}/performance_tuning_measurement_under_test.h"
    "#ifdef DEVBENCH_BRIDGE_ENABLED\n${status}\n#endif\n")
