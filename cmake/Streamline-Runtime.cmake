set(STREAMLINE_RUNTIME_VERSION "2.14.1")
set(
    STREAMLINE_RUNTIME_ARCHIVE_SHA256
    "92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B"
)
set(
    STREAMLINE_RUNTIME_ARCHIVE_URL
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${STREAMLINE_RUNTIME_VERSION}/streamline-sdk-v${STREAMLINE_RUNTIME_VERSION}.zip"
)

include("${CMAKE_CURRENT_LIST_DIR}/CsxDownload.cmake")

set(STREAMLINE_RUNTIME_WORK_ROOT "${CMAKE_CURRENT_BINARY_DIR}/streamline-runtime")
set(
    STREAMLINE_RUNTIME_ARCHIVE
    "${STREAMLINE_RUNTIME_WORK_ROOT}/streamline-sdk-v${STREAMLINE_RUNTIME_VERSION}.zip"
)
set(STREAMLINE_RUNTIME_EXTRACT_ROOT "${STREAMLINE_RUNTIME_WORK_ROOT}/sdk")
set(
    STREAMLINE_RUNTIME_EXTRACT_STAMP
    "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/.archive-sha256"
)
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
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_WORK_ROOT}")
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_DIRECTORY}")

csx_download_verified_asset(
    "${STREAMLINE_RUNTIME_ARCHIVE_URL}"
    "${STREAMLINE_RUNTIME_ARCHIVE}"
    "${STREAMLINE_RUNTIME_ARCHIVE_SHA256}"
)

set(_streamline_extract_required ON)
if(EXISTS "${STREAMLINE_RUNTIME_EXTRACT_STAMP}")
    file(READ "${STREAMLINE_RUNTIME_EXTRACT_STAMP}" _streamline_extracted_hash)
    string(STRIP "${_streamline_extracted_hash}" _streamline_extracted_hash)
    if(_streamline_extracted_hash STREQUAL STREAMLINE_RUNTIME_ARCHIVE_SHA256)
        set(_streamline_extract_required OFF)
    endif()
endif()

if(_streamline_extract_required)
    file(REMOVE_RECURSE "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
    file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
    file(
        ARCHIVE_EXTRACT
        INPUT "${STREAMLINE_RUNTIME_ARCHIVE}"
        DESTINATION "${STREAMLINE_RUNTIME_EXTRACT_ROOT}"
    )
    file(
        WRITE "${STREAMLINE_RUNTIME_EXTRACT_STAMP}"
        "${STREAMLINE_RUNTIME_ARCHIVE_SHA256}\n"
    )
endif()

function(stage_streamline_runtime _relative_path)
    set(_source "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/${_relative_path}")
    if(NOT EXISTS "${_source}" OR IS_DIRECTORY "${_source}")
        message(
            FATAL_ERROR
            "Missing required ${_relative_path} in Streamline ${STREAMLINE_RUNTIME_VERSION}"
        )
    endif()

    get_filename_component(_filename "${_source}" NAME)
    set(_destination "${STREAMLINE_RUNTIME_DIRECTORY}/${_filename}")
    file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    set(STREAMLINE_RUNTIME_FILES
        ${STREAMLINE_RUNTIME_FILES}
        "${_destination}"
        PARENT_SCOPE
    )
endfunction()

set(STREAMLINE_RUNTIME_FILES "")
# Use production binaries and their original notices from the same pinned SDK.
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
        "3rd-party-licenses.md"
        "NVIDIA Nsight Graphics SDK License (Apache 2.0).txt"
)
    stage_streamline_runtime("${_relative_path}")
endforeach()

if(BUILD_CONTROLLER_TESTS)
    add_test(
        NAME StreamlineRuntimePackaging
        COMMAND
            "${CMAKE_COMMAND}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
            "-DSDK_ROOT=${STREAMLINE_RUNTIME_EXTRACT_ROOT}"
            "-DTEST_CONFIG=$<CONFIG>" -P
            "${PROJECT_SOURCE_DIR}/tests/streamline_runtime_packaging_test.cmake"
    )
    set_tests_properties(
        StreamlineRuntimePackaging
        PROPERTIES LABELS "PackagingTests" RUN_SERIAL TRUE
    )
endif()
