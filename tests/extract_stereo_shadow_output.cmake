include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/ScreenSpaceShadows.cpp" _source)
extract_between("${_source}" "const bool publishSecondary ="
    "\n\tstd::array<FoveatedCommon::DispatchBounds" stereo_output_selection.h)
