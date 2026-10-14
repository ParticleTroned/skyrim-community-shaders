if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
extract_between("${_source}"
    "\ttemplate <ShaderClass Stage, typename ShaderMaps, typename MutexType>"
    "\n\tShaderCache::~ShaderCache()" "shader_resident_lookup_under_test.h")
extract_between("${_header}" "static constexpr uint32_t GetCharacterCategoryFlag("
    "std::shared_ptr<RE::BSGraphics::PixelShader> GetPixelShader(" "shader_resident_category.h")
extract_between("${_source}" "\ttemplate <typename ShaderType, typename MutexType>"
    "\n\tvoid ShaderCache::EvictShaderResources(" "shader_resident_release_under_test.h")
