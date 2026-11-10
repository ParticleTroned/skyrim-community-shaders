if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater/ReflectionCapture.h" _capture)
string(FIND "${_capture}" "class UnifiedWaterReflectionCapture" _start)
if(_start LESS 0)
    message(FATAL_ERROR "Missing reflection ownership class")
endif()
string(SUBSTRING "${_capture}" ${_start} -1 _body)
file(WRITE "${OUTPUT_DIRECTORY}/reflection_capture_under_test.h" "${_body}")
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater.cpp" _water)
string(REPLACE "\r\n" "\n" _water "${_water}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
extract_between("${_water}"
    "namespace\n{\n\tRE::BSWaterShaderProperty* GetWaterProperty"
    "void UnifiedWater::UpdateWaterLODCull() const"
    "reflection_maintenance_under_test.h")
