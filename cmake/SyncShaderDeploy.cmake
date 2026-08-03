# Content-based sync of staged Community Shaders files into a deployment
# target. Unchanged files retain their destination mtimes, avoiding false
# runtime FileWatcher events and unnecessary shader-cache invalidation.
#
# The destination may be a shared Skyrim Data/Shaders directory. Therefore
# stale cleanup is limited to files recorded in this build tree's ownership
# manifest, and a stale file is removed only when its content still matches
# the last content this script deployed.
#
# Required:
#   SRC_DIR       Staged AIO/Shaders directory.
#   DST_DIR       Deployed Shaders directory.
#   MANIFEST_FILE Per-target ownership manifest outside DST_DIR.

foreach(_required IN ITEMS SRC_DIR DST_DIR MANIFEST_FILE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "SyncShaderDeploy.cmake requires ${_required}")
    endif()
endforeach()

function(normalize_absolute_path _input _output)
    file(TO_CMAKE_PATH "${_input}" _native_path)
    cmake_path(SET _normalized NORMALIZE "${_native_path}")
    if(NOT IS_ABSOLUTE "${_normalized}")
        cmake_path(
            ABSOLUTE_PATH _normalized
            BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            NORMALIZE
        )
    endif()
    string(REGEX REPLACE "/+$" "" _normalized "${_normalized}")
    set(${_output} "${_normalized}" PARENT_SCOPE)
endfunction()

function(normalize_relative_path _input _output _valid)
    string(REPLACE "\\" "/" _normalized "${_input}")
    set(_is_valid TRUE)
    if(
        "${_normalized}" STREQUAL ""
        OR IS_ABSOLUTE "${_normalized}"
        OR _normalized MATCHES "(^|/)\\.\\.(/|$)"
        OR _normalized MATCHES "(^|/)\\.(/|$)"
        OR _normalized MATCHES "[|;]"
    )
        set(_is_valid FALSE)
    endif()
    set(${_output} "${_normalized}" PARENT_SCOPE)
    set(${_valid} ${_is_valid} PARENT_SCOPE)
endfunction()

function(path_key _path _output)
    string(TOLOWER "${_path}" _key)
    set(${_output} "${_key}" PARENT_SCOPE)
endfunction()

function(path_under_root _root _relative _output)
    set(_candidate "${_root}/${_relative}")
    cmake_path(SET _candidate NORMALIZE "${_candidate}")
    path_key("${_root}/" _root_key)
    path_key("${_candidate}" _candidate_key)
    string(FIND "${_candidate_key}" "${_root_key}" _prefix_index)
    if(NOT _prefix_index EQUAL 0)
        message(
            FATAL_ERROR
            "Shader deploy path escapes destination root: ${_relative}"
        )
    endif()
    set(${_output} "${_candidate}" PARENT_SCOPE)
endfunction()

normalize_absolute_path("${SRC_DIR}" _src_root)
normalize_absolute_path("${DST_DIR}" _dst_root)
normalize_absolute_path("${MANIFEST_FILE}" _manifest_file)

path_key("${_src_root}" _src_root_key)
path_key("${_dst_root}" _dst_root_key)
path_key("${_manifest_file}" _manifest_key)
set(_src_prefix "${_src_root_key}/")
set(_dst_prefix "${_dst_root_key}/")
string(FIND "${_dst_prefix}" "${_src_prefix}" _dst_in_src)
string(FIND "${_src_prefix}" "${_dst_prefix}" _src_in_dst)
string(FIND "${_manifest_key}" "${_src_prefix}" _manifest_in_src)
string(FIND "${_manifest_key}" "${_dst_prefix}" _manifest_in_dst)
if(
    _src_root_key STREQUAL _dst_root_key
    OR _dst_in_src EQUAL 0
    OR _src_in_dst EQUAL 0
    OR _manifest_key STREQUAL _src_root_key
    OR _manifest_key STREQUAL _dst_root_key
    OR _manifest_in_src EQUAL 0
    OR _manifest_in_dst EQUAL 0
)
    message(
        FATAL_ERROR
        "Shader source, destination, and external manifest paths overlap: ${_src_root} ; ${_dst_root} ; ${_manifest_file}"
    )
