cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED STAGE_PINNED_RUNTIME_MANIFEST OR
   NOT EXISTS "${STAGE_PINNED_RUNTIME_MANIFEST}")
    message(FATAL_ERROR "STAGE_PINNED_RUNTIME_MANIFEST is required")
endif()
if(NOT DEFINED STAGE_PINNED_RUNTIME_DESTINATION OR
   "${STAGE_PINNED_RUNTIME_DESTINATION}" STREQUAL "")
    message(FATAL_ERROR "STAGE_PINNED_RUNTIME_DESTINATION is required")
endif()
if(NOT DEFINED STAGE_PINNED_RUNTIME_MANIFEST_SHA256 OR
   "${STAGE_PINNED_RUNTIME_MANIFEST_SHA256}" STREQUAL "")
    message(FATAL_ERROR "STAGE_PINNED_RUNTIME_MANIFEST_SHA256 is required")
endif()

file(SHA256 "${STAGE_PINNED_RUNTIME_MANIFEST}" _runtime_manifest_hash)
string(TOUPPER "${_runtime_manifest_hash}" _runtime_manifest_hash)
string(
    TOUPPER
    "${STAGE_PINNED_RUNTIME_MANIFEST_SHA256}"
    _runtime_manifest_expected_hash
)
if(NOT _runtime_manifest_hash STREQUAL _runtime_manifest_expected_hash)
    message(FATAL_ERROR
        "Pinned runtime manifest hash mismatch: ${_runtime_manifest_hash}"
    )
endif()

file(STRINGS "${STAGE_PINNED_RUNTIME_MANIFEST}" _runtime_manifest_entries)
if(NOT _runtime_manifest_entries)
    message(FATAL_ERROR "The pinned runtime manifest is empty")
endif()

# Validate the complete transaction before changing a consumer directory.
set(_runtime_expected_names "")
foreach(_manifest_entry IN LISTS _runtime_manifest_entries)
    string(REPLACE "|" ";" _manifest_fields "${_manifest_entry}")
    list(LENGTH _manifest_fields _manifest_field_count)
    if(NOT _manifest_field_count EQUAL 3)
        message(FATAL_ERROR "Malformed pinned runtime entry: ${_manifest_entry}")
    endif()

    list(GET _manifest_fields 0 _runtime_name)
    list(GET _manifest_fields 1 _runtime_source)
    list(GET _manifest_fields 2 _expected_hash)
    if(NOT _runtime_name MATCHES [[^[A-Za-z0-9_.-]+\.[dD][lL][lL]$]])
        message(FATAL_ERROR "Invalid pinned runtime filename: ${_runtime_name}")
    endif()
    string(TOLOWER "${_runtime_name}" _runtime_name_key)
    if(_runtime_name_key IN_LIST _runtime_expected_names)
        message(FATAL_ERROR "Duplicate pinned runtime filename: ${_runtime_name}")
    endif()
    list(APPEND _runtime_expected_names "${_runtime_name_key}")
    if(NOT EXISTS "${_runtime_source}")
        message(FATAL_ERROR "Pinned runtime source is missing: ${_runtime_source}")
    endif()

    file(SHA256 "${_runtime_source}" _source_hash)
    string(TOUPPER "${_source_hash}" _source_hash)
    string(TOUPPER "${_expected_hash}" _expected_hash)
    if(NOT _source_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Rejected ${_runtime_source}: expected SHA-256 ${_expected_hash}, got ${_source_hash}"
        )
    endif()

endforeach()

set(_runtime_notice_destination "")
if(DEFINED STAGE_PINNED_RUNTIME_NOTICE_SOURCE AND
   NOT "${STAGE_PINNED_RUNTIME_NOTICE_SOURCE}" STREQUAL "")
    if(NOT EXISTS "${STAGE_PINNED_RUNTIME_NOTICE_SOURCE}")
        message(FATAL_ERROR
            "Pinned runtime notice is missing: ${STAGE_PINNED_RUNTIME_NOTICE_SOURCE}"
        )
    endif()
    if(NOT DEFINED STAGE_PINNED_RUNTIME_NOTICE_DESTINATION OR
       "${STAGE_PINNED_RUNTIME_NOTICE_DESTINATION}" STREQUAL "")
        message(FATAL_ERROR
            "STAGE_PINNED_RUNTIME_NOTICE_DESTINATION is required"
        )
    endif()
    if(NOT DEFINED STAGE_PINNED_RUNTIME_NOTICE_SHA256 OR
       "${STAGE_PINNED_RUNTIME_NOTICE_SHA256}" STREQUAL "")
        message(FATAL_ERROR "STAGE_PINNED_RUNTIME_NOTICE_SHA256 is required")
    endif()
    file(SHA256 "${STAGE_PINNED_RUNTIME_NOTICE_SOURCE}" _runtime_notice_hash)
    string(TOUPPER "${_runtime_notice_hash}" _runtime_notice_hash)
    string(
        TOUPPER
        "${STAGE_PINNED_RUNTIME_NOTICE_SHA256}"
        _runtime_notice_expected_hash
    )
    if(NOT _runtime_notice_hash STREQUAL _runtime_notice_expected_hash)
        message(FATAL_ERROR
            "Pinned runtime notice source hash mismatch: ${_runtime_notice_hash}"
        )
    endif()
    get_filename_component(
        _runtime_notice_name "${STAGE_PINNED_RUNTIME_NOTICE_SOURCE}" NAME
    )
    set(
        _runtime_notice_destination
        "${STAGE_PINNED_RUNTIME_NOTICE_DESTINATION}/${_runtime_notice_name}"
    )
