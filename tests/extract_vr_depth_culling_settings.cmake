if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(READ "${PROJECT_ROOT}/src/Features/VR.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/VR.h" _header)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
function(extract_between source start end output)
    string(FIND "${source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Depth culling UI integration cannot find ${start}")
    endif()
    string(SUBSTRING "${source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "Depth culling UI integration cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(${output} "${_extracted}" PARENT_SCOPE)
endfunction()
extract_between("${_header}"
    "bool EnableDepthBufferCullingExterior"
    "\n\t\t// Post-composite VR stereo"
    _settings
)
extract_between("${_source}"
    "\tvoid DrawDepthCullingSettings("
    "\n}\n\nvoid VR::DrawPerformanceSettings("
    _draw
)
extract_between("${_source}"
    "void VR::UpdateDepthBufferCulling()"
    "void VR::SetDepthCullingLegacyMode("
    _update
)
file(
    WRITE "${OUTPUT_DIRECTORY}/vr_depth_culling_settings_members.h"
    "${_settings}"
)
file(
    WRITE "${OUTPUT_DIRECTORY}/vr_depth_culling_settings_ui_under_test.h"
    "${_draw}\n${_update}"
)
