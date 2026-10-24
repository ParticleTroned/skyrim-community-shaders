# Shader-only contract. Runtime admission, persistence and buffer layout are
# checked by their own production-backed contracts.
if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl" _mesh_scope_lighting)
file(READ "${PROJECT_ROOT}/features/Mesh Blending/Shaders/MeshBlending/MeshBlending.hlsli" _mesh_scope_math)
file(READ "${PROJECT_ROOT}/package/Shaders/Tests/TestMeshBlending.hlsl" _mesh_scope_tests)
file(READ "${PROJECT_ROOT}/package/Shaders/Common/Permutation.hlsli" _mesh_scope_permutation)

foreach(_mesh_scope_forbidden IN ITEMS
    "RemapLandscapeWeights"
    "MeshBlendingLandscape"
    "kLandscapeClass"
    "LandscapeBlendStrength"
    "EnableLandscapeBlending"
    "MESH_BLENDING_LANDSCAPE_AVAILABLE"
)
    string(FIND "${_mesh_scope_lighting}${_mesh_scope_math}${_mesh_scope_tests}${_mesh_scope_permutation}" "${_mesh_scope_forbidden}" _mesh_scope_position)
    if(NOT _mesh_scope_position EQUAL -1)
        message(FATAL_ERROR "Future LAND policy is present in this shader stage: ${_mesh_scope_forbidden}")
    endif()
endforeach()

string(REGEX MATCH
    "#[ \t]*(if|elif)[^\n]*defined\\(MESH_BLENDING\\)[^\n]*\n[ \t]*!defined\\(LOD\\)[^\n]*!defined\\(LANDSCAPE\\)"
    _mesh_scope_static_guard "${_mesh_scope_lighting}")
if(NOT _mesh_scope_static_guard)
    message(FATAL_ERROR "The NIF shader gate must explicitly exclude LANDSCAPE")
endif()

foreach(_mesh_scope_required IN ITEMS
    "MeshBlending::ComputeFade(screenUV, input.Position.z, eyeIndex)"
    "LandscapeSeams::Load("
    "LANDSCAPE_SEAMS_BLEND_EXTRAS"
)
    string(FIND "${_mesh_scope_lighting}" "${_mesh_scope_required}" _mesh_scope_position)
    if(_mesh_scope_position EQUAL -1)
        message(FATAL_ERROR "Shader integration is missing: ${_mesh_scope_required}")
    endif()
endforeach()

foreach(_mesh_scope_forbidden IN ITEMS
    "MESH_BLENDING_PROJECTED_SNOW_AVAILABLE"
    "ComputeProjectedSnowCoverage"
    "meshBlendingProjectedSnowCoverage"
    "ProjectedSnowEdgeWidth"
)
    string(FIND "${_mesh_scope_lighting}${_mesh_scope_math}${_mesh_scope_tests}" "${_mesh_scope_forbidden}" _mesh_scope_position)
    if(NOT _mesh_scope_position EQUAL -1)
        message(FATAL_ERROR "Future projected-snow code is present in the NIF shader stage: ${_mesh_scope_forbidden}")
    endif()
endforeach()
