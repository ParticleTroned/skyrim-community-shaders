if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater/Flowmap.cpp" _flowmap)
file(READ "${PROJECT_ROOT}/src/Features/UnifiedWater.cpp" _water)
file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)

function(extract_between source start end output)
    string(FIND "${source}" "${start}" _start)
    if(_start LESS 0)
        message(FATAL_ERROR "Cannot find ${start}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end LESS 0)
        message(FATAL_ERROR "Cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _body)
    file(APPEND "${OUTPUT_DIRECTORY}/${output}" "${_body}\n")
endfunction()

file(WRITE "${OUTPUT_DIRECTORY}/unified_water_flowmap_under_test.h" "")
extract_between("${_flowmap}" "bool Flowmap::IsValid()" "bool Flowmap::GenerateFlowmap(" "unified_water_flowmap_under_test.h")
extract_between("${_water}" "void UnifiedWater::DataLoaded()" "RE::BSEventNotifyControl UnifiedWater::MenuOpenCloseEventHandler::ProcessEvent(" "unified_water_flowmap_under_test.h")
extract_between("${_water}" "void UnifiedWater::SetFlowmapTex() const" "void UnifiedWater::PostPostLoad()" "unified_water_flowmap_under_test.h")
extract_between("${_water}" "bool UnifiedWater::IsWaterDataReady() const" "bool UnifiedWater::IsExteriorWorldspaceActive() const" "unified_water_flowmap_under_test.h")

# Keep the fallback at binding sites so startup cache warming still runs.
string(
    REGEX MATCHALL "!UseNativeWaterShaders\\(\\*currentShader\\)"
    _bindings
    "${_hooks}"
)
list(LENGTH _bindings _binding_count)
if(
    NOT _binding_count EQUAL 2
    OR NOT _hooks MATCHES "!UseNativeWaterShaders\\(\\*shader\\)"
)
    message(
        FATAL_ERROR
        "Native water fallback must cover all three shader binding paths"
    )
endif()
