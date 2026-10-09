if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Menu/ProfilingRenderer.cpp" source)
extract_between("${source}"
    "static constexpr float kGraphHeadroomScale" "#ifdef ENABLE_SKYRIM_VR"
    "profiling_constants.h")
extract_between("${source}"
    "static bool IsDisplayTimingSampleValid" "struct TimingAverage"
    "profiling_validity.h")
extract_between("${source}"
    "static int ScaleToUiInt" "void ProfilingRenderer::RenderTimingModeToggle"
    "profiling_helpers.h")
extract_between("${source}"
    "void ProfilingRenderer::SetupTimingTableColumns" "void ProfilingRenderer::RenderGraph"
    "profiling_columns.h")
extract_between("${source}"
    "ProfilingRenderer::FeatureTimingData ProfilingRenderer::CollectFeatureTimingData"
    "bool ProfilingRenderer::RenderEnabledControl" "profiling_views.h")
extract_between("${source}"
    "void ProfilingRenderer::RenderFeatureTimers"
    "ProfilingRenderer::PerformanceTimingSummary ProfilingRenderer::CapturePerformanceTimingSummary"
    "profiling_controls.h")
file(WRITE "${OUTPUT_DIRECTORY}/feature_profiling_under_test.h" "")
foreach(part IN ITEMS constants validity helpers columns views controls)
    file(READ "${OUTPUT_DIRECTORY}/profiling_${part}.h" section)
    file(APPEND "${OUTPUT_DIRECTORY}/feature_profiling_under_test.h" "${section}\n")
endforeach()
