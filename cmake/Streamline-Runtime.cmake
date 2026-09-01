option(
    CSX_STAGE_LOCAL_DLSS_RUNTIME
    "Stage the hash-pinned internal DLSS 310.8 and Streamline 2.13 runtime"
    OFF
)

set(
    CSX_LOCAL_DLSS_RUNTIME_DIRECTORY
    ""
    CACHE PATH
    "Directory containing the user-supplied normal DLSS runtime set; required when local staging is enabled"
)
set(
    CSX_LOCAL_DLSSNR_RUNTIME_FILE
    ""
    CACHE FILEPATH
    "User-supplied patched Neural Rendering runtime; required when local staging is enabled"
)
set(
    STREAMLINE_RUNTIME_NOTICE_SOURCE
    "${CMAKE_SOURCE_DIR}/features/Upscaling/INTERNAL-NO-REDISTRIBUTION.txt"
)

set(
    STREAMLINE_NORMAL_DLSS_RUNTIME_FILENAMES
    nvngx_dlss.dll
    sl.common.dll
    sl.dlss.dll
    sl.interposer.dll
    sl.pcl.dll
    sl.reflex.dll
)
set(
    STREAMLINE_NEURAL_RUNTIME_FILENAMES
    nvngx_dlssnr.dll
)
set(
    STREAMLINE_RUNTIME_PACKAGED_FILENAMES
    ${STREAMLINE_NORMAL_DLSS_RUNTIME_FILENAMES}
    ${STREAMLINE_NEURAL_RUNTIME_FILENAMES}
)

set(
    STREAMLINE_LOCAL_SHA256_nvngx_dlss_dll
    "C85F971CE023C9F3492FC7455F0B01A24BA18EA39636407A846902C4360B0B7E"
)
set(
    STREAMLINE_LOCAL_SHA256_nvngx_dlssnr_dll
    "8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206"
)
set(
    STREAMLINE_LOCAL_SHA256_sl_common_dll
    "A4B2B5ACBE49FBC6D44DD432CAC19CD53218F698B2539DC7ED0FB268C72CFC8D"
)
set(
    STREAMLINE_LOCAL_SHA256_sl_dlss_dll
    "1EB5FB3D6F01D340FE086D981CC2DE4F18AA6D05EE276E5CF28ECD54818DCC8B"
)
set(
    STREAMLINE_LOCAL_SHA256_sl_interposer_dll
    "27B2190057994C0B287C2C5716953BF1586F6499AC12FBBB2092B9AAF8396570"
)
set(
    STREAMLINE_LOCAL_SHA256_sl_pcl_dll
    "12AA4E76C28A27C735E4ECB3072F44D09428ACB107B70AC38E4BD48DDB05F88D"
)
set(
    STREAMLINE_LOCAL_SHA256_sl_reflex_dll
    "ECF12973CDCEC2FFCED2EA77B1C7E45F4D387E7C864DDB5531B66A6F947EFFB3"
)

if(NOT CSX_STAGE_LOCAL_DLSS_RUNTIME)
    set(STREAMLINE_RUNTIME_FEATURE_ROOT "")
    set(STREAMLINE_RUNTIME_SHADER_ROOT "")
    set(STREAMLINE_RUNTIME_DIRECTORY "")
    set(STREAMLINE_RUNTIME_FILES "")
    set(STREAMLINE_RUNTIME_PACKAGED_FILENAMES "")
    set(STREAMLINE_RUNTIME_PACKAGE_SUFFIX "")
    set(STREAMLINE_RUNTIME_INSTALL_SCRIPT "")
    message(STATUS "Local DLSS runtime staging is disabled")
    return()
endif()

set(STREAMLINE_RUNTIME_PACKAGE_SUFFIX "-INTERNAL-NO-REDISTRIBUTION")

if(NOT IS_DIRECTORY "${CSX_LOCAL_DLSS_RUNTIME_DIRECTORY}")
    message(
        FATAL_ERROR
        "The internal DLSS runtime directory does not exist: ${CSX_LOCAL_DLSS_RUNTIME_DIRECTORY}"
    )
endif()

