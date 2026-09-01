cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT DEFINED LOCAL_RUNTIME_DIR OR NOT IS_DIRECTORY "${LOCAL_RUNTIME_DIR}")
    message(FATAL_ERROR "LOCAL_RUNTIME_DIR must identify the supplied runtime")
endif()
if(NOT DEFINED LOCAL_DLSSNR_FILE OR NOT EXISTS "${LOCAL_DLSSNR_FILE}")
    message(FATAL_ERROR "LOCAL_DLSSNR_FILE must identify the patched NR runtime")
endif()
if(NOT DEFINED STAGED_RUNTIME_DIR OR NOT IS_DIRECTORY "${STAGED_RUNTIME_DIR}")
    message(FATAL_ERROR "STAGED_RUNTIME_DIR must identify the build payload")
endif()
foreach(_required_input IN ITEMS
    PINNED_RUNTIME_MANIFEST
    RUNTIME_STAGE_SCRIPT
    RUNTIME_INSTALL_SCRIPT
    RUNTIME_SCRUB_SCRIPT
    RUNTIME_NOTICE_SOURCE
    BUILD_ROOT
    BUILD_CONFIG
)
    if(NOT DEFINED ${_required_input} OR
       "${${_required_input}}" STREQUAL "")
        message(FATAL_ERROR "${_required_input} is required")
    endif()
endforeach()
foreach(_required_path IN ITEMS
    "${PINNED_RUNTIME_MANIFEST}"
    "${RUNTIME_STAGE_SCRIPT}"
    "${RUNTIME_INSTALL_SCRIPT}"
    "${RUNTIME_SCRUB_SCRIPT}"
    "${RUNTIME_NOTICE_SOURCE}"
)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required packaging input is missing: ${_required_path}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${BUILD_ROOT}")
    message(FATAL_ERROR "BUILD_ROOT must identify the configured build tree")
endif()

set(_runtime_policy_path "${PROJECT_ROOT}/cmake/Streamline-Runtime.cmake")
set(
    _runtime_stage_script_path
    "${RUNTIME_STAGE_SCRIPT}"
)
set(_cmake_lists_path "${PROJECT_ROOT}/CMakeLists.txt")
set(
    _experiment_doc_path
    "${PROJECT_ROOT}/docs/development/dlss-neural-rendering-experiments.md"
)
set(
    _neural_runtime_header_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.h"
)
set(
    _neural_runtime_source_path
    "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Runtime.cpp"
)
set(
    _internal_notice_path
    "${PROJECT_ROOT}/features/Upscaling/INTERNAL-NO-REDISTRIBUTION.txt"
)
file(READ "${_runtime_policy_path}" _runtime_policy)
file(READ "${_runtime_stage_script_path}" _runtime_stage_script)
file(READ "${_cmake_lists_path}" _cmake_lists)
file(READ "${PROJECT_ROOT}/.gitignore" _gitignore)
file(READ "${_experiment_doc_path}" _experiment_doc)
file(READ "${_neural_runtime_header_path}" _neural_runtime_header)
file(READ "${_neural_runtime_source_path}" _neural_runtime_source)
file(READ "${_internal_notice_path}" _internal_notice)

foreach(_forbidden_download_contract IN ITEMS
    "CSX_DOWNLOAD_STREAMLINE_RUNTIME"
    "CsxDownload.cmake"
    "https://github.com/NVIDIA-RTX/Streamline/releases"
)
    string(FIND
        "${_runtime_policy}"
        "${_forbidden_download_contract}"
        _forbidden_download_position
    )
    if(NOT _forbidden_download_position EQUAL -1)
        message(FATAL_ERROR
            "Local runtime policy still contains ${_forbidden_download_contract}"
        )
    endif()
endforeach()

