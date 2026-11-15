include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(WRITE "${OUTPUT_DIRECTORY}/shader_block_lookup_contract.h" "")
foreach(_lookup IN ITEMS VertexShaderLookup PixelShaderLookup)
    string(REGEX MATCH "enum class ${_lookup}[^}]*};" _declaration "${_header}")
    if(_declaration STREQUAL "")
        message(FATAL_ERROR "Source extraction cannot find ${_lookup}")
    endif()
    file(APPEND "${OUTPUT_DIRECTORY}/shader_block_lookup_contract.h" "${_declaration}\n")
endforeach()
extract_between("${_header}" "static constexpr uint32_t kLightingDryWeather"
    "enum class LightingShaderFlags" "shader_block_lighting_constants.h")
extract_between(
    "${_source}" "RE::BSGraphics::VertexShader* ShaderCache::GetVertexShaderIfCached("
    "template <typename Interface, typename Cache>" "shader_block_vertex_under_test.h"
)
extract_between(
    "${_source}" "RE::BSGraphics::PixelShader* ShaderCache::GetPixelShader("
    "void ShaderCache::SelectLightingPixelShader(" "shader_block_pixel_under_test.h"
)
extract_between(
    "${_source}" "RE::BSGraphics::ComputeShader* ShaderCache::GetComputeShader("
    "ShaderCache::~ShaderCache()" "shader_block_compute_under_test.h"
)
