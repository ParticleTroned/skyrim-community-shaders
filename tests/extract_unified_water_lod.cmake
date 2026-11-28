if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater.cpp" _water)
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater/WaterLOD.h" _ownership)
string(REPLACE "\r\n" "\n" _water "${_water}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
string(FIND "${_ownership}" "class UnifiedWaterLOD" _start)
if(_start LESS 0)
    message(FATAL_ERROR "Missing terrain water ownership class")
endif()
string(SUBSTRING "${_ownership}" ${_start} -1 _body)
file(WRITE "${OUTPUT_DIRECTORY}/water_lod_ownership_under_test.h" "${_body}")
extract_between("${_water}"
    "static bool DisableVanillaFlowmapUpdates()"
    "// Engine behavior:"
    "water_flowmap_patch_under_test.h")
extract_between("${_water}"
    "static void ClearWaterNodeChildren("
    "static bool ShouldCullAtCell("
    "water_node_cleanup_under_test.h")
extract_between("${_water}"
    "bool UnifiedWater::IsGeneratedWater("
    "void UnifiedWater::TES_SetWorldSpace::thunk"
    "water_lod_visibility_under_test.h")
extract_between("${_water}"
    "void UnifiedWater::TESWaterSystem_AddLODWater::thunk"
    "void UnifiedWater::BSWaterShader_SetupGeometry::thunk"
    "water_lod_hooks_under_test.h")
