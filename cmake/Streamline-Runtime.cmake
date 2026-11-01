set(STREAMLINE_RUNTIME_VERSION "2.14.16")
set(STREAMLINE_RUNTIME_ARCHIVE_SHA256
    "32041E51DB0CAB709A271B72A352281B690C2ABBD669A827809401A5DD23956B"
)
set(STREAMLINE_RUNTIME_ARCHIVE_URL
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${STREAMLINE_RUNTIME_VERSION}/streamline-sdk-v${STREAMLINE_RUNTIME_VERSION}.zip"
)

include("${CMAKE_CURRENT_LIST_DIR}/RuntimePayload.cmake")

set(STREAMLINE_RUNTIME_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/streamline-runtime"
)
set(STREAMLINE_RUNTIME_ARCHIVE
    "${STREAMLINE_RUNTIME_ROOT}/streamline-sdk-v${STREAMLINE_RUNTIME_VERSION}.zip"
)
set(STREAMLINE_RUNTIME_EXTRACT_ROOT
    "${STREAMLINE_RUNTIME_ROOT}/sdk"
)
set(STREAMLINE_RUNTIME_FEATURE_ROOT
    "${STREAMLINE_RUNTIME_ROOT}/payload"
)
set(STREAMLINE_RUNTIME_SHADER_ROOT
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/Shaders"
)
set(STREAMLINE_RUNTIME_RELATIVE_DIRECTORY
    "Shaders/Upscaling/Streamline"
)
set(STREAMLINE_RUNTIME_DIRECTORY
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/${STREAMLINE_RUNTIME_RELATIVE_DIRECTORY}"
)
set(STREAMLINE_DLSSG_RUNTIME_RELATIVE_DIRECTORY
    "Shaders/Upscaling/StreamlineDX12"
)
set(STREAMLINE_DLSSG_RUNTIME_DIRECTORY
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/${STREAMLINE_DLSSG_RUNTIME_RELATIVE_DIRECTORY}"
)

set(STREAMLINE_RUNTIME_EXTRACT_STAMP
    "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/.archive-sha256")
set(STREAMLINE_RUNTIME_SOURCE_HASHES
    "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/.runtime-payload-sha256.json")
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_ROOT}")
csx_prepare_runtime_asset(
    "${STREAMLINE_RUNTIME_ARCHIVE_URL}"
    "${STREAMLINE_RUNTIME_ARCHIVE}"
    "${STREAMLINE_RUNTIME_ARCHIVE_SHA256}" _streamline_archive_available
)

set(_streamline_extract_required ON)
if(EXISTS "${STREAMLINE_RUNTIME_EXTRACT_STAMP}")
    file(READ "${STREAMLINE_RUNTIME_EXTRACT_STAMP}" _streamline_extracted_hash)
    string(STRIP "${_streamline_extracted_hash}" _streamline_extracted_hash)
    if(_streamline_extracted_hash STREQUAL STREAMLINE_RUNTIME_ARCHIVE_SHA256)
        set(_streamline_extract_required OFF)
    endif()
endif()

