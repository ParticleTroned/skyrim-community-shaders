include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/ScreenSpaceGI.cpp" _source)
extract_between("${_source}" "void ScreenSpaceGI::SyncVanillaSSAO()" "void ScreenSpaceGI::DrawSSGI()" vanilla_ssao.h)