foreach(_parameter_core_contract IN ITEMS
    "kParameterCoreBasenames"
    "L\"nvngx.dll\""
    "L\"_nvngx.dll\""
    "module basename is neither nvngx.dll nor _nvngx.dll"
    "a_requireTrustedDriverStore && !VerifyAuthenticodeOffline(a_path, a_error)"
    "!VerifyNvidiaParameterCoreVersionResource(a_path, a_error)"
    "_wcsicmp(companyName.c_str(), L\"NVIDIA Corporation\") == 0"
    "_wcsicmp(productName.c_str(), L\"NGX\") == 0"
    "_wcsicmp(originalFilename.c_str(), L\"nvngx.dll\") == 0"
    "RequireAbsentAdjacentParameterCore"
    "adjacent NGX parameter core is forbidden"
    "path_.parent_path() / parameterCoreBasename"
)
    string(FIND
        "${_neural_runtime_source}"
        "${_parameter_core_contract}"
        _parameter_core_contract_position
    )
    if(_parameter_core_contract_position EQUAL -1)
        message(FATAL_ERROR
            "NGX parameter-core trust contract is missing: ${_parameter_core_contract}"
        )
    endif()
endforeach()

string(FIND
    "${_neural_runtime_source}"
    "if (!RequireAbsentAdjacentParameterCore(adjacentCorePath"
    _adjacent_core_gate_position
)
string(FIND
    "${_neural_runtime_source}"
    "if (!SelectParameterCore("
    _parameter_core_selection_position
)
string(FIND
    "${_neural_runtime_source}"
    "initializeResult = initialize("
    _ngx_initialization_position
)
string(FIND
    "${_neural_runtime_source}"
    "coreModule_ = coreSelection.ReleaseModule()"
    _parameter_core_retention_position
)
if(_adjacent_core_gate_position EQUAL -1 OR
   _parameter_core_selection_position EQUAL -1 OR
   _ngx_initialization_position EQUAL -1 OR
   _parameter_core_retention_position EQUAL -1 OR
   NOT _adjacent_core_gate_position LESS _parameter_core_selection_position OR
   NOT _parameter_core_selection_position LESS _ngx_initialization_position OR
   NOT _ngx_initialization_position LESS _parameter_core_retention_position)
    message(FATAL_ERROR
        "NGX core trust must be retained across vendor initialization"
    )
endif()

foreach(_runtime_contract IN ITEMS
    "CSX_STAGE_LOCAL_DLSS_RUNTIME"
    "CSX_LOCAL_DLSS_RUNTIME_DIRECTORY"
    "CSX_LOCAL_DLSSNR_RUNTIME_FILE"
    "STREAMLINE_RUNTIME_PACKAGED_FILENAMES"
    "csx_stage_pinned_local_runtime"
    "file(SHA256"
    "CMAKE_CONFIGURE_DEPENDS"
    "STREAMLINE_RUNTIME_MANIFEST_SHA256"
    "STREAMLINE_RUNTIME_VERIFY_STAMP"
    "VerifyLocalDLSSRuntime"
    "INTERNAL-NO-REDISTRIBUTION"
)
    string(FIND "${_runtime_policy}" "${_runtime_contract}" _contract_position)
    if(_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Local runtime policy is missing: ${_runtime_contract}"
        )
    endif()
endforeach()

foreach(_stage_contract IN ITEMS
    "file(SHA256"
    "COPY_FILE"
    "STAGE_PINNED_RUNTIME_MANIFEST_SHA256"
    "_destination_hash"
    "file(TOUCH"
)
    string(FIND
        "${_runtime_stage_script}"
        "${_stage_contract}"
        _stage_contract_position
    )
    if(_stage_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Build-time runtime verifier is missing: ${_stage_contract}"
        )
    endif()
endforeach()

set(_runtime_manifest
    "nvngx_dlss.dll=C85F971CE023C9F3492FC7455F0B01A24BA18EA39636407A846902C4360B0B7E"
    "nvngx_dlssnr.dll=8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206"
    "sl.common.dll=A4B2B5ACBE49FBC6D44DD432CAC19CD53218F698B2539DC7ED0FB268C72CFC8D"
    "sl.dlss.dll=1EB5FB3D6F01D340FE086D981CC2DE4F18AA6D05EE276E5CF28ECD54818DCC8B"
    "sl.interposer.dll=27B2190057994C0B287C2C5716953BF1586F6499AC12FBBB2092B9AAF8396570"
    "sl.pcl.dll=12AA4E76C28A27C735E4ECB3072F44D09428ACB107B70AC38E4BD48DDB05F88D"
    "sl.reflex.dll=ECF12973CDCEC2FFCED2EA77B1C7E45F4D387E7C864DDB5531B66A6F947EFFB3"
)

