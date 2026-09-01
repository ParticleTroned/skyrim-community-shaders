file(READ "${PROJECT_ROOT}/src/Features/TerrainVariation.cpp" _source)
string(
    FIND "${_source}"
    "void TerrainVariation::SetMeshSupportEnabled("
    _setter_begin
)
string(FIND "${_source}" "void TerrainVariation::DataLoaded(" _load_begin)
string(
    FIND "${_source}"
    "bool TerrainVariation::IsLandscapeDiffuseTexture("
    _lookup_begin
)
string(
    FIND "${_source}"
    "void TerrainVariation::UpdateMeshPermutation("
    _update_begin
)
if(
    _setter_begin LESS 0
    OR _load_begin LESS 0
    OR _lookup_begin LESS 0
    OR _update_begin LESS 0
)
    message(FATAL_ERROR "Terrain Variation production functions were not found")
endif()
string(SUBSTRING "${_source}" ${_setter_begin} -1 _functions)
get_filename_component(_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
file(WRITE "${OUTPUT_FILE}" "${_functions}")