# Normal configurations always re-extract the verified archive. Skip mode never
# downloads, extracts, or discards an existing cache.
if(NOT SKIP_RUNTIME_DOWNLOADS)
    file(REMOVE_RECURSE "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
    file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${STREAMLINE_RUNTIME_ARCHIVE}"
        DESTINATION "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
    file(WRITE "${STREAMLINE_RUNTIME_EXTRACT_STAMP}"
        "${STREAMLINE_RUNTIME_ARCHIVE_SHA256}\n")
    set(_streamline_extract_required OFF)
endif()

# Record hashes only from a freshly verified extraction. A matching archive
# stamp alone must not admit a DLL or notice edited in the SDK cache afterwards.
set(_streamline_verified_hashes "{}")
if(SKIP_RUNTIME_DOWNLOADS AND EXISTS "${STREAMLINE_RUNTIME_SOURCE_HASHES}")
    file(READ "${STREAMLINE_RUNTIME_SOURCE_HASHES}" _candidate_hashes)
    string(JSON _hashes_type ERROR_VARIABLE _hashes_error TYPE "${_candidate_hashes}")
    if(NOT _hashes_error AND _hashes_type STREQUAL "OBJECT")
        set(_streamline_verified_hashes "${_candidate_hashes}")
    endif()
endif()

function(verify_streamline_source _source _available)
    set(_verified FALSE)
    if(EXISTS "${_source}" AND NOT IS_DIRECTORY "${_source}")
        file(RELATIVE_PATH _relative "${STREAMLINE_RUNTIME_EXTRACT_ROOT}" "${_source}")
        file(SHA256 "${_source}" _actual_hash)
        if(NOT SKIP_RUNTIME_DOWNLOADS)
            string(JSON _streamline_verified_hashes SET "${_streamline_verified_hashes}"
                "${_relative}" "\"${_actual_hash}\"")
            set(_streamline_verified_hashes "${_streamline_verified_hashes}" PARENT_SCOPE)
            set(_verified TRUE)
        elseif(_streamline_archive_available AND NOT _streamline_extract_required)
            string(JSON _expected_hash ERROR_VARIABLE _hash_error
                GET "${_streamline_verified_hashes}" "${_relative}")
            if(NOT _hash_error AND _actual_hash STREQUAL _expected_hash)
                set(_verified TRUE)
            endif()
        endif()
    endif()
    set(${_available} ${_verified} PARENT_SCOPE)
endfunction()

file(
    GLOB_RECURSE _streamline_archive_files
    LIST_DIRECTORIES FALSE
    "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/*"
)

function(stage_streamline_runtime _filename)
    set(_destination_directory "${STREAMLINE_RUNTIME_DIRECTORY}")
    if(ARGC GREATER 1)
        set(_destination_directory "${ARGV1}")
    endif()
    set(_production_matches "")
    foreach(_candidate IN LISTS _streamline_archive_files)
        get_filename_component(_candidate_name "${_candidate}" NAME)
        if(NOT _candidate_name STREQUAL _filename)
            continue()
        endif()

        get_filename_component(_candidate_directory "${_candidate}" DIRECTORY)
        get_filename_component(
            _candidate_directory_name
            "${_candidate_directory}"
            NAME
        )
        get_filename_component(
            _candidate_parent
            "${_candidate_directory}"
            DIRECTORY
        )
        get_filename_component(
            _candidate_parent_name
            "${_candidate_parent}"
            NAME
        )
        if(
            _candidate_directory_name STREQUAL "x64"
            AND _candidate_parent_name STREQUAL "bin"
        )
            list(APPEND _production_matches "${_candidate}")
        endif()
    endforeach()

    set(_destination "${_destination_directory}/${_filename}")
    set(STREAMLINE_RUNTIME_PAYLOAD_FILES
        ${STREAMLINE_RUNTIME_PAYLOAD_FILES} "${_destination}" PARENT_SCOPE)
    list(LENGTH _production_matches _production_match_count)
    if(SKIP_RUNTIME_DOWNLOADS AND NOT _production_match_count EQUAL 1)
        set(STREAMLINE_RUNTIME_PAYLOAD_MISSING
            ${STREAMLINE_RUNTIME_PAYLOAD_MISSING} "${_destination}" PARENT_SCOPE)
        return()
    endif()
    if(NOT _production_match_count EQUAL 1)
        message(
            FATAL_ERROR
            "Expected one production ${_filename} in Streamline ${STREAMLINE_RUNTIME_VERSION}, found ${_production_match_count}"
        )
    endif()

    list(GET _production_matches 0 _source)
    verify_streamline_source("${_source}" _available)
    set(_streamline_verified_hashes "${_streamline_verified_hashes}" PARENT_SCOPE)
    if(NOT _available)
        set(STREAMLINE_RUNTIME_PAYLOAD_MISSING
            ${STREAMLINE_RUNTIME_PAYLOAD_MISSING} "${_destination}" PARENT_SCOPE)
        return()
    endif()
    file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    set(STREAMLINE_RUNTIME_FILES
        ${STREAMLINE_RUNTIME_FILES}
        "${_destination}"
        PARENT_SCOPE
    )
    if(
        "${_destination_directory}"
        STREQUAL "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
    )
        set(STREAMLINE_DLSSG_RUNTIME_FILES
            ${STREAMLINE_DLSSG_RUNTIME_FILES}
            "${_destination}"
            PARENT_SCOPE
        )
    else()
        set(STREAMLINE_DX11_RUNTIME_FILES
            ${STREAMLINE_DX11_RUNTIME_FILES}
            "${_destination}"
            PARENT_SCOPE
        )
    endif()
endfunction()

function(stage_streamline_notice _relative_path)
    set(_source "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/${_relative_path}")
    if(NOT SKIP_RUNTIME_DOWNLOADS AND (NOT EXISTS "${_source}" OR IS_DIRECTORY "${_source}"))
        message(
            FATAL_ERROR
            "Streamline ${STREAMLINE_RUNTIME_VERSION} archive is missing ${_relative_path}"
        )
    endif()

    get_filename_component(_name "${_source}" NAME)
    foreach(
        _destination_directory
        IN ITEMS
            "${STREAMLINE_RUNTIME_DIRECTORY}"
            "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
    )
        set(_destination "${_destination_directory}/${_name}")
        list(APPEND STREAMLINE_RUNTIME_PAYLOAD_FILES "${_destination}")
        verify_streamline_source("${_source}" _available)
        if(NOT _available)
            list(APPEND STREAMLINE_RUNTIME_PAYLOAD_MISSING "${_destination}")
            continue()
        endif()
        file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
        list(APPEND STREAMLINE_RUNTIME_FILES "${_destination}")
        if(
            "${_destination_directory}"
            STREQUAL "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
        )
            list(APPEND STREAMLINE_DLSSG_RUNTIME_FILES "${_destination}")
        else()
            list(APPEND STREAMLINE_DX11_RUNTIME_FILES "${_destination}")
        endif()
    endforeach()

    set(_streamline_verified_hashes "${_streamline_verified_hashes}" PARENT_SCOPE)
    set(STREAMLINE_RUNTIME_PAYLOAD_FILES ${STREAMLINE_RUNTIME_PAYLOAD_FILES} PARENT_SCOPE)
    set(STREAMLINE_RUNTIME_PAYLOAD_MISSING ${STREAMLINE_RUNTIME_PAYLOAD_MISSING} PARENT_SCOPE)
    set(STREAMLINE_RUNTIME_FILES ${STREAMLINE_RUNTIME_FILES} PARENT_SCOPE)
    set(
        STREAMLINE_DX11_RUNTIME_FILES
        ${STREAMLINE_DX11_RUNTIME_FILES}
        PARENT_SCOPE
    )
    set(
        STREAMLINE_DLSSG_RUNTIME_FILES
        ${STREAMLINE_DLSSG_RUNTIME_FILES}
        PARENT_SCOPE
    )
endfunction()

if(NOT SKIP_RUNTIME_DOWNLOADS)
    file(REMOVE_RECURSE "${STREAMLINE_RUNTIME_FEATURE_ROOT}")
endif()
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_DIRECTORY}")
file(MAKE_DIRECTORY "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}")