endif()

# Validate the complete source set before creating or deleting anything in the
# destination. An unexpectedly missing or empty AIO tree must fail closed.
if(NOT IS_DIRECTORY "${_src_root}")
    message(FATAL_ERROR "Shader deploy source directory does not exist: ${_src_root}")
endif()
file(GLOB_RECURSE _src_files LIST_DIRECTORIES FALSE "${_src_root}/*")
if(NOT _src_files)
    message(FATAL_ERROR "Shader deploy source directory is empty: ${_src_root}")
endif()

set(_source_keys)
set(_source_rels)
foreach(_src IN LISTS _src_files)
    file(RELATIVE_PATH _rel "${_src_root}" "${_src}")
    normalize_relative_path("${_rel}" _rel _rel_valid)
    if(NOT _rel_valid)
        message(FATAL_ERROR "Unsafe shader source path: ${_src}")
    endif()
    path_key("${_rel}" _key)
    list(FIND _source_keys "${_key}" _duplicate_index)
    if(NOT _duplicate_index EQUAL -1)
        list(GET _source_rels ${_duplicate_index} _duplicate_rel)
        message(
            FATAL_ERROR
            "Case-insensitive shader path collision: ${_duplicate_rel} ; ${_rel}"
        )
    endif()
    list(APPEND _source_keys "${_key}")
    list(APPEND _source_rels "${_rel}")
endforeach()

get_filename_component(_manifest_dir "${_manifest_file}" DIRECTORY)
file(MAKE_DIRECTORY "${_manifest_dir}")
file(
    LOCK "${_manifest_file}.lock"
    GUARD PROCESS
    TIMEOUT 60
    RESULT_VARIABLE _lock_result
)
if(NOT _lock_result STREQUAL "0")
    message(
        FATAL_ERROR
        "Could not lock shader deploy manifest ${_manifest_file}: ${_lock_result}"
    )
endif()

# Read the previous ownership state. Invalid or unsupported state is ignored
# rather than trusted for deletion; the successful sync below replaces it.
set(_previous_keys)
set(_previous_hashes)
set(_previous_rels)
set(_manifest_header "# Community Shaders deploy manifest v1")
if(EXISTS "${_manifest_file}")
    file(STRINGS "${_manifest_file}" _manifest_lines ENCODING UTF-8)
    list(LENGTH _manifest_lines _manifest_line_count)
    if(_manifest_line_count GREATER 0)
        list(GET _manifest_lines 0 _existing_header)
    else()
        set(_existing_header "")
    endif()

    if(NOT _existing_header STREQUAL _manifest_header)
        message(
            WARNING
            "Ignoring unsupported shader deploy manifest; no stale files will be removed: ${_manifest_file}"
        )
    else()
        list(REMOVE_AT _manifest_lines 0)
        foreach(_line IN LISTS _manifest_lines)
            string(REPLACE "|" ";" _parts "${_line}")
            list(LENGTH _parts _part_count)
            if(NOT _part_count EQUAL 3)
                message(WARNING "Ignoring malformed shader ownership entry: ${_line}")
                continue()
            endif()

            list(GET _parts 0 _old_key)
            list(GET _parts 1 _old_hash)
            list(GET _parts 2 _old_rel)
            normalize_relative_path("${_old_rel}" _old_rel _old_rel_valid)
            path_key("${_old_rel}" _derived_key)
            string(LENGTH "${_old_hash}" _hash_length)
            if(
                NOT _old_rel_valid
                OR NOT _old_key STREQUAL _derived_key
                OR NOT _hash_length EQUAL 64
                OR NOT _old_hash MATCHES "^[0-9A-Fa-f]+$"
            )
                message(WARNING "Ignoring unsafe shader ownership entry: ${_line}")
                continue()
            endif()

            list(FIND _previous_keys "${_old_key}" _old_duplicate_index)
            if(NOT _old_duplicate_index EQUAL -1)
                message(WARNING "Ignoring duplicate shader ownership entry: ${_line}")
                continue()
            endif()
            string(TOLOWER "${_old_hash}" _old_hash)
            list(APPEND _previous_keys "${_old_key}")
            list(APPEND _previous_hashes "${_old_hash}")
            list(APPEND _previous_rels "${_old_rel}")
        endforeach()
    endif()
