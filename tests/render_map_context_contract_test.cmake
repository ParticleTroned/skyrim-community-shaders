if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
file(READ "${PROJECT_ROOT}/src/RenderMap/D3DContextHooks.cpp" _hooks)
file(READ "${PROJECT_ROOT}/src/RenderMap/Runtime.h" _runtime)
file(READ "${PROJECT_ROOT}/tests/render_map_context_hook_test.h" _fixture)
file(READ "${PROJECT_ROOT}/CMakeLists.txt" _targets)
foreach(_binding IN ITEMS
    "using ContextOriginal = REL::Relocation<Signature>;"
    "using ContextOriginal = Signature*;"
    "#ifndef CSX_RENDER_MAP_CONTEXT_TESTING"
    "render_map_context_hook_test.h"
)
    string(FIND "${_hooks}" "${_binding}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Context hook source binding missing: ${_binding}")
    endif()
endforeach()
string(REGEX MATCHALL "class[ \t]+CaptureObservationView" _facades "${_runtime}")
string(REGEX MATCHALL "struct[ \t]+ContextObservation" _records "${_runtime}")
list(LENGTH _facades _facade_count)
list(LENGTH _records _record_count)
string(FIND "${_runtime}" "class ContextObservation" _collision)
if(NOT _facade_count EQUAL 1 OR NOT _record_count EQUAL 1 OR NOT _collision EQUAL -1)
    message(FATAL_ERROR "Capture publication facade and context record must have distinct types")
endif()
foreach(_method IN ITEMS
    DrawIndexed Dispatch Map FinishCommandList PSSetShader VSSetShader CSSetShader
    ExecuteCommandList CopyResource VSSetShaderResources HSSetShaderResources
    DSSetShaderResources GSSetShaderResources PSSetShaderResources CSSetShaderResources
)
    string(FIND "${_fixture}" "decltype(&ID3D11DeviceContext_${_method}::thunk)" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Concrete SDK signature case missing: ${_method}")
    endif()
endforeach()
foreach(_forbidden IN ITEMS D3D11CreateDevice "InstallD3DContextHooks(" LoadLibrary DetourAttach)
    string(FIND "${_fixture}" "${_forbidden}" _found)
    if(NOT _found EQUAL -1)
        message(FATAL_ERROR "CPU fixture must not install hooks or create a device: ${_forbidden}")
    endif()
endforeach()
string(FIND "${_targets}" "render_map_context_hooks_test" _target_start)
if(_target_start EQUAL -1)
    message(FATAL_ERROR "Concrete context target is not registered")
endif()
string(SUBSTRING "${_targets}" ${_target_start} -1 _target)
string(FIND "${_target}" "add_test(NAME RenderMapContextHooks" _target_end)
if(_target_end EQUAL -1)
    message(FATAL_ERROR "Concrete context assertion entry is not registered")
endif()
string(SUBSTRING "${_target}" 0 ${_target_end} _target)
foreach(_input IN ITEMS
    src/RenderMap/D3DContextHooks.cpp src/RenderMap/Runtime.cpp
    src/RenderMap/Collector.cpp tests/render_map_context_hooks_test.cpp
    CSX_RENDER_MAP_TESTING=1 CSX_RENDER_MAP_CONTEXT_TESTING=1
)
    string(FIND "${_target}" "${_input}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Concrete context fixture target input missing: ${_input}")
    endif()
endforeach()
message(STATUS "Concrete context hook source, target and distinct-type contract PASS")