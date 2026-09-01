if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_FILE are required")
endif()
function(extract_between source begin end output)
    string(FIND "${source}" "${begin}" _begin)
    if(_begin LESS 0)
        message(FATAL_ERROR "Missing production volumetric code: ${begin}")
    endif()
    string(SUBSTRING "${source}" ${_begin} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end LESS 0)
        message(FATAL_ERROR "Missing end of production volumetric code: ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _body)
    set(${output} "${_body}" PARENT_SCOPE)
endfunction()

file(READ "${PROJECT_ROOT}/src/State.h" _state_header)
file(READ "${PROJECT_ROOT}/src/Features/VolumetricLighting.h" _header)
file(READ "${PROJECT_ROOT}/src/Features/VolumetricLighting.cpp" _feature)
file(READ "${PROJECT_ROOT}/src/State.cpp" _state)
extract_between("${_state_header}" "struct alignas(16) SharedDataCB" "\n\t};" _buffer)
extract_between("${_header}" "\tstruct TextureSize" "\n\tSettings settings;" _settings)
extract_between("${_feature}" "\tbool IsImageSpaceReplacementEnabled()" "\n\tbool IsRainWeatherActive(" _replacement)
extract_between("${_feature}" "bool VolumetricLighting::TryGetActiveGodrayProfile(" "bool VolumetricLighting::IsPerformanceCostMeasurementEnabled()" _profile)
extract_between("${_state}" "\t\tconst auto godrayProfile =" "\n\n\t\tdata.SSSHumanMaleIntensity" _upload)
get_filename_component(_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_directory}")
file(WRITE "${OUTPUT_FILE}" "${_buffer}\n\t};\n")
file(
    WRITE "${_directory}/volumetric_lighting_settings_under_test.h"
    "${_settings}\n"
)
file(
    WRITE "${_directory}/volumetric_lighting_runtime_under_test.h"
    "${_replacement}\n${_profile}\nSharedDataCB Upload(bool a_inWorld) {\nSharedDataCB data{};\n${_upload}\nreturn data;\n}\n"
)