set(STREAMLINE_RUNTIME_FILES "")
set(STREAMLINE_RUNTIME_PAYLOAD_FILES "")
set(STREAMLINE_RUNTIME_PAYLOAD_MISSING "")
set(STREAMLINE_DX11_RUNTIME_FILES "")
set(STREAMLINE_DLSSG_RUNTIME_FILES "")
set(
    _streamline_dx11_runtime_names
    nvngx_dlss.dll
    sl.common.dll
    sl.dlss.dll
    sl.interposer.dll
    sl.pcl.dll
    sl.reflex.dll
)
set(
    _streamline_dlssg_runtime_names
    nvngx_dlssg.dll
    sl.common.dll
    sl.dlss_g.dll
    sl.interposer.dll
    sl.pcl.dll
    sl.reflex.dll
)
foreach(_runtime_name IN LISTS _streamline_dx11_runtime_names)
    stage_streamline_runtime("${_runtime_name}")
endforeach()
foreach(_runtime_name IN LISTS _streamline_dlssg_runtime_names)
    stage_streamline_runtime(
        "${_runtime_name}"
        "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
    )
endforeach()

foreach(
    _license_relative_path
    IN ITEMS
        license.txt
        3rd-party-licenses.md
        "NVIDIA Nsight Graphics SDK License (Apache 2.0).txt"
        bin/x64/nvngx_dlss.license.txt
        bin/x64/reflex.license.txt
)
    stage_streamline_notice("${_license_relative_path}")
endforeach()

if(NOT SKIP_RUNTIME_DOWNLOADS)
    file(WRITE "${STREAMLINE_RUNTIME_SOURCE_HASHES}" "${_streamline_verified_hashes}\n")
endif()
