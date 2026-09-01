cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_runtime_policy_path "${PROJECT_ROOT}/cmake/Streamline-Runtime.cmake")
set(
    _runtime_stage_script_path
    "${PROJECT_ROOT}/cmake/StagePinnedLocalRuntime.cmake"
)
set(
    _official_runtime_path
    "${PROJECT_ROOT}/cmake/Streamline-Official-Runtime.cmake"
)
set(_download_helper_path "${PROJECT_ROOT}/cmake/CsxDownload.cmake")
set(_cmake_lists_path "${PROJECT_ROOT}/CMakeLists.txt")
set(
    _runtime_readme_path
    "${PROJECT_ROOT}/features/Upscaling/Shaders/Upscaling/Streamline/README.md"
)
set(
    _publication_paths
    "${PROJECT_ROOT}/.gitignore"
    "${_cmake_lists_path}"
    "${_download_helper_path}"
    "${_official_runtime_path}"
    "${_runtime_policy_path}"
    "${_runtime_stage_script_path}"
    "${PROJECT_ROOT}/docs/development/dlss-neural-rendering-experiments.md"
    "${_runtime_readme_path}"
    "${PROJECT_ROOT}/include/FidelityFX/LICENSE.md"
    "${PROJECT_ROOT}/race.md"
    "${PROJECT_ROOT}/tests/streamline_local_runtime_packaging_contract_test.cmake"
    "${CMAKE_CURRENT_LIST_FILE}"
)
foreach(_required_path IN LISTS _publication_paths)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required runtime contract is missing: ${_required_path}")
    endif()
endforeach()

file(READ "${_runtime_policy_path}" _runtime_policy)
file(READ "${_official_runtime_path}" _official_runtime)
file(READ "${_download_helper_path}" _download_helper)
file(READ "${_cmake_lists_path}" _cmake_lists)
file(READ "${_runtime_readme_path}" _runtime_readme)
file(READ "${PROJECT_ROOT}/.gitignore" _gitignore)

foreach(_default_off_option IN ITEMS
    CSX_STAGE_LOCAL_DLSS_RUNTIME
    CSX_FETCH_OFFICIAL_STREAMLINE_RUNTIME
)
    string(
        REGEX MATCH
        "option\\([ \t\r\n]*${_default_off_option}[^\\)]*[ \t\r\n]OFF[ \t\r\n]*\\)"
        _default_off_match
        "${_runtime_policy}"
    )
    if(_default_off_match STREQUAL "")
        message(FATAL_ERROR "${_default_off_option} must default to OFF")
    endif()
endforeach()

foreach(_local_cache_variable IN ITEMS
    CSX_LOCAL_DLSS_RUNTIME_DIRECTORY
    CSX_LOCAL_DLSSNR_RUNTIME_FILE
)
    string(
        REGEX MATCH
        "set\\([ \t\r\n]*${_local_cache_variable}[ \t\r\n]*\"\"[ \t\r\n]*CACHE"
        _empty_local_cache_match
        "${_runtime_policy}"
    )
    if(_empty_local_cache_match STREQUAL "")
        message(FATAL_ERROR
            "${_local_cache_variable} must be an empty user-supplied cache value"
        )
    endif()
endforeach()

foreach(_runtime_policy_contract IN ITEMS
    "CSX_STAGE_LOCAL_DLSS_RUNTIME AND CSX_FETCH_OFFICIAL_STREAMLINE_RUNTIME"
    "are mutually exclusive"
    "NOT CSX_STAGE_LOCAL_DLSS_RUNTIME AND"
    "NOT CSX_FETCH_OFFICIAL_STREAMLINE_RUNTIME"
    "file(REMOVE_RECURSE \"\${CMAKE_CURRENT_BINARY_DIR}/streamline-runtime\")"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/Streamline-Official-Runtime.cmake\")"
    "set(STREAMLINE_RUNTIME_FILES \"\")"
    "Streamline runtime staging is disabled"
    "VerifyStreamlineRuntime"
)
    string(FIND
        "${_runtime_policy}"
        "${_runtime_policy_contract}"
        _runtime_policy_contract_position
    )
    if(_runtime_policy_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Runtime source policy is missing: ${_runtime_policy_contract}"
        )
    endif()