foreach(_manifest_entry IN LISTS _runtime_manifest)
    string(REPLACE "=" ";" _manifest_fields "${_manifest_entry}")
    list(GET _manifest_fields 0 _runtime_name)
    list(GET _manifest_fields 1 _expected_hash)

    foreach(_contract_value IN ITEMS "${_runtime_name}" "${_expected_hash}")
        string(FIND "${_runtime_policy}" "${_contract_value}" _value_position)
        if(_value_position EQUAL -1)
            message(FATAL_ERROR
                "Runtime policy is missing pinned value ${_contract_value}"
            )
        endif()
    endforeach()

    set(_source_runtime_path "${LOCAL_RUNTIME_DIR}/${_runtime_name}")
    if(_runtime_name STREQUAL "nvngx_dlssnr.dll")
        set(_source_runtime_path "${LOCAL_DLSSNR_FILE}")
    endif()
    foreach(_runtime_path IN ITEMS
        "${_source_runtime_path}"
        "${STAGED_RUNTIME_DIR}/${_runtime_name}"
    )
        if(NOT EXISTS "${_runtime_path}")
            message(FATAL_ERROR "Pinned runtime is missing: ${_runtime_path}")
        endif()
        file(SHA256 "${_runtime_path}" _actual_hash)
        string(TOUPPER "${_actual_hash}" _actual_hash)
        if(NOT _actual_hash STREQUAL _expected_hash)
            message(FATAL_ERROR
                "Runtime hash mismatch for ${_runtime_path}: ${_actual_hash}"
            )
        endif()
    endforeach()
endforeach()

foreach(_unstaged_nr_hash IN ITEMS
    E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
    CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650
)
    string(FIND "${_runtime_policy}" "${_unstaged_nr_hash}" _unstaged_position)
    if(NOT _unstaged_position EQUAL -1)
        message(FATAL_ERROR
            "Automatic staging must not select NR identity ${_unstaged_nr_hash}"
        )
    endif()
endforeach()

foreach(_packaging_contract IN ITEMS
    "CSX_LOCAL_STREAMLINE_DLL_REGEX"
    "csx_install_content_without_local_streamline_dlls"
    "COMPONENT StreamlineRuntime"
    "if(STREAMLINE_RUNTIME_FILES)"
    "ScrubUnmanagedStreamlineRuntime.cmake"
    "FILTER _package_shaders"
    "FILTER _feat_shaders"
    "FILTER CORE_SOURCES"
    "FILTER FEATURE_SOURCES"
    "SCRIPT \"\${STREAMLINE_RUNTIME_INSTALL_SCRIPT}\""
    "StageAioPinnedLocalDLSSRuntime"
    "ScrubAioLocalDLSSRuntime"
    "CSX_INTERNAL_RUNTIME_NOTICE_NAME"
)
    string(FIND "${_cmake_lists}" "${_packaging_contract}" _contract_position)
    if(_contract_position EQUAL -1)
        message(FATAL_ERROR
            "Streamline packaging contract is missing: ${_packaging_contract}"
        )
    endif()
endforeach()

foreach(_notice_contract IN ITEMS
    "DO NOT REDISTRIBUTE OR PUBLISH THIS BUILD"
    "8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206"
    "Authenticode status HashMismatch"
    "CSX log level does not affect this allowlist"
    "malware-free"
)
    string(FIND "${_internal_notice}" "${_notice_contract}" _notice_position)
    if(_notice_position EQUAL -1)
        message(FATAL_ERROR "Internal package notice is missing: ${_notice_contract}")
    endif()
endforeach()

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
        "features/Upscaling/Shaders/Upscaling/Streamline/*.dll"
        "features/Upscaling/Shaders/Upscaling/Streamline/*.DLL"
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

