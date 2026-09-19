cmake_minimum_required(VERSION 4.2)

foreach(_required IN ITEMS BUILD_ROOT SDK_ROOT TEST_CONFIG)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(_prefix "${BUILD_ROOT}/Testing/StreamlineRuntime")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --install "${BUILD_ROOT}" --config "${TEST_CONFIG}"
        --prefix "${_prefix}" --component StreamlineRuntime
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error
)
if(NOT _install_result EQUAL 0)
    message(
        FATAL_ERROR
        "Streamline runtime install failed: ${_install_output}${_install_error}"
    )
endif()

# The component manifest proves completeness without a preceding Shaders install.
file(STRINGS "${BUILD_ROOT}/install_manifest_StreamlineRuntime.txt" _installed)
set(_expected "")
foreach(
    _relative_path
    IN
    ITEMS
        bin/x64/nvngx_dlss.dll
        bin/x64/sl.common.dll
        bin/x64/sl.dlss.dll
        bin/x64/sl.interposer.dll
        bin/x64/sl.pcl.dll
        bin/x64/sl.reflex.dll
        license.txt
        bin/x64/nvngx_dlss.license.txt
        bin/x64/reflex.license.txt
        3rd-party-licenses.md
        "NVIDIA Nsight Graphics SDK License (Apache 2.0).txt"
)
    get_filename_component(_filename "${_relative_path}" NAME)
    set(_destination "${_prefix}/Shaders/Upscaling/Streamline/${_filename}")
    list(APPEND _expected "${_destination}")
    if(NOT _destination IN_LIST _installed)
        message(FATAL_ERROR "StreamlineRuntime did not install ${_filename}")
    endif()
    file(SHA256 "${SDK_ROOT}/${_relative_path}" _source_hash)
    file(SHA256 "${_destination}" _installed_hash)
    if(NOT _source_hash STREQUAL _installed_hash)
        message(
            FATAL_ERROR
            "Installed ${_filename} differs from the verified SDK"
        )
    endif()
endforeach()

if(DEFINED NR_RUNTIME_FILE AND NOT "${NR_RUNTIME_FILE}" STREQUAL "")
    set(_nr_destination "${_prefix}/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll")
    list(APPEND _expected "${_nr_destination}")
    if(NOT _nr_destination IN_LIST _installed)
        message(FATAL_ERROR "StreamlineRuntime did not install the configured NR provider")
    endif()
    file(SHA256 "${NR_RUNTIME_FILE}" _nr_source_hash)
    file(SHA256 "${_nr_destination}" _nr_installed_hash)
    if(NOT _nr_source_hash STREQUAL _nr_installed_hash)
        message(FATAL_ERROR "Installed NR provider differs from the configured source")
    endif()
endif()

list(SORT _installed)
list(SORT _expected)
if(NOT _installed STREQUAL _expected)
    message(
        FATAL_ERROR
        "StreamlineRuntime installed unexpected or duplicate files: ${_installed}"
    )
endif()
message(
    STATUS
    "StreamlineRuntime installs the exact configured DLLs and five original notices"
)
