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

# Behavioral parser, cache and failed-write checks execute production code in
# the controller tests. Keep this contract focused on cross-file wiring.
foreach(_required IN ITEMS
    "WriteTextFileAtomicIfUnchanged"
    "a_state.sourceContents"
    "ReadTextFileBounded"
    "kMaximumRuleFileBytes"
    "MatchesRuleIdentity"
    "ResolveLiveRoute"
    "RequiresReceiverValidation"
)
    string(FIND "${_implementation}${_header}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Mesh Blending runtime integration is missing: ${_required}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/State.cpp" _draw_source)
string(FIND "${_draw_source}" "void State::Draw(bool a_isCompute)" _draw_start)
string(SUBSTRING "${_draw_source}" ${_draw_start} -1 _draw_source)
string(FIND "${_draw_source}" "void State::Debug()" _draw_end)
string(SUBSTRING "${_draw_source}" 0 ${_draw_end} _draw_source)
string(FIND "${_draw_source}" "if (!a_isCompute && currentShader" _graphics_guard)
string(FIND "${_draw_source}" "GetCurrentSceneDepthSRV(false)" _depth_selection)
string(FIND "${_draw_source}" "context->PSSetShaderResources(17, 1, &depth)" _depth_bind)
string(FIND "${_draw_source}" "permutationData.ExtraShaderDescriptor &= ~meshBlendingMask" _missing_depth)
string(FIND "${_draw_source}" "permutationCB->Update(permutationData)" _permutation_upload)
if(_graphics_guard LESS 0 OR _depth_selection LESS _graphics_guard OR
   _depth_bind LESS _depth_selection OR _missing_depth LESS _depth_bind OR
   _permutation_upload LESS _missing_depth)
    message(FATAL_ERROR "NIF depth must bind only for graphics, before the updated admission reaches the GPU")
endif()

file(READ "${PROJECT_ROOT}/src/Api/FeatureDevBenchBridge.cpp" _bridge)
foreach(_required IN ITEMS
    "MeshBlendingDevBench::Schema()"
    "MeshBlendingDevBench::StatusSchema()"
    "meshBlending.ControlDevBench"
)
    string(FIND "${_bridge}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Mesh Blending DevBench registration is missing: ${_required}")
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
