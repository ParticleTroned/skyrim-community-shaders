if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/DetailMaps.cpp" source)
extract_between("${source}" "void DetailMaps::Reload()" "int32_t DetailMaps::MatchRule"
    "detail_maps_reload_under_test.h")
extract_between("${source}" "DetailMaps::PassState::PassState" "void DetailMaps::EndPass()"
    "detail_maps_bindings_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/DetailMaps.h" declaration)
extract_between("${declaration}" "\tstruct PassState" "\n\tbool HasContent()"
    "detail_maps_bindings_decl_under_test.h")
