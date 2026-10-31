if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/MeshBlending.cpp" _implementation)
file(READ "${PROJECT_ROOT}/src/Features/MeshBlending.h" _header)
file(READ "${PROJECT_ROOT}/src/Features/MeshBlendingPolicy.h" _policy)
file(READ "${PROJECT_ROOT}/src/Utils/FileSystem.cpp" _file_system)
file(READ "${PROJECT_ROOT}/src/State.h" _state)
file(READ "${PROJECT_ROOT}/package/Shaders/Common/Permutation.hlsli" _permutation)
file(READ "${PROJECT_ROOT}/features/Mesh Blending/Shaders/MeshBlending/MeshBlending.hlsli" _mesh_shader)
set(_runtime "${_implementation}${_header}${_policy}")

string(REGEX MATCH "MeshBlending = 1u << ([0-9]+)" _cpu_flag "${_state}")
if(NOT _cpu_flag)
    message(FATAL_ERROR "Mesh Blending CPU descriptor flag is missing")
endif()
set(_cpu_bit "${CMAKE_MATCH_1}")
string(REGEX MATCH "MeshBlending = \\(1u << ([0-9]+)\\)" _shader_flag "${_permutation}")
if(NOT _shader_flag OR NOT "${CMAKE_MATCH_1}" STREQUAL "${_cpu_bit}")
    message(FATAL_ERROR "Mesh Blending CPU/HLSL descriptor flags disagree")
endif()
string(REGEX MATCH "kEnableMask = 1u << ([0-9]+)" _shader_gate "${_mesh_shader}")
if(NOT _shader_gate OR NOT "${CMAKE_MATCH_1}" STREQUAL "${_cpu_bit}")
    message(FATAL_ERROR "Mesh Blending shader gate differs from the CPU flag")
endif()

# Host tests execute parsing, save publication, identity comparison and route
# traversal; this contract guards their native-engine integration boundaries.
foreach(_required IN ITEMS
    "BuildOverlaySourceState"
    "GetAlphaBlending()"
    "GetAlphaTesting()"
    "AlphaFunction::kSrcAlpha"
    "AlphaFunction::kInvSrcAlpha"
    "ShaderFlag::kZBufferWrite"
    "RE::FormType::Static"
    "GetLinkedChild"
    "GetBoundedOwner"
    "ResolveBoundedOwner"
    "Util::GetOwnUserData"
    "GetAppCulled()"
    "GetChildren().capacity()"
    "ResolveStaticOwner(a_source)"
    "cached->signature != signature"
    "MatchesRuleIdentity"
    "RequiresReceiverValidation(cached->classification)"
    "ValidateCachedReceiver(a_source, *cached)"
    "ResolveLiveRoute"
    "current && IsSafeReceiver(a_source, current->AsGeometry())"
    "CanReuseCacheHit"
    "a_source.root->HasAnimation()"
    "HasOpaqueSibling"
    "kMaximumTraversalObjects"
    "BuildCanonicalNodePath"
    "BuildNodeCacheIdentity"
    "Classification::kDetectedByRule"
    "ReadTextFileBounded"
    "kMaximumRuleFileBytes"
    "CanonicalizeOverrideSelectors"
    ".Model has no effective path after canonicalization"
    ".NodePath has no effective path after canonicalization"
    "WriteTextFileAtomicIfUnchanged"
    "a_state.sourceContents"
    "NifEnabled"
    "Util::ClampFinite"
    "static_assert(sizeof(PerFrame) == 32)"
    "ControlDevBench"
)
    string(FIND "${_runtime}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Mesh Blending native contract is missing: ${_required}")
    endif()
endforeach()

if(_implementation MATCHES "(a_entry\\.receiver|cached->receiver)->")
    message(FATAL_ERROR "A cached receiver address must not be dereferenced")
endif()

foreach(_forbidden IN ITEMS
    "LandscapeLayerBlending"
    "LandscapeBlendStrength"
    "EnableLandscapeBlending"
    "CaptureLandscapeMaterials"
    "LandscapeAssignments"
    "DiscoveryDiagnostics"
    "SurfaceMaterials"
    "BlendPairs"
    "LandscapeDiagnostic"
    "NormalizeTexturePath"
    "LandscapeClassMask"
)
    string(FIND "${_runtime}" "${_forbidden}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "Future LAND implementation remains in this stage: ${_forbidden}")
    endif()
endforeach()

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

foreach(_forbidden IN ITEMS
    "ProjectedSnowEnabled"
    "ProjectedSnowEdgeWidth"
    "TryPrepareProjectedSnow"
)
    string(FIND "${_runtime}" "${_forbidden}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "Future snow implementation remains in the NIF stage: ${_forbidden}")
    endif()
endforeach()

string(FIND "${_implementation}" "->GetUserData()" _recursive_owner)
if(NOT _recursive_owner EQUAL -1)
    message(FATAL_ERROR "NIF ownership lookup must not recurse through unbounded engine parent chains")
endif()