endif()

file(MAKE_DIRECTORY "${_dst_root}")

# Copy and verify every current source before considering stale removals. The
# manifest records the verified deployed content, not merely the intended path.
set(_new_manifest_entries)
list(LENGTH _src_files _source_count)
math(EXPR _source_last "${_source_count} - 1")
foreach(_index RANGE 0 ${_source_last})
    list(GET _src_files ${_index} _src)
    list(GET _source_keys ${_index} _key)
    list(GET _source_rels ${_index} _rel)
    path_under_root("${_dst_root}" "${_rel}" _dst)
    get_filename_component(_dst_dir "${_dst}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dst_dir}")
    if(IS_SYMLINK "${_dst}")
        message(FATAL_ERROR "Refusing to deploy through a shader symlink: ${_dst}")
    endif()

    file(
        COPY_FILE "${_src}" "${_dst}"
        ONLY_IF_DIFFERENT
        INPUT_MAY_BE_RECENT
        RESULT _copy_result
    )
    if(NOT _copy_result STREQUAL "0")
        message(
            FATAL_ERROR
            "Failed to deploy shader file ${_src} -> ${_dst}: ${_copy_result}"
        )
    endif()
    file(SHA256 "${_src}" _source_hash)
    file(SHA256 "${_dst}" _deployed_hash)
    if(NOT _deployed_hash STREQUAL _source_hash)
        message(
            FATAL_ERROR
            "Shader changed while being deployed or failed verification: ${_src} -> ${_dst}"
        )
    endif()
    string(TOLOWER "${_deployed_hash}" _deployed_hash)
    list(APPEND _new_manifest_entries "${_key}|${_deployed_hash}|${_rel}")
endforeach()

# Remove only files this manifest previously owned, and only if another mod or
# user has not replaced the deployed content since the last successful sync.
list(LENGTH _previous_keys _previous_count)
if(_previous_count GREATER 0)
    math(EXPR _previous_last "${_previous_count} - 1")
    foreach(_index RANGE 0 ${_previous_last})
        list(GET _previous_keys ${_index} _old_key)
        list(FIND _source_keys "${_old_key}" _current_index)
        if(NOT _current_index EQUAL -1)
            continue()
        endif()

        list(GET _previous_hashes ${_index} _old_hash)
        list(GET _previous_rels ${_index} _old_rel)
        path_under_root("${_dst_root}" "${_old_rel}" _stale_path)
        if(IS_SYMLINK "${_stale_path}")
            message(
                WARNING
                "Preserving stale shader symlink and releasing ownership: ${_stale_path}"
            )
        elseif(EXISTS "${_stale_path}" AND NOT IS_DIRECTORY "${_stale_path}")
            file(SHA256 "${_stale_path}" _current_hash)
            string(TOLOWER "${_current_hash}" _current_hash)
            if(_current_hash STREQUAL _old_hash)
                file(REMOVE "${_stale_path}")
                if(EXISTS "${_stale_path}")
                    message(FATAL_ERROR "Failed to remove stale shader: ${_stale_path}")
                endif()
            else()
                message(
                    WARNING
                    "Preserving modified stale shader and releasing ownership: ${_stale_path}"
                )
            endif()
        endif()
    endforeach()
endif()

list(SORT _new_manifest_entries COMPARE STRING CASE INSENSITIVE)
set(_manifest_content "${_manifest_header}\n")
foreach(_entry IN LISTS _new_manifest_entries)
    string(APPEND _manifest_content "${_entry}\n")
endforeach()

# Publish ownership atomically only after copy verification and stale cleanup
# have both completed. The caller touches its success stamp afterwards.
set(_manifest_temp "${_manifest_file}.tmp")
file(WRITE "${_manifest_temp}" "${_manifest_content}")
file(RENAME "${_manifest_temp}" "${_manifest_file}" RESULT _rename_result)
if(NOT _rename_result STREQUAL "0")
    message(
        FATAL_ERROR
        "Failed to publish shader deploy manifest ${_manifest_file}: ${_rename_result}"
    )
endif()