set(_publication_text "${_runtime_policy}\n${_experiment_doc}")
string(
    REGEX MATCH
    "(^|[^A-Za-z])([A-Za-z]:[/\\\\])"
    _absolute_windows_path
    "${_publication_text}"
)
if(NOT _absolute_windows_path STREQUAL "")
    message(FATAL_ERROR "Publication text contains an absolute Windows path")
endif()

set(_accepted_nr_hashes
    E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
    8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206
    CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650
)
foreach(_accepted_hash IN LISTS _accepted_nr_hashes)
    foreach(_contract_text IN ITEMS "${_experiment_doc}" "${_neural_runtime_header}")
        string(FIND "${_contract_text}" "${_accepted_hash}" _hash_position)
        if(_hash_position EQUAL -1)
            message(FATAL_ERROR
                "Neural Rendering contract is missing hash ${_accepted_hash}"
            )
        endif()
    endforeach()
endforeach()

foreach(_obsolete_gate IN ITEMS
    "allowPatchedRuntime"
    "developer-mode opt-in"
)
    string(FIND
        "${_neural_runtime_header}\n${_neural_runtime_source}"
        "${_obsolete_gate}"
        _obsolete_gate_position
    )
    if(NOT _obsolete_gate_position EQUAL -1)
        message(FATAL_ERROR
            "Neural Rendering still contains the patched-runtime gate: ${_obsolete_gate}"
        )
    endif()
endforeach()

foreach(_documentation_contract IN ITEMS
    "All three identities are accepted"
    "at every CSX log level"
    "does not use or require `sl.dlss_nr.dll`"
    "DriverStore alias `_nvngx.dll`"
    "Streamline 2.13"
    "caller-path substitution"
    "WHCP signature"
    "signer's publisher"
    "not a redistributable or release-ready integration"
    "Local runtime staging is disabled by default"
    "CSX_STAGE_LOCAL_DLSS_RUNTIME=ON"
    "admission relies on its exact pinned SHA-256"
    "Do not publish or redistribute"
)
    string(FIND
        "${_experiment_doc}"
        "${_documentation_contract}"
        _documentation_position
    )
    if(_documentation_position EQUAL -1)
        message(FATAL_ERROR
            "Streamline documentation contract is missing: ${_documentation_contract}"
        )
    endif()
endforeach()

set(_stage_probe_root "${CMAKE_CURRENT_BINARY_DIR}/pinned-runtime-stage-probe")
set(_stage_probe_source "${_stage_probe_root}/source/probe.dll")
set(_stage_probe_destination "${_stage_probe_root}/destination")
set(_stage_probe_manifest "${_stage_probe_root}/manifest.txt")
set(_stage_probe_stamp "${_stage_probe_root}/verified.stamp")
file(REMOVE_RECURSE "${_stage_probe_root}")
file(MAKE_DIRECTORY "${_stage_probe_root}/source" "${_stage_probe_destination}")
file(WRITE "${_stage_probe_source}" "pinned runtime probe\n")
file(SHA256 "${_stage_probe_source}" _stage_probe_hash)
file(WRITE
    "${_stage_probe_manifest}"
    "probe.dll|${_stage_probe_source}|${_stage_probe_hash}\n"
)
file(SHA256
    "${_stage_probe_manifest}"
    STAGE_PINNED_RUNTIME_MANIFEST_SHA256
)
file(WRITE "${_stage_probe_destination}/probe.dll" "tampered\n")
set(STAGE_PINNED_RUNTIME_MANIFEST "${_stage_probe_manifest}")
set(STAGE_PINNED_RUNTIME_DESTINATION "${_stage_probe_destination}")
set(STAGE_PINNED_RUNTIME_STAMP "${_stage_probe_stamp}")
include("${_runtime_stage_script_path}")
file(SHA256 "${_stage_probe_destination}/probe.dll" _stage_probe_output_hash)
if(NOT _stage_probe_output_hash STREQUAL _stage_probe_hash)
    message(FATAL_ERROR "Build-time verifier did not repair staged runtime drift")