set(STREAMLINE_RUNTIME_WORK_ROOT "${CMAKE_CURRENT_BINARY_DIR}/streamline-runtime")
set(
    STREAMLINE_RUNTIME_FEATURE_ROOT
    "${STREAMLINE_RUNTIME_WORK_ROOT}/payload"
)
set(
    STREAMLINE_RUNTIME_SHADER_ROOT
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/Shaders"
)
set(
    STREAMLINE_RUNTIME_DIRECTORY
    "${STREAMLINE_RUNTIME_SHADER_ROOT}/Upscaling/Streamline"
)
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_DIRECTORY}")

function(csx_stage_pinned_local_runtime _filename)
    string(MAKE_C_IDENTIFIER "${_filename}" _hash_key)
    set(_expected_hash "${STREAMLINE_LOCAL_SHA256_${_hash_key}}")
    if(_expected_hash STREQUAL "")
        message(FATAL_ERROR "No SHA-256 pin is declared for ${_filename}")
    endif()

    if(_filename STREQUAL "nvngx_dlssnr.dll")
        set(_source "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}")
    else()
        set(_source "${CSX_LOCAL_DLSS_RUNTIME_DIRECTORY}/${_filename}")
    endif()
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "Required internal runtime is missing: ${_source}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")

    file(SHA256 "${_source}" _actual_hash)
    string(TOUPPER "${_actual_hash}" _actual_hash)
    if(NOT _actual_hash STREQUAL _expected_hash)
        message(
            FATAL_ERROR
            "Rejected ${_source}: expected SHA-256 ${_expected_hash}, got ${_actual_hash}"
        )
    endif()

    set(_destination "${STREAMLINE_RUNTIME_DIRECTORY}/${_filename}")
    file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    set(
        STREAMLINE_RUNTIME_FILES
        ${STREAMLINE_RUNTIME_FILES}
        "${_destination}"
        PARENT_SCOPE
    )
    set(
        STREAMLINE_RUNTIME_SOURCE_FILES
        ${STREAMLINE_RUNTIME_SOURCE_FILES}
        "${_source}"
        PARENT_SCOPE
    )
    set(
        STREAMLINE_RUNTIME_MANIFEST_ENTRIES
        ${STREAMLINE_RUNTIME_MANIFEST_ENTRIES}
        "${_filename}|${_source}|${_expected_hash}"
        PARENT_SCOPE
    )
endfunction()

set(STREAMLINE_RUNTIME_FILES "")
set(STREAMLINE_RUNTIME_SOURCE_FILES "")
set(STREAMLINE_RUNTIME_MANIFEST_ENTRIES "")
foreach(_filename IN LISTS STREAMLINE_RUNTIME_PACKAGED_FILENAMES)
    csx_stage_pinned_local_runtime("${_filename}")
endforeach()

set(
    STREAMLINE_RUNTIME_MANIFEST
    "${STREAMLINE_RUNTIME_WORK_ROOT}/pinned-runtime-manifest.txt"
)
string(
    REPLACE ";" "\n" _streamline_runtime_manifest_content
    "${STREAMLINE_RUNTIME_MANIFEST_ENTRIES}"
)
file(WRITE "${STREAMLINE_RUNTIME_MANIFEST}" "${_streamline_runtime_manifest_content}\n")
file(SHA256 "${STREAMLINE_RUNTIME_MANIFEST}" STREAMLINE_RUNTIME_MANIFEST_SHA256)
string(
    TOUPPER
    "${STREAMLINE_RUNTIME_MANIFEST_SHA256}"
    STREAMLINE_RUNTIME_MANIFEST_SHA256
)
if(NOT EXISTS "${STREAMLINE_RUNTIME_NOTICE_SOURCE}")
    message(FATAL_ERROR
        "The internal runtime notice is missing: ${STREAMLINE_RUNTIME_NOTICE_SOURCE}"
    )
