include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LinearLighting.h" _header)
extract_between("${_header}" "struct alignas(16) PerGeometryData"
    "\n\tConstantBuffer* PerGeometryCB" linear_geometry_payload.h)
file(READ "${PROJECT_ROOT}/src/Features/LinearLighting.cpp" _source)
extract_between("${_source}" "void LinearLighting::SetupResources()"
    "\nvoid LinearLighting::" linear_geometry_resources.h)
string(FIND "${_source}" "void LinearLighting::BSLightingShader_SetupGeometry(" _begin)
if(_begin LESS 0)
    message(FATAL_ERROR "Missing linear geometry upload method")
endif()
string(SUBSTRING "${_source}" ${_begin} -1 _method)
file(WRITE "${OUTPUT_DIRECTORY}/linear_geometry_upload.h" "${_method}")