endif()
if(NOT EXISTS "${_stage_probe_stamp}")
    message(FATAL_ERROR "Build-time verifier did not publish its completion stamp")
endif()

set(_stage_probe_tamper_script "${_stage_probe_root}/reject-tampered-manifest.cmake")
file(APPEND "${_stage_probe_manifest}" "tampered manifest\n")
file(WRITE
    "${_stage_probe_tamper_script}"
    "set(STAGE_PINNED_RUNTIME_MANIFEST [==[${_stage_probe_manifest}]==])\n"
    "set(STAGE_PINNED_RUNTIME_MANIFEST_SHA256 [==[${STAGE_PINNED_RUNTIME_MANIFEST_SHA256}]==])\n"
    "set(STAGE_PINNED_RUNTIME_DESTINATION [==[${_stage_probe_destination}]==])\n"
    "include([==[${_runtime_stage_script_path}]==])\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -P "${_stage_probe_tamper_script}"
    RESULT_VARIABLE _stage_probe_tamper_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(_stage_probe_tamper_result EQUAL 0)
    message(FATAL_ERROR "Build-time verifier accepted a tampered manifest")
endif()
file(REMOVE_RECURSE "${_stage_probe_root}")

set(_consumer_probe_root "${CMAKE_CURRENT_BINARY_DIR}/pinned-runtime-consumer-probe")
set(_consumer_runtime_dir
    "${_consumer_probe_root}/Shaders/Upscaling/Streamline"
)
file(REMOVE_RECURSE "${_consumer_probe_root}")
file(MAKE_DIRECTORY "${_consumer_runtime_dir}/nested")
file(WRITE "${_consumer_runtime_dir}/nvngx_dlss.dll" "bogus recognized name\n")
file(WRITE "${_consumer_runtime_dir}/sl.dlss_nr.dll" "unmanaged plugin\n")
file(WRITE "${_consumer_runtime_dir}/nested/sl.common.dll" "nested plugin\n")
set(STAGE_PINNED_RUNTIME_MANIFEST "${PINNED_RUNTIME_MANIFEST}")
file(SHA256
    "${PINNED_RUNTIME_MANIFEST}"
    STAGE_PINNED_RUNTIME_MANIFEST_SHA256
)
set(STAGE_PINNED_RUNTIME_DESTINATION "${_consumer_runtime_dir}")
set(STAGE_PINNED_RUNTIME_NOTICE_SOURCE "${RUNTIME_NOTICE_SOURCE}")
set(STAGE_PINNED_RUNTIME_NOTICE_DESTINATION "${_consumer_probe_root}")
file(SHA256 "${RUNTIME_NOTICE_SOURCE}" STAGE_PINNED_RUNTIME_NOTICE_SHA256)
unset(STAGE_PINNED_RUNTIME_STAMP)
include("${_runtime_stage_script_path}")

file(GLOB_RECURSE _consumer_runtime_dlls
    LIST_DIRECTORIES FALSE
    "${_consumer_runtime_dir}/*.[dD][lL][lL]"
)
list(LENGTH _consumer_runtime_dlls _consumer_runtime_count)
if(NOT _consumer_runtime_count EQUAL 7)
    message(FATAL_ERROR
        "Final consumer must contain exactly seven DLLs, found ${_consumer_runtime_count}"
    )
endif()
foreach(_manifest_entry IN LISTS _runtime_manifest)
    string(REPLACE "=" ";" _manifest_fields "${_manifest_entry}")
    list(GET _manifest_fields 0 _runtime_name)
    list(GET _manifest_fields 1 _expected_hash)
    file(SHA256 "${_consumer_runtime_dir}/${_runtime_name}" _actual_hash)
    string(TOUPPER "${_actual_hash}" _actual_hash)
    if(NOT _actual_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Final consumer hash mismatch for ${_runtime_name}: ${_actual_hash}"
        )
    endif()
endforeach()
if(EXISTS "${_consumer_runtime_dir}/sl.dlss_nr.dll" OR
   EXISTS "${_consumer_runtime_dir}/nested/sl.common.dll")
    message(FATAL_ERROR "Final consumer retained unmanaged runtime DLLs")
endif()
file(SHA256 "${RUNTIME_NOTICE_SOURCE}" _notice_source_hash)
file(SHA256
    "${_consumer_probe_root}/INTERNAL-NO-REDISTRIBUTION.txt"
    _notice_consumer_hash
)
if(NOT _notice_source_hash STREQUAL _notice_consumer_hash)
    message(FATAL_ERROR "Final consumer notice does not match its source")
endif()

set(_scrub_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/raw-runtime-scrub-probe")
file(REMOVE_RECURSE "${_scrub_probe_dir}")
file(MAKE_DIRECTORY "${_scrub_probe_dir}/nested")
file(WRITE "${_scrub_probe_dir}/nvngx_dlss.dll" "recognized but unverified\n")
file(WRITE "${_scrub_probe_dir}/nested/sl.common.dll" "nested unverified\n")
set(STREAMLINE_DIRECTORY "${_scrub_probe_dir}")
include("${RUNTIME_SCRUB_SCRIPT}")
file(GLOB_RECURSE _scrubbed_dlls
    LIST_DIRECTORIES FALSE
    "${_scrub_probe_dir}/*.[dD][lL][lL]"
)
if(_scrubbed_dlls)
    message(FATAL_ERROR "Raw package scrub retained runtime DLLs: ${_scrubbed_dlls}")
endif()

set(_install_probe_root "${CMAKE_CURRENT_BINARY_DIR}/pinned-runtime-install-probe")
set(_install_runtime_dir
    "${_install_probe_root}/Shaders/Upscaling/Streamline"
)
file(REMOVE_RECURSE "${_install_probe_root}")
file(MAKE_DIRECTORY "${_install_runtime_dir}")
file(WRITE "${_install_runtime_dir}/nvngx_dlss.dll" "tampered install target\n")
file(WRITE "${_install_runtime_dir}/sl.dlss_nr.dll" "unmanaged install target\n")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --install "${BUILD_ROOT}" --prefix
        "${_install_probe_root}" --component StreamlineRuntime --config
        "${BUILD_CONFIG}"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error
)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR
        "Pinned runtime component install failed (${_install_result}):\n${_install_output}\n${_install_error}"
    )