endforeach()

foreach(_official_contract IN ITEMS
    "2.12.0"
    "F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48"
    "BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E"
    "C57930EF5A8A3FE9BE85EFDF71A61D8107C1148E8A6AED456464547128F7F4AE"
    "A997022D2B93601E0EEFC3DDB3067C36DF386DD3163AE71E11095191FB14F8E4"
    "2A79DB6857AE8C75BBD871A9489C48BC6A39F7FCC88B9B02AFD53D0376CBEC66"
    "699AB461E64E95189A7FE6A21C79AD237CF56B60EA748CB6C840CD5431BA91D1"
    "7E6E4CCC4B561BD449FB0DA90709D9B96B08C3F6F4697362CAAA359E72A58A67"
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/CsxDownload.cmake\")"
    "_candidate_directory_name STREQUAL \"x64\""
    "_candidate_parent_name STREQUAL \"bin\""
    "_production_match_count EQUAL 1"
    "file(SHA256 \"\${_source}\" _source_hash)"
    "if(NOT _source_hash STREQUAL _expected_hash)"
    "file(COPY_FILE \"\${_source}\" \"\${_destination}\" ONLY_IF_DIFFERENT)"
)
    string(FIND
        "${_official_runtime}"
        "${_official_contract}"
        _official_contract_position
    )
    if(_official_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Official runtime acquisition is missing: ${_official_contract}"
        )
    endif()
endforeach()

foreach(_fixed_source_variable IN ITEMS
    CSX_OFFICIAL_STREAMLINE_VERSION
    CSX_OFFICIAL_STREAMLINE_ARCHIVE_SHA256
)
    string(
        REGEX MATCH
        "set\\([ \t\r\n]*${_fixed_source_variable}[^\\)]*CACHE"
        _cache_override_match
        "${_official_runtime}"
    )
    if(NOT _cache_override_match STREQUAL "")
        message(FATAL_ERROR
            "${_fixed_source_variable} must not be cache-overridable"
        )
    endif()
endforeach()

string(FIND
    "${_official_runtime}"
    "file(REMOVE_RECURSE \"\${_streamline_official_extract_root}\")"
    _extract_reset_position
)
string(FIND "${_official_runtime}" "ARCHIVE_EXTRACT" _extract_position)
string(FIND "${_official_runtime}" "GLOB_RECURSE" _enumerate_position)
string(FIND
    "${_official_runtime}"
    "file(SHA256 \"\${_source}\" _source_hash)"
    _source_hash_position
)
string(FIND
    "${_official_runtime}"
    "file(COPY_FILE \"\${_source}\" \"\${_destination}\" ONLY_IF_DIFFERENT)"
    _copy_position
)
if(_extract_reset_position EQUAL -1 OR _extract_position EQUAL -1 OR
   _enumerate_position EQUAL -1 OR _source_hash_position EQUAL -1 OR
   _copy_position EQUAL -1 OR
   NOT _extract_reset_position LESS _extract_position OR
   NOT _extract_position LESS _enumerate_position OR
   NOT _enumerate_position LESS _source_hash_position OR
   NOT _source_hash_position LESS _copy_position)
    message(FATAL_ERROR
        "Official runtime must re-extract and verify fixed DLL pins before copying"
    )
endif()
string(FIND "${_official_runtime}" ".archive-sha256" _extract_stamp_position)
if(NOT _extract_stamp_position EQUAL -1)
    message(FATAL_ERROR "Official runtime must not trust an extraction stamp")
endif()
string(FIND "${_official_runtime}" "nvngx_dlssnr.dll" _official_nr_position)
if(NOT _official_nr_position EQUAL -1)
    message(FATAL_ERROR "Official normal-DLSS mode must not stage Feature 18")
