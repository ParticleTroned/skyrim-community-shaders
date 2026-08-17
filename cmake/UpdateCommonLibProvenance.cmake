cmake_minimum_required(VERSION 4.2)

foreach(
    _required_variable
    IN ITEMS
        CSX_SOURCE_ROOT
        CSX_COMMONLIB_SOURCE_DIR
        CSX_COMMONLIB_RELATIVE_PATH
        CSX_COMMONLIB_PROVENANCE_POLICY
        CSX_COMMONLIB_LINKAGE
        CSX_PROVENANCE_HEADER_TEMPLATE
        CSX_PROVENANCE_RESOURCE_TEMPLATE
        CSX_PROVENANCE_HEADER_OUTPUT
        CSX_PROVENANCE_RESOURCE_OUTPUT
)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable ${_required_variable}.")
    endif()
endforeach()

string(TOUPPER "${CSX_COMMONLIB_PROVENANCE_POLICY}" CSX_COMMONLIB_PROVENANCE_POLICY)
if(NOT CSX_COMMONLIB_PROVENANCE_POLICY MATCHES "^(WARN|STRICT)$")
    message(FATAL_ERROR "CSX_COMMONLIB_PROVENANCE_POLICY must be WARN or STRICT.")
endif()
if(NOT CSX_COMMONLIB_LINKAGE MATCHES "^(source|prebuilt)$")
    message(FATAL_ERROR "CSX_COMMONLIB_LINKAGE must be source or prebuilt.")
endif()

function(_csx_normalize_path input_path output_variable)
    file(REAL_PATH "${input_path}" _normalized_path)
    cmake_path(NORMAL_PATH _normalized_path)
    if(WIN32)
        string(TOLOWER "${_normalized_path}" _normalized_path)
    endif()
    set(${output_variable} "${_normalized_path}" PARENT_SCOPE)
endfunction()

function(_csx_normalize_revision input_revision output_variable valid_variable)
    string(STRIP "${input_revision}" _revision)
    string(LENGTH "${_revision}" _revision_length)
    if(
        (_revision_length EQUAL 40 OR _revision_length EQUAL 64)
        AND NOT _revision MATCHES "[^0-9A-Fa-f]"
    )
        string(TOLOWER "${_revision}" _revision)
        set(${output_variable} "${_revision}" PARENT_SCOPE)
        set(${valid_variable} TRUE PARENT_SCOPE)
    else()
        set(${output_variable} "unavailable" PARENT_SCOPE)
        set(${valid_variable} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_csx_is_owned_git_repository repository_path owned_variable reason_variable)
    if(NOT EXISTS "${repository_path}/.git")
        set(${owned_variable} FALSE PARENT_SCOPE)
        set(${reason_variable} "no direct Git metadata" PARENT_SCOPE)
        return()
    endif()
    if(
        NOT DEFINED CSX_GIT_EXECUTABLE
        OR "${CSX_GIT_EXECUTABLE}" STREQUAL ""
        OR "${CSX_GIT_EXECUTABLE}" MATCHES "-NOTFOUND$"
    )
        set(${owned_variable} FALSE PARENT_SCOPE)
        set(${reason_variable} "Git is unavailable" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND
            "${CSX_GIT_EXECUTABLE}" -C "${repository_path}" rev-parse
            --show-toplevel
        RESULT_VARIABLE _root_result
        OUTPUT_VARIABLE _reported_root
        ERROR_VARIABLE _root_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _root_result EQUAL 0)
        string(STRIP "${_root_error}" _root_error)
        set(${owned_variable} FALSE PARENT_SCOPE)
        set(${reason_variable} "Git root lookup failed: ${_root_error}" PARENT_SCOPE)
        return()
    endif()

    _csx_normalize_path("${repository_path}" _requested_root)
    _csx_normalize_path("${_reported_root}" _actual_root)
    if(NOT _requested_root STREQUAL _actual_root)
        set(${owned_variable} FALSE PARENT_SCOPE)
        set(
            ${reason_variable}
            "Git metadata belongs to another repository"
            PARENT_SCOPE
        )
        return()
    endif()

    set(${owned_variable} TRUE PARENT_SCOPE)
    set(${reason_variable} "" PARENT_SCOPE)
endfunction()

