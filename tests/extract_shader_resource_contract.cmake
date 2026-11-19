include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
extract_between("${_upscaling}" "ID3D11ComputeShader* Upscaling::GetFoveatedCenterBlendCS("
    "ID3D11ComputeShader* Upscaling::GetFoveatedSpatialCompositeCS(" "composite_shader_selection.h")
string(FIND "${_upscaling}" "bool Upscaling::DispatchFoveatedBlendPass(" _blend_start)
if(_blend_start EQUAL -1)
    message(FATAL_ERROR "Composite dispatch missing")
endif()
string(SUBSTRING "${_upscaling}" ${_blend_start} -1 _blend)
extract_between("${_blend}" "ID3D11UnorderedAccessView* nullOutput = nullptr;"
    "ID3D11ShaderResourceView* boundSRVs[3]" "composite_bindings.h")
file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
extract_between("${_hooks}" "void BindOptionalMaterialTexture("
    "\n}\n\nnamespace EffectExtensions" "optional_material_binding.h")
extract_between("${_hooks}" "void ClearUnusedEffectPalette("
    "struct BSEffectShader_SetupMaterial" "effect_material_binding.h")
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _cache_header)
extract_between("${_cache_header}" "enum class EffectShaderFlags"
    "// Shader blocking data" "effect_flags.h")
extract_between("${_hooks}" "void BindLightingEnvironmentTexture("
    "struct BSLightingShader_SetupMaterial" "lighting_environment_binding.h")
file(READ "${PROJECT_ROOT}/src/TruePBR.cpp" _pbr)
extract_between("${_pbr}" "const bool hasDisplacement ="
    "const bool hasFeaturesTexture0 =" "pbr_displacement_binding.h")
