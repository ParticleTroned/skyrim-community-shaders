include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" source)
extract_between("${source}" "[[nodiscard]] bool HasPendingPerfModeRenderTargetWork()"
    "/** Service settings relatches" "state_draw_gate_under_test.h")
file(READ "${PROJECT_ROOT}/src/State.cpp" source)
extract_between("${source}" "ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::PerfModeRelatch,"
    "\n\t\t}" "state_draw_pending_under_test.h")