set(CSX_COMMONLIB_EXPECTED_REVISION "unavailable")
set(CSX_COMMONLIB_CHECKOUT_REVISION "unavailable")
set(CSX_COMMONLIB_CHECKOUT_STATE "unavailable")
set(CSX_COMMONLIB_PROVENANCE_STATUS "unverified")

_csx_is_owned_git_repository(
    "${CSX_SOURCE_ROOT}"
    _source_is_git_repository
    _source_repository_reason
)
_csx_is_owned_git_repository(
    "${CSX_COMMONLIB_SOURCE_DIR}"
    _commonlib_is_git_repository
    _commonlib_repository_reason
)
set(_provenance_issues "")
if(NOT _source_is_git_repository)
    list(APPEND _provenance_issues "superproject: ${_source_repository_reason}")
endif()
if(NOT _commonlib_is_git_repository)
    list(APPEND _provenance_issues "CommonLib checkout: ${_commonlib_repository_reason}")
endif()

if(DEFINED CSX_COMMONLIB_ARCHIVE_REVISION AND NOT CSX_COMMONLIB_ARCHIVE_REVISION STREQUAL "")
    _csx_normalize_revision(
        "${CSX_COMMONLIB_ARCHIVE_REVISION}"
        _archive_revision
        _archive_revision_valid
    )
    if(NOT _archive_revision_valid)
        message(
            FATAL_ERROR
            "CSX_COMMONLIB_ARCHIVE_REVISION must be a 40- or 64-character hexadecimal revision."
        )
    endif()
    if(EXISTS "${CSX_SOURCE_ROOT}/.git" OR EXISTS "${CSX_COMMONLIB_SOURCE_DIR}/.git")
        message(
            FATAL_ERROR
            "CSX_COMMONLIB_ARCHIVE_REVISION cannot override available Git metadata."
        )
    endif()
endif()

if(_source_is_git_repository)
    execute_process(
        COMMAND
            "${CSX_GIT_EXECUTABLE}" -C "${CSX_SOURCE_ROOT}" ls-tree HEAD --
            "${CSX_COMMONLIB_RELATIVE_PATH}"
        RESULT_VARIABLE _expected_result
        OUTPUT_VARIABLE _expected_entry
        ERROR_VARIABLE _expected_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_expected_result EQUAL 0 AND _expected_entry MATCHES "^160000[ \t]+commit[ \t]+([0-9A-Fa-f]+)[ \t]")
        _csx_normalize_revision(
            "${CMAKE_MATCH_1}"
            CSX_COMMONLIB_EXPECTED_REVISION
            _expected_revision_valid
        )
        if(NOT _expected_revision_valid)
            list(APPEND _provenance_issues "the committed CommonLib gitlink is invalid")
        endif()
    elseif(NOT _expected_result EQUAL 0)
        string(STRIP "${_expected_error}" _expected_error)
        list(
            APPEND _provenance_issues
            "the committed CommonLib gitlink could not be read: ${_expected_error}"
        )
    else()
        list(APPEND _provenance_issues "the committed CommonLib gitlink is missing or invalid")
    endif()
endif()

if(_commonlib_is_git_repository)
    execute_process(
        COMMAND
            "${CSX_GIT_EXECUTABLE}" -C "${CSX_COMMONLIB_SOURCE_DIR}"
            rev-parse --verify "HEAD^{commit}"
        RESULT_VARIABLE _checkout_result
        OUTPUT_VARIABLE _checkout_revision
        ERROR_VARIABLE _checkout_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_checkout_result EQUAL 0)
        _csx_normalize_revision(
            "${_checkout_revision}"
            CSX_COMMONLIB_CHECKOUT_REVISION
            _checkout_revision_valid
        )
        if(NOT _checkout_revision_valid)
            list(APPEND _provenance_issues "the CommonLib checkout revision is invalid")
        endif()
    else()
        string(STRIP "${_checkout_error}" _checkout_error)
        list(
            APPEND _provenance_issues
            "the CommonLib checkout revision could not be read: ${_checkout_error}"
        )
    endif()

    execute_process(
        COMMAND
            "${CSX_GIT_EXECUTABLE}" -C "${CSX_COMMONLIB_SOURCE_DIR}" status
            --porcelain=v1 --untracked-files=normal --ignore-submodules=none
        RESULT_VARIABLE _status_result
        OUTPUT_VARIABLE _status_output
        ERROR_VARIABLE _status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_status_result EQUAL 0)
        if(_status_output STREQUAL "")
            set(CSX_COMMONLIB_CHECKOUT_STATE "clean")
        else()
            set(CSX_COMMONLIB_CHECKOUT_STATE "dirty")
        endif()
    else()
        string(STRIP "${_status_error}" _status_error)
        list(
            APPEND _provenance_issues
            "the CommonLib checkout state could not be read: ${_status_error}"
        )
    endif()
