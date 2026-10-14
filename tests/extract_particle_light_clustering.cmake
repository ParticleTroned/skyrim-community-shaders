if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)

extract_between(
    "${_source}" "static constexpr uint MAX_LIGHTS ="
    "\n" "particle_light_cap_under_test.h"
)
extract_between(
    "${_source}" "\t\tLightData clusteredLight{};"
    "\n\t}\n\n\tPublishParticleLights(" "particle_light_clustering_under_test.h"
)
extract_between(
    "${_source}" "\tstatic thread_local eastl::vector<LightData> reusableLightsData;"
    "\n\tconst bool isInterior =" "particle_light_scratch_under_test.h"
)
