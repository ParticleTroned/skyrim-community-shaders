include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _cache)
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
file(READ "${PROJECT_ROOT}/src/TruePBR.cpp" _pbr)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp" _character)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/CharacterRendering.h" _character_header)
extract_between("${_cache}" "std::shared_ptr<RE::BSGraphics::PixelShader> ShaderCache::GetPixelShader("
    "std::shared_ptr<RE::BSGraphics::ComputeShader> ShaderCache::GetComputeShader(" "pixel_lookup.h")
extract_between("${_header}" "static constexpr uint32_t GetCharacterCategoryFlag("
    "std::shared_ptr<RE::BSGraphics::PixelShader> GetPixelShader(" "category_flag.h")
extract_between("${_hooks}" "struct BSShader__BeginTechnique_SetPixelShader"
    "struct CreateDepthStencil_PrecipitationMask" "pixel_binding.h")
extract_between("${_character}" "void Increment(std::uint64_t&"
    "void Increment(std::uint32_t&" "diagnostic_increment.h")
extract_between("${_character}" "void RecordPreparationFailure("
    "CharacterSnapshot snapshot_{};" "preparation_failure.h")
extract_between("${_character_header}" "struct CharacterPreparationFailure"
    "struct CharacterSnapshot" "preparation_failure_type.h")
extract_between("${_hooks}" "bool Hooks::BSShader_BeginTechnique::thunk("
    "namespace EffectExtensions" "begin_technique.h")
foreach(_guard IN ITEMS
    "state->deferredPixelShaderMissing = false;"
    "return shaderFound && !state->deferredPixelShaderMissing;"
    "skipPixelShader ? nullptr :"
    "cache->GetPixelShader(*shader, state->modifiedPixelDescriptor, SIE::ShaderCache::PixelShaderLookup::Draw)"
    "GetPixelShader(*currentShader, state->modifiedPixelDescriptor, SIE::ShaderCache::PixelShaderLookup::Draw)")
    string(FIND "${_hooks}" "${_guard}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Deferred draw admission guard missing: ${_guard}")
    endif()
endforeach()

extract_between("${_pbr}" "auto* boundShader = *globals::game::currentPixelShader;"
    "const auto* pbr = static_cast<const BSLightingShaderMaterialPBR*>" "grass_material_admission.h")

extract_between("${_header}" "enum class LightingShaderFlags"
    "enum class BloodSplatterShaderTechniques" "lighting_flags.h")
extract_between("${_cache}" "void ShaderCache::PrewarmDeferredPixelShaders("
    "std::shared_ptr<RE::BSGraphics::PixelShader> ShaderCache::GetPixelShader(" "pixel_prewarm.h")
extract_between("${_pbr}" "void TruePBR::GenerateShaderPermutations("
    "struct ExtendedRendererState" "pbr_prewarm.h")