endif()
file(GLOB_RECURSE _installed_runtime_dlls
    LIST_DIRECTORIES FALSE
    "${_install_runtime_dir}/*.[dD][lL][lL]"
)
list(LENGTH _installed_runtime_dlls _installed_runtime_count)
if(NOT _installed_runtime_count EQUAL 7)
    message(FATAL_ERROR
        "Installed runtime must contain exactly seven DLLs, found ${_installed_runtime_count}"
    )
endif()
foreach(_manifest_entry IN LISTS _runtime_manifest)
    string(REPLACE "=" ";" _manifest_fields "${_manifest_entry}")
    list(GET _manifest_fields 0 _runtime_name)
    list(GET _manifest_fields 1 _expected_hash)
    file(SHA256 "${_install_runtime_dir}/${_runtime_name}" _actual_hash)
    string(TOUPPER "${_actual_hash}" _actual_hash)
    if(NOT _actual_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Installed runtime hash mismatch for ${_runtime_name}: ${_actual_hash}"
        )
    endif()
endforeach()
if(EXISTS "${_install_runtime_dir}/sl.dlss_nr.dll")
    message(FATAL_ERROR "Component install retained an unmanaged runtime DLL")
endif()
file(SHA256
    "${_install_probe_root}/INTERNAL-NO-REDISTRIBUTION.txt"
    _notice_install_hash
)
if(NOT _notice_source_hash STREQUAL _notice_install_hash)
    message(FATAL_ERROR "Installed runtime notice does not match its source")
endif()

file(REMOVE_RECURSE
    "${_consumer_probe_root}"
    "${_scrub_probe_dir}"
    "${_install_probe_root}"
)

message(STATUS "Pinned local Streamline runtime packaging contract is coherent")
