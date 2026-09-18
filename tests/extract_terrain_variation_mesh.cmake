file(READ "${PROJECT_ROOT}/src/Features/TerrainVariation.cpp" _source)
string(
    FIND "${_source}"
    "void TerrainVariation::SetMeshSupportEnabled("
    _setter_begin
)
string(FIND "${_source}" "void TerrainVariation::DataLoaded(" _setter_end)
string(
    FIND "${_source}"
    "void TerrainVariation::UpdateMeshPermutation("
    _update_begin
)
if(_setter_begin LESS 0 OR _setter_end LESS 0 OR _update_begin LESS 0)
    message(FATAL_ERROR "Terrain Variation production functions were not found")
endif()
math(EXPR _setter_length "${_setter_end} - ${_setter_begin}")
string(SUBSTRING "${_source}" ${_setter_begin} ${_setter_length} _setter)
string(SUBSTRING "${_source}" ${_update_begin} -1 _update)
get_filename_component(_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
file(WRITE "${OUTPUT_FILE}" "${_setter}\n${_update}")