endif()

foreach(_download_contract IN ITEMS
    "EXPECTED_HASH \"SHA256=\${EXPECTED_SHA256}\""
    "TLS_VERIFY ON"
    "TIMEOUT 600"
    "INACTIVITY_TIMEOUT 60"
    "file(SHA256 \"\${DESTINATION}\" _existing_sha256)"
    "file(REMOVE \"\${DESTINATION}\")"
)
    string(FIND
        "${_download_helper}"
        "${_download_contract}"
        _download_contract_position
    )
    if(_download_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Verified download contract is missing: ${_download_contract}"
        )
    endif()
endforeach()

foreach(_packaging_contract IN ITEMS
    "CSX_LOCAL_STREAMLINE_DLL_REGEX"
    "REGEX \"\${CSX_LOCAL_STREAMLINE_DLL_REGEX}\" EXCLUDE"
    "StageAioPinnedStreamlineRuntime"
    "ScrubAioStreamlineRuntime"
    "STREAMLINE_NOTICE_PATH"
    "ScrubUnmanagedStreamlineRuntime.cmake"
)
    string(FIND
        "${_cmake_lists}"
        "${_packaging_contract}"
        _packaging_contract_position
    )
    if(_packaging_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Runtime packaging contract is missing: ${_packaging_contract}"
        )
    endif()
endforeach()

foreach(_documentation_contract IN ITEMS
    "configuration and license notices only"
    "disabled by default"
    "builds and packages without NVIDIA"
    "CSX_STAGE_LOCAL_DLSS_RUNTIME=ON"
    "CSX_FETCH_OFFICIAL_STREAMLINE_RUNTIME=ON"
    "six normal-DLSS runtime files"
    "final `Shaders/Upscaling/Streamline` output"
    "mutually exclusive"
    "neither happens by"
)
    string(FIND
        "${_runtime_readme}"
        "${_documentation_contract}"
        _documentation_contract_position
    )
    if(_documentation_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Runtime placement documentation is missing: ${_documentation_contract}"
        )
    endif()
endforeach()

set(_publication_text "")
foreach(_publication_path IN LISTS _publication_paths)
    file(READ "${_publication_path}" _publication_file_text)
    string(APPEND _publication_text "\n${_publication_file_text}")
endforeach()
string(
    REGEX MATCH
    "(^|[^A-Za-z])([A-Za-z]:[/\\\\])"
    _absolute_windows_path
    "${_publication_text}"
)
if(NOT _absolute_windows_path STREQUAL "")
    message(FATAL_ERROR "Runtime publication text contains an absolute Windows path")
endif()

string(REPLACE "\r\n" "\n" _gitignore_normalized "${_gitignore}")
string(FIND
    "${_gitignore_normalized}"
    "\n/features/Upscaling/Shaders/Upscaling/Streamline/**/*.[dD][lL][lL]\n"
    _ignore_position
)
if(_ignore_position EQUAL -1)
    message(FATAL_ERROR "Source-tree NVIDIA runtime DLLs are not ignored")
endif()

execute_process(
    COMMAND
        git -C "${PROJECT_ROOT}" ls-files --
        ":(glob,icase)features/Upscaling/Shaders/Upscaling/Streamline/*.dll"
        ":(glob,icase)features/Upscaling/Shaders/Upscaling/Streamline/**/*.dll"
    RESULT_VARIABLE _tracked_dll_result
    OUTPUT_VARIABLE _tracked_dlls
    ERROR_VARIABLE _tracked_dll_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _tracked_dll_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect tracked NVIDIA DLLs: ${_tracked_dll_error}")
endif()
if(NOT _tracked_dlls STREQUAL "")
    message(FATAL_ERROR "Git tree contains NVIDIA runtime DLLs: ${_tracked_dlls}")
endif()

message(STATUS "Streamline runtime source contract is coherent")
