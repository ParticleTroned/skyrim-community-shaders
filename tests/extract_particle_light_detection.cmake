if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)

extract_between(
    "${_source}" "static constexpr uint MAX_LIGHTS ="
    "\n" "particle_light_detection_cap_under_test.h"
)
extract_between(
    "${_source}" "struct LightLimitFix::ParticleLightSnapshot"
    "\nnamespace\n{" "particle_light_detection_snapshot_under_test.h"
)
extract_between(
    "${_source}" "void LightLimitFix::SetParticleLightDetectionEnabled("
    "\nvoid LightLimitFix::Prepass()" "particle_light_detection_methods_under_test.h"
)
file(READ "${OUTPUT_DIRECTORY}/particle_light_detection_methods_under_test.h" _methods)
# Only allocation is substituted; production failure handling remains under test.
string(REGEX MATCHALL "std::make_shared<ParticleLightSnapshot>[(][)]" _allocations "${_methods}")
list(LENGTH _allocations _allocation_count)
if(NOT _allocation_count EQUAL 1)
    message(FATAL_ERROR "Particle snapshot allocation boundary must occur exactly once")
endif()
string(REPLACE "std::make_shared<ParticleLightSnapshot>()" "AllocateParticleSnapshot<ParticleLightSnapshot>()" _methods "${_methods}")
file(WRITE "${OUTPUT_DIRECTORY}/particle_light_detection_methods_under_test.h" "${_methods}")
extract_between(
    "${_source}" "float LightLimitFix::CalculateLightDistance("
    "\nfloat3 LightLimitFix::Saturation(" "particle_light_detection_emission_under_test.h"
)
extract_between(
    "${_source}" "void LightLimitFix::UpdateLights()"
    "\n\t// Cache data since cameraData" "particle_light_detection_update_guard_under_test.h"
)
file(APPEND "${OUTPUT_DIRECTORY}/particle_light_detection_update_guard_under_test.h" "\n\tstatic_cast<void>(isl);\n}\n")
extract_between(
    "${_source}" "void LightLimitFix::Prepass()"
    "\n\tCS_GPU_PASS(" "particle_light_detection_prepass_guard_under_test.h"
)
file(APPEND "${OUTPUT_DIRECTORY}/particle_light_detection_prepass_guard_under_test.h" "\n}\n")
