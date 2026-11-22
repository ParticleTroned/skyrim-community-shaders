include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)
extract_between("${_source}" "void LightLimitFix::UpdateStructure()"
    "\nvoid LightLimitFix::Hooks::" cluster_structure_under_test.h)
extract_between("${_source}" "lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);"
    "\n}\n\nvoid LightLimitFix::UpdateStructure()" cluster_upload_under_test.h)