endif()

if(
    NOT CSX_COMMONLIB_EXPECTED_REVISION STREQUAL "unavailable"
    AND NOT CSX_COMMONLIB_CHECKOUT_REVISION STREQUAL "unavailable"
    AND NOT CSX_COMMONLIB_CHECKOUT_STATE STREQUAL "unavailable"
)
    if(CSX_COMMONLIB_EXPECTED_REVISION STREQUAL CSX_COMMONLIB_CHECKOUT_REVISION)
        set(CSX_COMMONLIB_PROVENANCE_STATUS "${CSX_COMMONLIB_CHECKOUT_STATE}-match")
    else()
        set(CSX_COMMONLIB_PROVENANCE_STATUS "${CSX_COMMONLIB_CHECKOUT_STATE}-mismatch")
    endif()
elseif(
    DEFINED _archive_revision_valid
    AND _archive_revision_valid
    AND NOT _source_is_git_repository
    AND NOT _commonlib_is_git_repository
)
    set(CSX_COMMONLIB_CHECKOUT_REVISION "${_archive_revision}")
    set(CSX_COMMONLIB_CHECKOUT_STATE "declared")
    set(CSX_COMMONLIB_PROVENANCE_STATUS "archive-declared")
endif()

if(CSX_COMMONLIB_LINKAGE STREQUAL "prebuilt")
    set(CSX_COMMONLIB_PROVENANCE_STATUS "prebuilt-unverified")
endif()

get_filename_component(_header_output_directory "${CSX_PROVENANCE_HEADER_OUTPUT}" DIRECTORY)
get_filename_component(_resource_output_directory "${CSX_PROVENANCE_RESOURCE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_header_output_directory}" "${_resource_output_directory}")
configure_file(
    "${CSX_PROVENANCE_HEADER_TEMPLATE}"
    "${CSX_PROVENANCE_HEADER_OUTPUT}"
    @ONLY
    NEWLINE_STYLE LF
)
configure_file(
    "${CSX_PROVENANCE_RESOURCE_TEMPLATE}"
    "${CSX_PROVENANCE_RESOURCE_OUTPUT}"
    @ONLY
    NEWLINE_STYLE LF
)

message(
    STATUS
    "CommonLibSSE-NG provenance: expected=${CSX_COMMONLIB_EXPECTED_REVISION}; "
    "checkout=${CSX_COMMONLIB_CHECKOUT_REVISION}; "
    "state=${CSX_COMMONLIB_CHECKOUT_STATE}; "
    "status=${CSX_COMMONLIB_PROVENANCE_STATUS}; linkage=${CSX_COMMONLIB_LINKAGE}"
)
if(
    CSX_COMMONLIB_PROVENANCE_STATUS STREQUAL "unverified"
    AND _provenance_issues
)
    string(JOIN "; " _provenance_issue_summary ${_provenance_issues})
    message(STATUS "CommonLib provenance details: ${_provenance_issue_summary}")
endif()

if(CSX_COMMONLIB_PROVENANCE_POLICY STREQUAL "STRICT")
    if(
        NOT CSX_COMMONLIB_PROVENANCE_STATUS STREQUAL "clean-match"
        OR NOT CSX_COMMONLIB_LINKAGE STREQUAL "source"
    )
        set(_strict_failure_detail "")
        if(_provenance_issues)
            string(JOIN "; " _provenance_issue_summary ${_provenance_issues})
            set(_strict_failure_detail " Details: ${_provenance_issue_summary}.")
        endif()
        message(
            FATAL_ERROR
            "Strict CommonLib provenance requires a clean source checkout matching the committed gitlink."
            "${_strict_failure_detail}"
        )
    endif()
elseif(NOT CSX_COMMONLIB_PROVENANCE_STATUS STREQUAL "clean-match")
    message(
        WARNING
        "CommonLib provenance is ${CSX_COMMONLIB_PROVENANCE_STATUS}; the build remains allowed by WARN policy."
    )
endif()
