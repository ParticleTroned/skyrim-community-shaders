include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/EngineFixes/EmptyOverlayCullFix.cpp" source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${source}" "\tbool IsEmptyOverlay(" "\n\tstruct GetRenderPasses" "empty_overlay_under_test.h")