endif()

get_filename_component(
    _runtime_work_directory "${STAGE_PINNED_RUNTIME_MANIFEST}" DIRECTORY
)
string(SHA256
    _runtime_destination_key "${STAGE_PINNED_RUNTIME_DESTINATION}"
)
set(
    _runtime_pending_directory
    "${_runtime_work_directory}/pending-${_runtime_destination_key}"
)
file(REMOVE_RECURSE "${_runtime_pending_directory}")
file(MAKE_DIRECTORY "${_runtime_pending_directory}")

foreach(_manifest_entry IN LISTS _runtime_manifest_entries)
    string(REPLACE "|" ";" _manifest_fields "${_manifest_entry}")
    list(GET _manifest_fields 0 _runtime_name)
    list(GET _manifest_fields 1 _runtime_source)
    list(GET _manifest_fields 2 _expected_hash)
    set(_runtime_pending "${_runtime_pending_directory}/${_runtime_name}")
    file(COPY_FILE "${_runtime_source}" "${_runtime_pending}")
    file(SHA256 "${_runtime_pending}" _pending_hash)
    string(TOUPPER "${_pending_hash}" _pending_hash)
    string(TOUPPER "${_expected_hash}" _expected_hash)
    if(NOT _pending_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Pending runtime hash mismatch for ${_runtime_name}: ${_pending_hash}"
        )
    endif()
endforeach()

# Private payloads receive their verified warning before any consumer DLLs.
if(NOT "${_runtime_notice_destination}" STREQUAL "")
    file(MAKE_DIRECTORY "${STAGE_PINNED_RUNTIME_NOTICE_DESTINATION}")
    file(
        COPY_FILE
        "${STAGE_PINNED_RUNTIME_NOTICE_SOURCE}"
        "${_runtime_notice_destination}"
        ONLY_IF_DIFFERENT
    )
    file(SHA256 "${_runtime_notice_destination}" _runtime_notice_hash)
    string(TOUPPER "${_runtime_notice_hash}" _runtime_notice_hash)
    if(NOT _runtime_notice_hash STREQUAL _runtime_notice_expected_hash)
        message(FATAL_ERROR
            "Pinned runtime notice hash mismatch: ${_runtime_notice_hash}"
        )
    endif()
endif()

file(MAKE_DIRECTORY "${STAGE_PINNED_RUNTIME_DESTINATION}")
foreach(_manifest_entry IN LISTS _runtime_manifest_entries)
    string(REPLACE "|" ";" _manifest_fields "${_manifest_entry}")
    list(GET _manifest_fields 0 _runtime_name)
    list(GET _manifest_fields 2 _expected_hash)
    set(_runtime_pending "${_runtime_pending_directory}/${_runtime_name}")
    set(_runtime_destination
        "${STAGE_PINNED_RUNTIME_DESTINATION}/${_runtime_name}"
    )
    file(COPY_FILE "${_runtime_pending}" "${_runtime_destination}")
    file(SHA256 "${_runtime_destination}" _destination_hash)
    string(TOUPPER "${_destination_hash}" _destination_hash)
    string(TOUPPER "${_expected_hash}" _expected_hash)
    if(NOT _destination_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Staged runtime hash mismatch for ${_runtime_destination}: ${_destination_hash}"
        )
    endif()
endforeach()

# Final consumers are repaired to the exact flat manifest. This removes stale
# or manually supplied plugins before a package or deployment can use them.
file(
    GLOB_RECURSE _runtime_destination_dlls
    LIST_DIRECTORIES FALSE
    "${STAGE_PINNED_RUNTIME_DESTINATION}/*.[dD][lL][lL]"
)
foreach(_runtime_destination_dll IN LISTS _runtime_destination_dlls)
    file(
        RELATIVE_PATH _runtime_destination_relative
        "${STAGE_PINNED_RUNTIME_DESTINATION}"
        "${_runtime_destination_dll}"
    )
    get_filename_component(
        _runtime_destination_name "${_runtime_destination_dll}" NAME
    )
    string(TOLOWER "${_runtime_destination_name}" _runtime_destination_key)
    if(_runtime_destination_relative MATCHES "[/\\\\]"
       OR NOT _runtime_destination_key IN_LIST _runtime_expected_names)
        file(REMOVE "${_runtime_destination_dll}")
    endif()
endforeach()

if("${_runtime_notice_destination}" STREQUAL "" AND
   DEFINED STAGE_PINNED_RUNTIME_NOTICE_DESTINATION AND
   NOT "${STAGE_PINNED_RUNTIME_NOTICE_DESTINATION}" STREQUAL "")
    file(REMOVE
        "${STAGE_PINNED_RUNTIME_NOTICE_DESTINATION}/INTERNAL-NO-REDISTRIBUTION.txt"
    )
endif()
file(REMOVE_RECURSE "${_runtime_pending_directory}")

if(DEFINED STAGE_PINNED_RUNTIME_STAMP AND
   NOT "${STAGE_PINNED_RUNTIME_STAMP}" STREQUAL "")
    file(TOUCH "${STAGE_PINNED_RUNTIME_STAMP}")
endif()
