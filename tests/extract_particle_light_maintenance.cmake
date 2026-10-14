if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.h" _header)
extract_between(
    "${_source}" "constexpr std::size_t kParticleLightCachePruneBudget ="
    "// StartGroupingAlphas" "particle_light_maintenance_constants_under_test.h"
)
extract_between(
    "${_header}" "struct ParticleEmitterLight"
    "RE::NiPoint3 eyePositionCached" "particle_light_maintenance_declarations_under_test.h"
)
extract_between(
    "${_source}" "void LightLimitFix::Reset()"
    "void LightLimitFix::LoadSettings(" "particle_light_maintenance_methods_under_test.h"
)
extract_between(
    "${_source}" "LightLimitFix::ParticleLightReference LightLimitFix::GetParticleLightConfigs("
    "bool LightLimitFix::CheckParticleLights(" "particle_light_maintenance_lookup_under_test.h"
)
