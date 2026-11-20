if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" _header)
extract_between("${_header}"
    "enum class VRRenderScalePresentationPath"
    "struct VRRenderScalePresentationObservation"
    "vr_stable_menu_path_under_test.h")
extract_between("${_header}"
    "struct VRRenderScalePresentationEyeSnapshot"
    "\n#ifdef DEVBENCH_BRIDGE_ENABLED\n\tstatic constexpr uint32_t kVRLoadPresentationProbeGridSize"
    "vr_stable_menu_snapshots_under_test.h")
extract_between("${_source}"
    "bool HasExactVRRenderScaleStableStereoPresentation("
    "bool IsBufferedVRFpsStabilizerDoorHandoff("
    "vr_stable_menu_proof_under_test.h")
extract_between("${_source}"
    "\t\tconst auto& left = presentation.eyes[0];"
    "\n\t\tpublished = eye;"
    "vr_stable_menu_observation_under_test.h")
