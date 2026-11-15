include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/State.cpp" source)
extract_between("${source}" "void State::SetDefines(" "std::shared_ptr<const State::ShaderDefinesSnapshot> State::GetShaderDefinesSnapshot()" "performance_cost_defines_under_test.h")
