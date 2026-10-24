if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl" _lighting_shader)
string(REGEX MATCH
    "#[ \t]*if[ \t]+defined\\(LANDSCAPE\\)[ \t]*&&[ \t]*!defined\\(LANDSCAPE_SEAMS\\)[ \t\r\n]*#[ \t]*define[ \t]+MESH_BLENDING_LANDSCAPE_AVAILABLE"
    _landscape_seam_guard "${_lighting_shader}")
if(NOT _landscape_seam_guard)
    message(FATAL_ERROR "Six-layer LAND material policy must exclude borrowed seam layers")
endif()
string(REGEX MATCHALL "MeshBlending::RemapLandscapeWeights\\(" _landscape_remaps "${_lighting_shader}")
list(LENGTH _landscape_remaps _landscape_remap_count)
if(NOT _landscape_remap_count EQUAL 1)
    message(FATAL_ERROR "LAND material policy must be applied exactly once")
endif()

# POM owns its height-blended result. All shadow and material consumers must
# use it without a later policy remap invalidating cached shadow heights.
set(_landscape_stages "${_lighting_shader}")
foreach(_stage IN ITEMS
    "input.LandBlendWeights2.xy /= totalWeight"
    "MeshBlending::RemapLandscapeWeights("
    "SharedData::meshBlendingSettings.LandscapeBlendStrength"
    "input.LandBlendWeights1 = meshBlendingLandscapeWeights1"
    "ExtendedMaterials::GetParallaxCoords(input,"
    "input.LandBlendWeights1.x = weights[0]"
    "hasCachedTerrainShadowBaseHeight = COMPUTE_TERRAIN_SHADOW_BASE(sh0)"
    "cachedDirectionalTerrainParallaxShadow = EVAL_TERRAIN_DIR_SHADOW(sh0,"
    "shininess = dot(input.LandBlendWeights1,"
    "ExtendedMaterials::GetParallaxSoftShadowMultiplierTerrain(input,"
)
    string(FIND "${_landscape_stages}" "${_stage}" _stage_position)
    if(_stage_position EQUAL -1)
        message(FATAL_ERROR "LAND material/shadow pipeline is missing or out of order: ${_stage}")
    endif()
    string(LENGTH "${_stage}" _stage_length)
    math(EXPR _next_stage "${_stage_position} + ${_stage_length}")
    string(SUBSTRING "${_landscape_stages}" ${_next_stage} -1 _landscape_stages)
endforeach()
