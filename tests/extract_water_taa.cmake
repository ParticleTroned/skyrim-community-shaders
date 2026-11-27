include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" source)
extract_between("${source}" "void Upscaling::ConfigureTAA()"
    "void Upscaling::ConfigureUpscaling(" "water_taa_under_test.h")
