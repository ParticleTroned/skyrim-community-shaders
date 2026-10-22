if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/MeshBlending.cpp" _implementation)
file(READ "${PROJECT_ROOT}/src/Features/MeshBlending.h" _header)
file(READ "${PROJECT_ROOT}/src/Utils/FileSystem.cpp" _file_system)
file(READ "${PROJECT_ROOT}/src/State.h" _state)
file(READ "${PROJECT_ROOT}/package/Shaders/Common/Permutation.hlsli" _permutation)

string(REGEX MATCH "MeshBlending = 1u << ([0-9]+)" _cpu_flag "${_state}")
if(NOT _cpu_flag)
    message(FATAL_ERROR "Mesh Blending CPU descriptor flag is missing")
endif()
set(_cpu_bit "${CMAKE_MATCH_1}")
string(REGEX MATCH "MeshBlending = \\(1u << ([0-9]+)\\)" _shader_flag "${_permutation}")
if(NOT _shader_flag OR NOT "${CMAKE_MATCH_1}" STREQUAL "${_cpu_bit}")
    message(FATAL_ERROR "Mesh Blending CPU/HLSL descriptor flags disagree")
endif()

file(READ "${PROJECT_ROOT}/features/Mesh Blending/Shaders/MeshBlending/MeshBlending.hlsli" _mesh_shader)
string(REGEX MATCH "kEnableMask = 1u << ([0-9]+)" _shader_gate "${_mesh_shader}")
if(NOT _shader_gate OR NOT "${CMAKE_MATCH_1}" STREQUAL "${_cpu_bit}")
    message(FATAL_ERROR "Mesh Blending shader gate differs from the CPU descriptor flag")
endif()

foreach(_required IN ITEMS
    "Form is not a portable 0xLOCALID~Plugin selector"
    "WriteTextFileAtomicIfUnchanged"
    "a_state.sourceContents"
    "cachedSignature == signature"
    "HasOpaqueSibling"
    "currentReceiver"
    "cachedReceiver"
    "CanReuseCacheHit"
    "cachedSignature.ruleIdentity == signature.ruleIdentity"
    "a_signature.ruleIdentity = { BuildModelPath(a_source), BuildNodePathValue(a_source, true) }"
    "BuildNodeCacheIdentity"
    "CanMatchNodeSelector(rule.nodePath, a_nodePath)"
    "if ((flexibleRulesNeedNodePath || exactModelNeedsNodePath) && nodePath.empty())"
    "BuildCanonicalNodePath"
    "InvalidateCachedClassification"
    "CanonicalizeOverrideSelectors"
    "CanonicalizeLandscapeSelectors"
    ".Model has no effective path after canonicalization"
    ".NodePath has no effective path after canonicalization"
    "has no effective Form, EditorID, or Diffuse selector"
    "ReadTextFileBounded"
    "kMaximumRuleFileBytes"
    "maximumQuadrantObjects = 32u"
    "kMultiTextureLandscape"
)
    string(FIND "${_implementation}${_header}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Mesh Blending safety contract is missing: ${_required}")
    endif()
endforeach()

string(FIND "${_implementation}" "children[0]" _child_zero)
if(NOT _child_zero EQUAL -1)
    message(FATAL_ERROR "LAND capture must not assume quadrant geometry is child zero")
endif()

foreach(_required IN ITEMS
    "currentContents != expectedContents"
    "The destination changed after it was read"
    "Could not conditionally replace the destination"
    "BoundedTextRead::Read"
    "maximumBytes"
)
    string(FIND "${_file_system}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Conditional file persistence is missing: ${_required}")
    endif()
endforeach()
