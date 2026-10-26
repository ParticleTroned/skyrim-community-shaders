include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
extract_between(
    "${_source}" "RE::BSGraphics::VertexShader* ShaderCache::GetVertexShaderIfCached("
    "template <typename Interface, typename Cache>" "shader_block_vertex_under_test.h"
)
extract_between(
    "${_source}" "RE::BSGraphics::PixelShader* ShaderCache::GetPixelShader("
    "ShaderCache::~ShaderCache()" "shader_block_pixel_compute_under_test.h"
)