endif()
file(SHA256 "${STREAMLINE_RUNTIME_NOTICE_SOURCE}" STREAMLINE_RUNTIME_NOTICE_SHA256)
string(
    TOUPPER
    "${STREAMLINE_RUNTIME_NOTICE_SHA256}"
    STREAMLINE_RUNTIME_NOTICE_SHA256
)
set(
    STREAMLINE_RUNTIME_VERIFY_SCRIPT
    "${STREAMLINE_RUNTIME_WORK_ROOT}/VerifyPinnedLocalRuntime.cmake"
)
set(
    STREAMLINE_RUNTIME_VERIFY_STAMP
    "${STREAMLINE_RUNTIME_WORK_ROOT}/pinned-runtime-verified.stamp"
)
file(
    GENERATE OUTPUT "${STREAMLINE_RUNTIME_VERIFY_SCRIPT}"
    CONTENT
        "set(STAGE_PINNED_RUNTIME_MANIFEST [==[${STREAMLINE_RUNTIME_MANIFEST}]==])\nset(STAGE_PINNED_RUNTIME_MANIFEST_SHA256 [==[${STREAMLINE_RUNTIME_MANIFEST_SHA256}]==])\nset(STAGE_PINNED_RUNTIME_DESTINATION [==[${STREAMLINE_RUNTIME_DIRECTORY}]==])\nset(STAGE_PINNED_RUNTIME_NOTICE_SOURCE [==[${STREAMLINE_RUNTIME_NOTICE_SOURCE}]==])\nset(STAGE_PINNED_RUNTIME_NOTICE_DESTINATION [==[${STREAMLINE_RUNTIME_FEATURE_ROOT}]==])\nset(STAGE_PINNED_RUNTIME_NOTICE_SHA256 [==[${STREAMLINE_RUNTIME_NOTICE_SHA256}]==])\nset(STAGE_PINNED_RUNTIME_STAMP [==[${STREAMLINE_RUNTIME_VERIFY_STAMP}]==])\ninclude([==[${CMAKE_CURRENT_LIST_DIR}/StagePinnedLocalRuntime.cmake]==])\n"
)
set(
    STREAMLINE_RUNTIME_INSTALL_SCRIPT
    "${STREAMLINE_RUNTIME_WORK_ROOT}/InstallPinnedLocalRuntime.cmake"
)
file(
    GENERATE OUTPUT "${STREAMLINE_RUNTIME_INSTALL_SCRIPT}"
    CONTENT
        "set(STAGE_PINNED_RUNTIME_MANIFEST [==[${STREAMLINE_RUNTIME_MANIFEST}]==])\nset(STAGE_PINNED_RUNTIME_MANIFEST_SHA256 [==[${STREAMLINE_RUNTIME_MANIFEST_SHA256}]==])\nset(STAGE_PINNED_RUNTIME_DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Shaders/Upscaling/Streamline\")\nset(STAGE_PINNED_RUNTIME_NOTICE_SOURCE [==[${STREAMLINE_RUNTIME_NOTICE_SOURCE}]==])\nset(STAGE_PINNED_RUNTIME_NOTICE_DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")\nset(STAGE_PINNED_RUNTIME_NOTICE_SHA256 [==[${STREAMLINE_RUNTIME_NOTICE_SHA256}]==])\ninclude([==[${CMAKE_CURRENT_LIST_DIR}/StagePinnedLocalRuntime.cmake]==])\n"
)
add_custom_target(
    VerifyLocalDLSSRuntime
    COMMAND "${CMAKE_COMMAND}" -P "${STREAMLINE_RUNTIME_VERIFY_SCRIPT}"
    BYPRODUCTS
        ${STREAMLINE_RUNTIME_FILES}
        "${STREAMLINE_RUNTIME_FEATURE_ROOT}/INTERNAL-NO-REDISTRIBUTION.txt"
        "${STREAMLINE_RUNTIME_VERIFY_STAMP}"
    DEPENDS
        ${STREAMLINE_RUNTIME_SOURCE_FILES}
        "${STREAMLINE_RUNTIME_MANIFEST}"
        "${STREAMLINE_RUNTIME_VERIFY_SCRIPT}"
        "${STREAMLINE_RUNTIME_INSTALL_SCRIPT}"
        "${STREAMLINE_RUNTIME_NOTICE_SOURCE}"
        "${CMAKE_CURRENT_LIST_DIR}/StagePinnedLocalRuntime.cmake"
    COMMENT "Verifying and staging the pinned local DLSS runtime"
    VERBATIM
)
if(TARGET ${PROJECT_NAME})
    add_dependencies(${PROJECT_NAME} VerifyLocalDLSSRuntime)
endif()

message(
    STATUS
    "Staged the hash-pinned internal DLSS 310.8 and Streamline 2.13 runtime"
)
