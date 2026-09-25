if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
file(READ "${PROJECT_ROOT}/src/Utils/Game.h" _game)
file(READ "${PROJECT_ROOT}/src/Utils/D3D.h" _d3d)
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _shader_cache)
file(READ "${PROJECT_ROOT}/src/TruePBR.cpp" _pbr)
file(
    READ
        "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/B/BSLightingShader.h"
    _lighting_shader
)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

function(extract_between source start end output)
    string(FIND "${source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Lighting material guard test cannot find ${start}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Lighting material guard test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(${output} "${_extracted}" PARENT_SCOPE)
endfunction()

extract_between(
    "${_shader_cache}" "enum class LightingShaderTechniques"
    "enum class BloodSplatterShaderTechniques" _lighting_types
)
extract_between(
    "${_lighting_shader}" "enum class TechniqueFlag"
    "uint32_t unk90;" _lighting_technique
)
file(
    WRITE "${OUTPUT_DIRECTORY}/native_lighting_material_types_under_test.h"
    "namespace SIE { struct ShaderCache {\n${_lighting_types}\n}; }\n"
    "namespace RE { struct BSLightingShader : BSShader {\n${_lighting_technique}\n"
    "uint32_t unk90 = 0;\nuint32_t currentRawTechnique = 0;\n}; }\n"
)
extract_between(
    "${_pbr}" "bool TruePBR::UsesCustomMaterialSetup("
    "bool TruePBR::BSLightingShader_SetupMaterial(" _custom_setup
)
file(
    WRITE "${OUTPUT_DIRECTORY}/native_lighting_material_pbr_under_test.h"
    "${_custom_setup}"
)
extract_between(
    "${_pbr}" "bool TruePBR::BSLightingShader_SetupMaterial("
    "auto shadowState = globals::game::shadowState;" _setup_admission
)
string(
    FIND "${_setup_admission}"
    "if (UsesCustomMaterialSetup(shader->currentRawTechnique))"
    _shared_admission
)
string(
    FIND "${_setup_admission}"
    "ShaderConstants::LightingPS::Get()"
    _first_resource_read
)
if(
    _shared_admission EQUAL -1
    OR _first_resource_read LESS_EQUAL _shared_admission
)
    message(
        FATAL_ERROR
        "PBR setup must check shared ownership before reading graphics resources"
    )
endif()

extract_between(
    "${_hooks}" "enum class VRLightingMaterialRejection"
    "#ifdef TRACY_ENABLE" _guard
)
file(
    WRITE "${OUTPUT_DIRECTORY}/native_lighting_material_guard_under_test.h"
    "${_guard}"
)
extract_between(
    "${_game}" "[[nodiscard]] inline bool IsLikelyValidPointer("
    "inline constexpr float DirectionalLightDiscontinuityThreshold" _pointer
)
extract_between(
    "${_d3d}" "inline int GetRenderTargetCount()"
    "inline int GetDepthStencilCount()" _count
)
extract_between(
    "${_d3d}" "[[nodiscard]] inline bool IsValidRenderTargetIndex("
    "HRESULT SaveTextureToFile(" _index
)
file(
    WRITE "${OUTPUT_DIRECTORY}/native_lighting_material_utilities_under_test.h"
    "${_pointer}\n${_count}\n${_index}"
)
