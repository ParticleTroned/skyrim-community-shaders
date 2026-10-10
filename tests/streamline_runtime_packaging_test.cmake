cmake_minimum_required(VERSION 4.2)

foreach(_required IN ITEMS BUILD_ROOT SDK_ROOT TEST_CONFIG)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _run_id)
set(_prefix "${BUILD_ROOT}/Testing/StreamlineRuntime/${_run_id}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_ROOT}" --config "${TEST_CONFIG}"
        --prefix "${_prefix}" --component Shaders
    RESULT_VARIABLE _shader_result OUTPUT_VARIABLE _shader_output ERROR_VARIABLE _shader_error)
if(NOT _shader_result EQUAL 0)
    message(FATAL_ERROR "NR release shader install failed: ${_shader_output}${_shader_error}")
endif()
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

# The component manifest independently proves runtime payload completeness.
file(STRINGS "${BUILD_ROOT}/install_manifest_StreamlineRuntime.txt" _installed)
set(_expected "")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/StreamlineRuntimeFiles.cmake")
foreach(_relative_path IN LISTS STREAMLINE_OFFICIAL_RUNTIME_PATHS)
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

list(SORT _installed)
list(SORT _expected)
if(NOT _installed STREQUAL _expected)
    message(
        FATAL_ERROR
        "StreamlineRuntime installed unexpected or duplicate files: ${_installed}"
    )
endif()
set(_streamline_root "${_prefix}/Shaders/Upscaling/Streamline")
file(GLOB_RECURSE _actual RELATIVE "${_streamline_root}" "${_streamline_root}/*")
set(_relative_expected "")
foreach(_file IN LISTS _expected)
    file(RELATIVE_PATH _relative "${_streamline_root}" "${_file}")
    list(APPEND _relative_expected "${_relative}")
endforeach()
list(SORT _actual)
list(SORT _relative_expected)
if(NOT _actual STREQUAL _relative_expected)
    message(FATAL_ERROR "NR release contains unexpected physical files: ${_actual}")
endif()
message(
    STATUS
    "StreamlineRuntime installs the exact configured DLLs and five original notices"
)
