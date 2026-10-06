if(NOT DEFINED NR_PACKAGE_ROOT OR "${NR_PACKAGE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "NR_PACKAGE_ROOT is required for NR payload validation")
endif()

set(_nr_shader_root "${NR_PACKAGE_ROOT}/Shaders/Upscaling/NeuralRendering")
set(_nr_shader_files
    CopyCompactDepthGuideCS.hlsl
    CopyDepthGuideCS.hlsl
    ColorCommon.hlsli
    ColorExposureCS.hlsl
    ColorMeasureCS.hlsl
    ColorPrepareCS.hlsl
    ColorReconstructCS.hlsl)
file(GLOB_RECURSE _nr_entries LIST_DIRECTORIES TRUE
    RELATIVE "${_nr_shader_root}" "${_nr_shader_root}/*")
foreach(_entry IN LISTS _nr_entries)
    list(FIND _nr_shader_files "${_entry}" _entry_index)
    if(_entry_index LESS 0 OR IS_DIRECTORY "${_nr_shader_root}/${_entry}")
        message(FATAL_ERROR "Unexpected NR shader payload: ${_nr_shader_root}/${_entry}")
    endif()
endforeach()

file(GLOB_RECURSE _package_files LIST_DIRECTORIES FALSE "${NR_PACKAGE_ROOT}/*")
foreach(_file IN LISTS _package_files)
    get_filename_component(_name "${_file}" NAME)
    string(TOLOWER "${_name}" _name)
    if(_name STREQUAL "internal-no-redistribution.txt")
        message(FATAL_ERROR "Internal provider notices do not belong in NR releases: ${_file}")
    endif()
    if(_name MATCHES "^(nvngx_dlssnr|sl\\.dlss_nr).*\\.dll$")
        message(FATAL_ERROR "NR DLLs must be installed by the user, never packaged: ${_file}")
    endif()
endforeach()
