if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)

extract_between(
    "${_source}" "bool IsNearWhiteTint(" "struct EmissiveTintCandidate"
    "billboard_tint_near_white_under_test.h"
)
extract_between(
    "${_source}" "RE::NiColorA BuildBillboardFallbackTint("
    "RE::BSLightingShaderProperty* GetLightingShaderProperty("
    "billboard_tint_fallback_under_test.h"
)
extract_between(
    "${_source}" "RE::NiColorA BuildEffectMaterialEmissiveTint("
    "bool TryGetShadowMaskChannel(" "billboard_tint_material_under_test.h"
)
extract_between(
    "${_source}" "struct VertexColor" "struct VertexPosition"
    "billboard_tint_vertex_color_under_test.h"
)
extract_between(
    "${_source}" "bool TryGetMaxAlphaVertexColor(" "void ResolveBillboardTint("
    "billboard_tint_vertex_scan_under_test.h"
)
extract_between(
    "${_source}" "void ResolveBillboardTint("
    "LightLimitFix::ParticleLightReference LightLimitFix::GetParticleLightConfigs("
    "billboard_tint_resolve_under_test.h"
)
